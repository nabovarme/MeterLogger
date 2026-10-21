#include <esp8266.h>
#include "driver/uart.h"

#include "unix_time.h"
#include "mqtt.h"
#include "tinyprintf.h"
#include "en61107_request.h"
#include "en61107.h"
#include "config.h"
#include "crypto/crypto.h"
#include "crypto/aes.h"
#include "utils.h"
#include "led.h"

#ifdef DEBUG
	#include "wifi.h"	// needed for debug_print_wifi_ip() and debug_print_wifi_config()
#endif

#define QUEUE_SIZE 256

#define EN61107_ERROR_RECOVERY_INITIAL_DELAY_MS 5000
#define EN61107_ERROR_RECOVERY_DELAY_MS 60000
#define EN61107_FAST_BOOT_WAKEUP_DELAY_MS 500

#define EN61107_IDENT_RESPONSE_TIMEOUT_MS 3000
#define EN61107_DATA_RESPONSE_TIMEOUT_MS 8000
#define EN61107_STANDARD_DATA_RESPONSE_TIMEOUT_MS 3000
#define EN61107_INST_VALUES_RESPONSE_TIMEOUT_MS 3200

#define EN61107_UART_SETTINGS_DELAY_MS 400
#define EN61107_STANDARD_DATA_UART_SETTINGS_DELAY_MS 220
#define EN61107_INST_VALUES_UART_SETTINGS_DELAY_MS 300

extern bool fast_boot;

os_event_t en61107_received_task_queue[en61107_received_task_queue_length];

uint32_t en61107_serial = 0;
bool en61107_serial_set = false;
int8_t en61107_request_num;
meter_is_ready_cb en61107_meter_is_ready_cb = NULL;
bool meter_is_ready_cb_called = false;

meter_sent_data_cb en61107_meter_sent_data_cb = NULL;

// fifo
volatile unsigned int fifo_head, fifo_tail;
volatile char fifo_buffer[QUEUE_SIZE];
volatile size_t fifo_buffer_fill_count = 0;

// allocate frame to send
char frame[EN61107_FRAME_L];
unsigned int frame_length;

// allocate struct for response
en61107_response_t response;

static MQTT_Client *mqtt_client = NULL;	// initialize to NULL

static os_timer_t en61107_receive_timeout_timer;
static os_timer_t en61107_delayed_uart_change_setting_timer;

volatile en61107_uart_state_t en61107_uart_state;

UartDevice uart_settings;

volatile uint8_t en61107_eod;
bool en61107_etx_received;

// Lock and counters for exponential backoff error recovery
static bool is_in_error_recovery = false;
static uint8_t error_backoff_count = 0;

#ifdef DEBUG_NO_METER
uint8_t pseudo_data_debug_no_meter = 0;
#endif

ICACHE_FLASH_ATTR
void en61107_receive_timeout_timer_func(void *arg) {
	uint32_t backoff_delay;
	
	led_blink();	// DEBUG
	
	en61107_fifo_flush();

	// If the recovery delay is over, allow requests and trigger one
	if (is_in_error_recovery) {
		is_in_error_recovery = false;
		en61107_request_num = 1;
		en61107_uart_send_en61107_ident();
		return;
	}

	// Increment attempt counter on timeout so backoff can be reached
	en61107_request_num++;

	// if no reply received, retransmit
	if (en61107_request_num <= 2) {
		switch (en61107_uart_state) {
			case UART_STATE_EN61107_IDENT:
				en61107_uart_send_en61107_ident();
				break;
			case UART_STATE_EN61107:
				en61107_uart_send_en61107_ident();	// restart en61107 request from state UART_STATE_EN61107_IDENT
				break;
#ifndef MC_66B
			case UART_STATE_INST_VALUES:
				en61107_uart_send_inst_values();
				break;
#endif
			case UART_STATE_STANDARD_DATA_1:
				en61107_uart_send_standard_data_1();
				break;
			case UART_STATE_STANDARD_DATA_2:
				en61107_uart_send_standard_data_2();
				break;
		}
	}
	else {
		// too many errors, backoff and wait for meter to be clear
		is_in_error_recovery = true;
		en61107_uart_state = UART_STATE_NONE;
		en61107_request_num = 0;

		// Exponential backoff maxing out at EN61107_ERROR_RECOVERY_DELAY_MS (60s)
		backoff_delay = EN61107_ERROR_RECOVERY_INITIAL_DELAY_MS * (1 << error_backoff_count);
		if (backoff_delay > EN61107_ERROR_RECOVERY_DELAY_MS) {
			backoff_delay = EN61107_ERROR_RECOVERY_DELAY_MS;
		}
		
		if (error_backoff_count < 10) {
			error_backoff_count++;
		}

#ifndef DEBUG_NO_METER
		uart_set_baudrate(UART0, BIT_RATE_300);
		uart_set_word_length(UART0, SEVEN_BITS);
		uart_set_parity(UART0, EVEN_BITS);
		uart_set_stop_bits(UART0, TWO_STOP_BIT);
#endif

		os_timer_disarm(&en61107_receive_timeout_timer);
		os_timer_setfn(&en61107_receive_timeout_timer, (os_timer_func_t *)en61107_receive_timeout_timer_func, NULL);
		os_timer_arm(&en61107_receive_timeout_timer, backoff_delay, 0);
	}
}

ICACHE_FLASH_ATTR
void en61107_delayed_uart_change_setting_timer_func(UartDevice *uart_settings) {
#ifndef DEBUG_NO_METER
	uart_set_baudrate(UART0, uart_settings->baut_rate);
//	uart_set_word_length(UART0, SEVEN_BITS);
//	uart_set_parity(UART0, EVEN_BITS);
	uart_set_stop_bits(UART0, uart_settings->stop_bits);
#endif
}

// Helper to safely append strings without overflowing the destination buffer
static void append_key_value(char *dst, size_t max_len, const char *src) {
	size_t cur_len = strlen(dst);
	size_t src_len = strlen(src);
	if (cur_len + src_len < max_len - 1) {
		memcpy(dst + cur_len, src, src_len + 1);
	}
}

// define en61107_received_task() first
ICACHE_FLASH_ATTR
static void en61107_received_task(os_event_t *events) {
	char c;
	unsigned int i;
	uint64_t current_unix_time;
	char current_unix_time_string[64];
	char key_value[128];
	char topic[MQTT_TOPIC_L];
	static char message[EN61107_FRAME_L + AES_HMAC_OVERHEAD];
	int message_l;
	uint32_t parsed_serial;

	// vars for aes encryption
	static uint8_t cleartext[EN61107_FRAME_L];

	switch (en61107_uart_state) {
		case UART_STATE_EN61107_IDENT:
			// dont en61107_fifo_get() anything here, we get it in next state
			en61107_uart_send_en61107();
			break;
		case UART_STATE_EN61107:
			// get message buffer
			memset(message, 0, sizeof(message));
			i = 0;
			while (en61107_fifo_get(&c) && (i < EN61107_FRAME_L)) {
				message[i++] = c;
			}
			message_l = i;

			if (parse_en61107_frame(&response, message, message_l) == false) {
				// timeout if we cant parse response
				break;
			}

			// Read and set the serial number on the first successful read
			if (!en61107_serial_set) {
				parsed_serial = atoi(response.customer_no);
				if (parsed_serial == 0) {
					// timeout if we didn't get a serial number
					break;
				}
				en61107_serial = parsed_serial;
				en61107_serial_set = true;
			}
#ifndef MC_66B
			en61107_uart_send_inst_values();
#else
			en61107_uart_send_standard_data_1();
#endif
			break;
#ifndef MC_66B
		case UART_STATE_INST_VALUES:
			// get message buffer
			memset(message, 0, sizeof(message));
			i = 0;
			while (en61107_fifo_get(&c) && (i < EN61107_FRAME_L)) {
				message[i++] = c;
			}
			message_l = i;

			if (parse_mc66cde_inst_values_frame(&response, message, message_l) == false) {
				// timeout if we cant parse response
				break;
			}

			en61107_uart_send_standard_data_1();
			break;
#endif
		case UART_STATE_STANDARD_DATA_1:
			// get message buffer
			memset(message, 0, sizeof(message));
			i = 0;
			while (en61107_fifo_get(&c) && (i < EN61107_FRAME_L)) {
				message[i++] = c;
			}
			message_l = i;
			if (message_l >= 2) {
				message[message_l - 2] = 0;	// remove last two chars and null terminate
			}

			if (parse_mc66cde_standard_data_1_frame(&response, message, message_l)) {
				// if we can parse, send next request to meter
				en61107_uart_send_standard_data_2();
				break;
			}

			break;
		case UART_STATE_STANDARD_DATA_2:
			// get message buffer
			memset(message, 0, sizeof(message));
			i = 0;
			while (en61107_fifo_get(&c) && (i < EN61107_FRAME_L)) {
				message[i++] = c;
			}
			message_l = i;
			if (message_l >= 2) {
				message[message_l - 2] = 0;	// remove last two chars and null terminate
			}

			if (parse_mc66cde_standard_data_2_frame(&response, message, message_l)) {
				
				// Reset error backoff upon a fully successful read cycle
				error_backoff_count = 0;
				
				// tell user_main we got a serial
				if (en61107_meter_is_ready_cb && !meter_is_ready_cb_called) {
					meter_is_ready_cb_called = true;
					en61107_meter_is_ready_cb();
				}
#if defined(DEBUG) && !defined(DEBUG_NO_METER)
				else {
					os_printf("tried to call en61107_meter_is_ready_cb() before it was set - should not happen\n");
				}
#endif
				current_unix_time = get_unix_time();
				// format /sample/v2/serial/unix_time => val1=23&val2=val3&baz=blah
				memset(topic, 0, sizeof(topic));			// clear it
				tfp_snprintf(current_unix_time_string, 64, "%llu", current_unix_time);
				tfp_snprintf(topic, MQTT_TOPIC_L, "/sample/v2/%07u/%s", en61107_serial, current_unix_time_string);

				memset(message, 0, sizeof(message));			// clear it

				// heap size
				tfp_snprintf(key_value, MQTT_TOPIC_L, "heap=%u&", system_get_free_heap_size());
				append_key_value(message, sizeof(message), key_value);

				// meter program
				tfp_snprintf(key_value, MQTT_TOPIC_L, "program=%01u%01u%03u%02u%01u%02u%02u&", 
					response.meter_program.a, 
					response.meter_program.b, 
					response.meter_program.ccc, 
					response.meter_program.dd, 
					response.meter_program.e, 
					response.meter_program.ff, 
					response.meter_program.gg
				);
				append_key_value(message, sizeof(message), key_value);

#ifndef FLOW_METER
				// heating meter specific
				// flow temperature
				tfp_snprintf(key_value, MQTT_TOPIC_L, "t1=%s %s&", response.t1.value, response.t1.unit);
				append_key_value(message, sizeof(message), key_value);

				// return flow temperature
				tfp_snprintf(key_value, MQTT_TOPIC_L, "t2=%s %s&", response.t2.value, response.t2.unit);
				append_key_value(message, sizeof(message), key_value);

#ifndef MC_66B
				// t3 temperature
				tfp_snprintf(key_value, MQTT_TOPIC_L, "t3=%s %s&", response.t3.value, response.t3.unit);
				append_key_value(message, sizeof(message), key_value);
#endif	// MC_66B

				// calculated temperature difference
				tfp_snprintf(key_value, MQTT_TOPIC_L, "tdif=%s %s&", response.tdif.value, response.tdif.unit);
				append_key_value(message, sizeof(message), key_value);
#endif	// FLOW_METER

				// flow
				tfp_snprintf(key_value, MQTT_TOPIC_L, "flow1=%s %s&", response.flow1.value, response.flow1.unit);
				append_key_value(message, sizeof(message), key_value);

#ifndef FLOW_METER
				// current power
				tfp_snprintf(key_value, MQTT_TOPIC_L, "effect1=%s %s&", response.effect1.value, response.effect1.unit);
				append_key_value(message, sizeof(message), key_value);
#endif	// FLOW_METER

				// hours
				tfp_snprintf(key_value, MQTT_TOPIC_L, "hr=%s %s&", response.hr.value, response.hr.unit);
				append_key_value(message, sizeof(message), key_value);

				// volume
				tfp_snprintf(key_value, MQTT_TOPIC_L, "v1=%s %s&", response.v1.value, response.v1.unit);
				append_key_value(message, sizeof(message), key_value);

#ifndef FLOW_METER
				// power
				tfp_snprintf(key_value, MQTT_TOPIC_L, "e1=%s %s&", response.e1.value, response.e1.unit);
				append_key_value(message, sizeof(message), key_value);
#endif	// FLOW_METER

				memset(cleartext, 0, sizeof(cleartext));
				strncpy(cleartext, message, sizeof(cleartext) - 1);	// make a copy of message for later use
				memset(message, 0, sizeof(message));				// ...and clear it

				// encrypt and send
				message_l = encrypt_aes_hmac_combined(message, topic, strlen(topic), cleartext, strlen(cleartext) + 1);

				// and stop retransmission timeout timer, last data from meter
				os_timer_disarm(&en61107_receive_timeout_timer);

				// Queue the MQTT message as long as the client structure is initialized
				if (mqtt_client && mqtt_client->pCon && mqtt_client->mqtt_state.work_buffer) {
					if (message_l > 1) {
						MQTT_Publish(mqtt_client, topic, message, message_l, 2, 0);	// QoS level 2
					}
				}
				// tell user_main we got data from meter
				if (en61107_meter_sent_data_cb) {
					en61107_meter_sent_data_cb();
				}
				// change to last state - idle state
				en61107_uart_state = UART_STATE_NONE;
				en61107_request_num = 0;
			}
			break;
	}
}

ICACHE_FLASH_ATTR
void en61107_request_init() {
	fifo_head = 0;
	fifo_tail = 0;

	memset(&response, 0, sizeof(response));	// zero response before use

	en61107_uart_state = UART_STATE_NONE;

	en61107_request_num = 0;

	en61107_etx_received = false;
	is_in_error_recovery = false;
	error_backoff_count = 0;

	system_os_task(en61107_received_task, en61107_received_task_prio, en61107_received_task_queue, en61107_received_task_queue_length);
}

// helper function to pass mqtt client struct from user_main.c to here
ICACHE_FLASH_ATTR
void en61107_set_mqtt_client(MQTT_Client* client) {
	mqtt_client = client;
}

ICACHE_FLASH_ATTR
void en61107_register_meter_is_ready_cb(meter_is_ready_cb cb) {
	en61107_meter_is_ready_cb = cb;
}

// helper function to pass received kmp_serial to user_main.c
ICACHE_FLASH_ATTR
uint32_t en61107_get_received_serial() {
	return en61107_serial;
}

#ifdef FLOW_METER
ICACHE_FLASH_ATTR
uint32_t en61107_get_received_volume_l() {
#ifdef DEBUG_NO_METER
	return pseudo_data_debug_no_meter;
#else
	char v1_l_string[64];
	
	multiply_str_by_1000(response.v1.value, v1_l_string);
	return atoi(v1_l_string);
#endif
}
#else
// helper function to pass energy to user_main.c
ICACHE_FLASH_ATTR
uint32_t en61107_get_received_energy_kwh() {
#ifdef DEBUG_NO_METER
	return pseudo_data_debug_no_meter;
#else
	float e1_kwh;

	if (strncmp(response.e1.unit, "MWh", EN61107_UNIT_L) == 0) {
		tfp_vsscanf(response.e1.value, "%f", &e1_kwh);
		e1_kwh = e1_kwh * 1000.0;
		return (uint32_t)e1_kwh;
	}
	else {
		return atoi(response.e1.value);
	}
#endif
}
#endif	// FLOW_METER

ICACHE_FLASH_ATTR
void en61107_register_meter_sent_data_cb(meter_sent_data_cb cb) {
	en61107_meter_sent_data_cb = cb;
}

inline bool en61107_is_eod_char(uint8_t c) {
	if (en61107_eod == 0x03) {
		// we need to get the next char as well for this protocol
		if (c == 0x03) {
			en61107_etx_received = true;
			return false;
		}
		else if (en61107_etx_received) {
			en61107_etx_received = false;
			return true;
		}
	}
	else {
		if (c == en61107_eod) {
			return true;
		}
		else {
			return false;
		}
	}
	return false;
}

ICACHE_FLASH_ATTR
void en61107_request_send() {
#ifndef DEBUG_NO_METER
	char c;
	unsigned int i;

	// Ignore requests from main loop while we are in backoff penalty
	if (is_in_error_recovery) {
		return;
	}

	// Clear FIFO message buffer
	i = 0;
	while (en61107_fifo_get(&c) && (i <= EN61107_FRAME_L)) {
		i++;
	}

	en61107_request_num++;

	en61107_uart_send_en61107_ident();
#else
	char cleartext[MQTT_MESSAGE_L];
	char topic[MQTT_TOPIC_L];
	char message[EN61107_FRAME_L + AES_HMAC_OVERHEAD];
	int message_l;

	uint8_t sine_wave[256] = {
		0x80, 0x83, 0x86, 0x89, 0x8C, 0x90, 0x93, 0x96,
		0x99, 0x9C, 0x9F, 0xA2, 0xA5, 0xA8, 0xAB, 0xAE,
		0xB1, 0xB3, 0xB6, 0xB9, 0xBC, 0xBF, 0xC1, 0xC4,
		0xC7, 0xC9, 0xCC, 0xCE, 0xD1, 0xD3, 0xD5, 0xD8,
		0xDA, 0xDC, 0xDE, 0xE0, 0xE2, 0xE4, 0xE6, 0xE8,
		0xEA, 0xEB, 0xED, 0xEF, 0xF0, 0xF1, 0xF3, 0xF4,
		0xF5, 0xF6, 0xF8, 0xF9, 0xFA, 0xFA, 0xFB, 0xFC,
		0xFD, 0xFD, 0xFE, 0xFE, 0xFE, 0xFF, 0xFF, 0xFF,
		0xFF, 0xFF, 0xFF, 0xFF, 0xFE, 0xFE, 0xFE, 0xFD,
		0xFD, 0xFC, 0xFB, 0xFA, 0xFA, 0xF9, 0xF8, 0xF6,
		0xF5, 0xF4, 0xF3, 0xF1, 0xF0, 0xEF, 0xED, 0xEB,
		0xEA, 0xE8, 0xE6, 0xE4, 0xE2, 0xE0, 0xDE, 0xDC,
		0xDA, 0xD8, 0xD5, 0xD3, 0xD1, 0xCE, 0xCC, 0xC9,
		0xC7, 0xC4, 0xC1, 0xBF, 0xBC, 0xB9, 0xB6, 0xB3,
		0xB1, 0xAE, 0xAB, 0xA8, 0xA5, 0xA2, 0x9F, 0x9C,
		0x99, 0x96, 0x93, 0x90, 0x8C, 0x89, 0x86, 0x83,
		0x80, 0x7D, 0x7A, 0x77, 0x74, 0x70, 0x6D, 0x6A,
		0x67, 0x64, 0x61, 0x5E, 0x5B, 0x58, 0x55, 0x52,
		0x4F, 0x4D, 0x4A, 0x47, 0x44, 0x41, 0x3F, 0x3C,
		0x39, 0x37, 0x34, 0x32, 0x2F, 0x2D, 0x2B, 0x28,
		0x26, 0x24, 0x22, 0x20, 0x1E, 0x1C, 0x1A, 0x18,
		0x16, 0x15, 0x13, 0x11, 0x10, 0x0F, 0x0D, 0x0C,
		0x0B, 0x0A, 0x08, 0x07, 0x06, 0x06, 0x05, 0x04,
		0x03, 0x03, 0x02, 0x02, 0x02, 0x01, 0x01, 0x01,
		0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x03,
		0x03, 0x04, 0x05, 0x06, 0x06, 0x07, 0x08, 0x0A,
		0x0B, 0x0C, 0x0D, 0x0F, 0x10, 0x11, 0x13, 0x15,
		0x16, 0x18, 0x1A, 0x1C, 0x1E, 0x20, 0x22, 0x24,
		0x26, 0x28, 0x2B, 0x2D, 0x2F, 0x32, 0x34, 0x37,
		0x39, 0x3C, 0x3F, 0x41, 0x44, 0x47, 0x4A, 0x4D,
		0x4F, 0x52, 0x55, 0x58, 0x5B, 0x5E, 0x61, 0x64,
		0x67, 0x6A, 0x6D, 0x70, 0x74, 0x77, 0x7A, 0x7D
	};

	// clear data
	memset(message, 0, sizeof(message));
	memset(topic, 0, sizeof(topic));

	// fake serial for testing without meter
	en61107_serial = atoi(DEFAULT_METER_SERIAL);

	tfp_snprintf(topic, MQTT_TOPIC_L, "/sample/v2/%u/%llu", en61107_serial, get_unix_time());
	memset(cleartext, 0, sizeof(cleartext));
	pseudo_data_debug_no_meter = (get_unix_time() / 60) % 256;
	tfp_snprintf(cleartext, EN61107_FRAME_L, "heap=%u&program=1000000&t1=%u.00 C&t2=%u.00 C&tdif=%u.00 K&flow1=%u l/h&effect1=%u.0 kW&hr=%u h&v1=%u.00 m3&e1=%u kWh&",
		system_get_free_heap_size(),
		65 + ((sine_wave[pseudo_data_debug_no_meter] * 10) >> 8),		// t1
		45 + ((sine_wave[(pseudo_data_debug_no_meter + 128) & 0xff] * 10) >> 8),	// t2
		10,																// tdif
		100 + ((sine_wave[((pseudo_data_debug_no_meter + 64) * 7) & 0xff] * 50) >> 8),	// flow
		15 + ((sine_wave[pseudo_data_debug_no_meter] * 10) >> 8),		// effect1
		pseudo_data_debug_no_meter / 60,								// hr
		pseudo_data_debug_no_meter,										// v1
		pseudo_data_debug_no_meter										// e1
	);

	// tell user_main we got a serial
	if (en61107_meter_is_ready_cb && !meter_is_ready_cb_called) {
		meter_is_ready_cb_called = true;
		en61107_meter_is_ready_cb();
	}
	// tell user_main we got data from meter
	if (en61107_meter_sent_data_cb) {
		en61107_meter_sent_data_cb();
	}

	// encrypt and send
	message_l = encrypt_aes_hmac_combined(message, topic, strlen(topic), cleartext, strlen(cleartext) + 1);

	if (mqtt_client && mqtt_client->pCon && mqtt_client->mqtt_state.work_buffer) {
		// if mqtt_client is initialized
		MQTT_Publish(mqtt_client, topic, message, message_l, 2, 0);	// QoS level 2
	}
#endif
}

ICACHE_FLASH_ATTR
void en61107_uart_send_en61107_ident() {
	if (is_in_error_recovery) {
		return;
	}

	en61107_fifo_flush();

	en61107_eod = '\n';
	en61107_etx_received = false;

#ifndef DEBUG_NO_METER
	// 300 bps
	uart_set_baudrate(UART0, BIT_RATE_300);
	uart_set_word_length(UART0, SEVEN_BITS);
	uart_set_parity(UART0, EVEN_BITS);
	uart_set_stop_bits(UART0, TWO_STOP_BIT);
#endif

	// change to next state
	en61107_uart_state = UART_STATE_EN61107_IDENT;

	// and start retransmission timeout timer
	os_timer_disarm(&en61107_receive_timeout_timer);
	os_timer_setfn(&en61107_receive_timeout_timer, (os_timer_func_t *)en61107_receive_timeout_timer_func, NULL);
	os_timer_arm(&en61107_receive_timeout_timer, EN61107_IDENT_RESPONSE_TIMEOUT_MS, 0);

	strcpy(frame, "/?!\r\n");
	frame_length = strlen(frame);
#ifndef DEBUG_NO_METER
	uart0_tx_buffer(frame, frame_length);     // send request

	// reply is 300 bps, 7e1, not 7e2 as stated in standard
	uart_settings.baut_rate = BIT_RATE_300;
	uart_settings.stop_bits = ONE_STOP_BIT;
	// change uart settings after the data has been sent
	os_timer_disarm(&en61107_delayed_uart_change_setting_timer);
	os_timer_setfn(&en61107_delayed_uart_change_setting_timer, (os_timer_func_t *)en61107_delayed_uart_change_setting_timer_func, &uart_settings);
	os_timer_arm(&en61107_delayed_uart_change_setting_timer, EN61107_UART_SETTINGS_DELAY_MS, 0);
#endif
}

ICACHE_FLASH_ATTR
void en61107_uart_send_en61107() {
	if (is_in_error_recovery) {
		return;
	}

	en61107_eod = 0x03;
	en61107_etx_received = false;

#ifndef DEBUG_NO_METER
	// 300 bps
	uart_set_baudrate(UART0, BIT_RATE_300);
	uart_set_word_length(UART0, SEVEN_BITS);
	uart_set_parity(UART0, EVEN_BITS);
	uart_set_stop_bits(UART0, TWO_STOP_BIT);
#endif

	// change to next state
	en61107_uart_state = UART_STATE_EN61107;

	// and start retransmission timeout timer
	os_timer_disarm(&en61107_receive_timeout_timer);
	os_timer_setfn(&en61107_receive_timeout_timer, (os_timer_func_t *)en61107_receive_timeout_timer_func, NULL);
	os_timer_arm(&en61107_receive_timeout_timer, EN61107_DATA_RESPONSE_TIMEOUT_MS, 0);

	strcpy(frame, "\006000\r\n");
	frame_length = strlen(frame);
#ifndef DEBUG_NO_METER
	uart0_tx_buffer(frame, frame_length);     // send request

	// reply is 300 bps, 7e1, not 7e2 as stated in standard
	uart_settings.baut_rate = BIT_RATE_300;
	uart_settings.stop_bits = ONE_STOP_BIT;
	// change uart settings after the data has been sent
	os_timer_disarm(&en61107_delayed_uart_change_setting_timer);
	os_timer_setfn(&en61107_delayed_uart_change_setting_timer, (os_timer_func_t *)en61107_delayed_uart_change_setting_timer_func, &uart_settings);
	os_timer_arm(&en61107_delayed_uart_change_setting_timer, EN61107_UART_SETTINGS_DELAY_MS, 0);
#endif
}

ICACHE_FLASH_ATTR
void en61107_uart_send_standard_data_1() {
	if (is_in_error_recovery) {
		return;
	}

	en61107_eod = '\r';

#ifndef DEBUG_NO_METER
	// 300 bps
	uart_set_baudrate(UART0, BIT_RATE_300);
	uart_set_word_length(UART0, SEVEN_BITS);
	uart_set_parity(UART0, EVEN_BITS);
	uart_set_stop_bits(UART0, TWO_STOP_BIT);
#endif
	
	// change to next state
	en61107_uart_state = UART_STATE_STANDARD_DATA_1;

	// and start retransmission timeout timer
	os_timer_disarm(&en61107_receive_timeout_timer);
	os_timer_setfn(&en61107_receive_timeout_timer, (os_timer_func_t *)en61107_receive_timeout_timer_func, NULL);
	os_timer_arm(&en61107_receive_timeout_timer, EN61107_STANDARD_DATA_RESPONSE_TIMEOUT_MS, 0);

	strcpy(frame, "/#1\r");
	frame_length = strlen(frame);
#ifndef DEBUG_NO_METER
	uart0_tx_buffer(frame, frame_length);     // send request

	// reply is 1200 bps, 7e1, not 7e2 as stated in standard
	uart_settings.baut_rate = BIT_RATE_1200;
	uart_settings.stop_bits = ONE_STOP_BIT;
	// change uart settings after the data has been sent
	os_timer_disarm(&en61107_delayed_uart_change_setting_timer);
	os_timer_setfn(&en61107_delayed_uart_change_setting_timer, (os_timer_func_t *)en61107_delayed_uart_change_setting_timer_func, &uart_settings);
	os_timer_arm(&en61107_delayed_uart_change_setting_timer, EN61107_STANDARD_DATA_UART_SETTINGS_DELAY_MS, 0);
#endif
}

ICACHE_FLASH_ATTR
void en61107_uart_send_standard_data_2() {
	if (is_in_error_recovery) {
		return;
	}

	en61107_eod = '\r';

#ifndef DEBUG_NO_METER
	// 300 bps
	uart_set_baudrate(UART0, BIT_RATE_300);
	uart_set_word_length(UART0, SEVEN_BITS);
	uart_set_parity(UART0, EVEN_BITS);
	uart_set_stop_bits(UART0, TWO_STOP_BIT);
#endif
	
	// change to next state
	en61107_uart_state = UART_STATE_STANDARD_DATA_2;

	// and start retransmission timeout timer
	os_timer_disarm(&en61107_receive_timeout_timer);
	os_timer_setfn(&en61107_receive_timeout_timer, (os_timer_func_t *)en61107_receive_timeout_timer_func, NULL);
	os_timer_arm(&en61107_receive_timeout_timer, EN61107_STANDARD_DATA_RESPONSE_TIMEOUT_MS, 0);

	strcpy(frame, "/#2\r");
	frame_length = strlen(frame);
#ifndef DEBUG_NO_METER
	uart0_tx_buffer(frame, frame_length);     // send request

	// reply is 1200 bps, 7e1, not 7e2 as stated in standard
	uart_settings.baut_rate = BIT_RATE_1200;
	uart_settings.stop_bits = ONE_STOP_BIT;
	// change uart settings after the data has been sent
	os_timer_disarm(&en61107_delayed_uart_change_setting_timer);
	os_timer_setfn(&en61107_delayed_uart_change_setting_timer, (os_timer_func_t *)en61107_delayed_uart_change_setting_timer_func, &uart_settings);
	os_timer_arm(&en61107_delayed_uart_change_setting_timer, EN61107_STANDARD_DATA_UART_SETTINGS_DELAY_MS, 0);
#endif
}

#ifndef MC_66B
ICACHE_FLASH_ATTR
void en61107_uart_send_inst_values() {
	if (is_in_error_recovery) {
		return;
	}

	en61107_eod = '\r';

#ifndef DEBUG_NO_METER
	// 300 bps
	uart_set_baudrate(UART0, BIT_RATE_300);
	uart_set_word_length(UART0, SEVEN_BITS);
	uart_set_parity(UART0, EVEN_BITS);
	uart_set_stop_bits(UART0, TWO_STOP_BIT);
#endif

	// change to next state
	en61107_uart_state = UART_STATE_INST_VALUES;

	// and start retransmission timeout timer
	os_timer_disarm(&en61107_receive_timeout_timer);
	os_timer_setfn(&en61107_receive_timeout_timer, (os_timer_func_t *)en61107_receive_timeout_timer_func, NULL);
	os_timer_arm(&en61107_receive_timeout_timer, EN61107_INST_VALUES_RESPONSE_TIMEOUT_MS, 0);

	strcpy(frame, "/#C");
	frame_length = strlen(frame);
#ifndef DEBUG_NO_METER
	uart0_tx_buffer(frame, frame_length);     // send request

	// reply is 1200 bps, 7e1, not 7e2 as stated in standard
	uart_settings.baut_rate = BIT_RATE_1200;
	uart_settings.stop_bits = ONE_STOP_BIT;
	// change uart settings after the data has been sent
	os_timer_disarm(&en61107_delayed_uart_change_setting_timer);
	os_timer_setfn(&en61107_delayed_uart_change_setting_timer, (os_timer_func_t *)en61107_delayed_uart_change_setting_timer_func, &uart_settings);
	os_timer_arm(&en61107_delayed_uart_change_setting_timer, EN61107_INST_VALUES_UART_SETTINGS_DELAY_MS, 0);
#endif
}
#endif


// fifo
//ICACHE_FLASH_ATTR
inline size_t en61107_fifo_in_use() {
	return fifo_buffer_fill_count;
}

//ICACHE_FLASH_ATTR
inline bool en61107_fifo_put(char c) {
	if (en61107_fifo_in_use() != QUEUE_SIZE) {
		fifo_buffer[fifo_head++ % QUEUE_SIZE] = c;
		// wrap
		if (fifo_head == QUEUE_SIZE) {
			fifo_head = 0;
		}
		fifo_buffer_fill_count++;
		return true;
	}
	else {
		return false;
	}
}

ICACHE_FLASH_ATTR
bool en61107_fifo_get(char *c) {
	// Disable UART interrupts to prevent race conditions when updating fifo_buffer_fill_count.
	ETS_UART_INTR_DISABLE();
	if (fifo_buffer_fill_count != 0) {
		*c = fifo_buffer[fifo_tail++ % QUEUE_SIZE];
		if (fifo_tail == QUEUE_SIZE) {
			fifo_tail = 0;
		}
		fifo_buffer_fill_count--;
		ETS_UART_INTR_ENABLE();
		return true;
	}
	ETS_UART_INTR_ENABLE();
	return false;
}

ICACHE_FLASH_ATTR
bool en61107_fifo_snoop(char *c, unsigned int pos) {
	if (en61107_fifo_in_use() > (pos)) {
		*c = fifo_buffer[(fifo_tail + pos) % QUEUE_SIZE];
		return true;
	}
	else {
		return false;
	}
}

ICACHE_FLASH_ATTR
void en61107_fifo_flush() {
	ETS_UART_INTR_DISABLE();
	fifo_head = 0;
	fifo_tail = 0;
	fifo_buffer_fill_count = 0;
	// Clear ESP8266 hardware UART RX FIFO register
	SET_PERI_REG_MASK(UART_CONF0(UART0), UART_RXFIFO_RST);
	CLEAR_PERI_REG_MASK(UART_CONF0(UART0), UART_RXFIFO_RST);
	ETS_UART_INTR_ENABLE();
}

ICACHE_FLASH_ATTR
void en61107_request_destroy() {
	os_timer_disarm(&en61107_receive_timeout_timer);
	os_timer_disarm(&en61107_delayed_uart_change_setting_timer);
}
