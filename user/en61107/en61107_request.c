#include <esp8266.h>
#include "driver/uart.h"
#include "driver/uart_register.h"

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
#define EN61107_WAKEUP_STREAM_DELAY_MS 2600
#define EN61107_MAX_TIMEOUT_RETRIES 3
#define EN61107_RECOVERY_DELAY_MS 60000

extern bool fast_boot;

os_event_t en61107_received_task_queue[en61107_received_task_queue_length];

uint32_t en61107_serial = 0;
bool en61107_serial_set = false;
int8_t en61107_request_num;
meter_is_ready_cb en61107_meter_is_ready_cb = NULL;
bool meter_is_ready_cb_called = false;

meter_sent_data_cb en61107_meter_sent_data_cb = NULL;

//unsigned int mqtt_lwt_flag = 0;

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
static os_timer_t en61107_meter_wake_up_timer;
static uint8_t en61107_timeout_retries = 0;
static bool en61107_request_in_progress = false;

volatile en61107_uart_state_t en61107_uart_state;

UartDevice uart_settings;

volatile uint8_t en61107_eod;
bool en61107_etx_received;

#ifdef DEBUG_NO_METER
uint8_t pseudo_data_debug_no_meter = 0;
#endif

static void en61107_reset_receive_buffer(void) {
	ETS_UART_INTR_DISABLE();
	fifo_head = 0;
	fifo_tail = 0;
	fifo_buffer_fill_count = 0;
	en61107_etx_received = false;
	en61107_eod = '\n';

	// Discard bytes that arrived before the receive state was reset.
	SET_PERI_REG_MASK(UART_CONF0(UART0), UART_RXFIFO_RST);
	CLEAR_PERI_REG_MASK(UART_CONF0(UART0), UART_RXFIFO_RST);
	WRITE_PERI_REG(UART_INT_CLR(UART0), UART_RXFIFO_FULL_INT_CLR | UART_RXFIFO_TOUT_INT_CLR | UART_FRM_ERR_INT_CLR);
	ETS_UART_INTR_ENABLE();
}

static void en61107_schedule_recovery(uint32_t delay_ms) {
	os_timer_disarm(&en61107_receive_timeout_timer);
	os_timer_disarm(&en61107_delayed_uart_change_setting_timer);
	os_timer_disarm(&en61107_meter_wake_up_timer);

	en61107_uart_state = UART_STATE_NONE;
	memset(&response, 0, sizeof(response));
	en61107_reset_receive_buffer();
	en61107_request_in_progress = true;

	os_timer_setfn(&en61107_meter_wake_up_timer, (os_timer_func_t *)en61107_meter_wake_up_timer_func, NULL);
	os_timer_arm(&en61107_meter_wake_up_timer, delay_ms, 0);
}

ICACHE_FLASH_ATTR
void en61107_teardown_session(void) {
	// Stop pending serial activity without changing the meter's protocol state.
	os_timer_disarm(&en61107_receive_timeout_timer);
	os_timer_disarm(&en61107_delayed_uart_change_setting_timer);
	os_timer_disarm(&en61107_meter_wake_up_timer);
}

ICACHE_FLASH_ATTR
void en61107_receive_timeout_timer_func(void *arg) {
	led_blink();	// DEBUG

	// Retry the current protocol step twice, then restart from a clean 300-baud state.
	if (en61107_timeout_retries < EN61107_MAX_TIMEOUT_RETRIES) {
		en61107_timeout_retries++;
	}

	if (en61107_timeout_retries >= EN61107_MAX_TIMEOUT_RETRIES) {
		// Give the meter time to leave any previous session before starting over.
		en61107_schedule_recovery(EN61107_RECOVERY_DELAY_MS);
		return;
	}

	// Discard an incomplete or noisy response before retransmitting.
	en61107_reset_receive_buffer();
	switch (en61107_uart_state) {
		case UART_STATE_EN61107_IDENT:
			en61107_uart_send_en61107_ident();
			break;
		case UART_STATE_EN61107:
			memset(&response, 0, sizeof(response));
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
		default:
			en61107_schedule_recovery(EN61107_RECOVERY_DELAY_MS);
			break;
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

ICACHE_FLASH_ATTR
void en61107_meter_wake_up_timer_func(void *arg) {
#ifndef DEBUG_NO_METER
	unsigned int i;
#endif
	// Start recovery at the protocol's initial baud rate, independent of the previous session.
	if (en61107_uart_state == UART_STATE_NONE) {
#ifndef DEBUG_NO_METER
		// Always clear out stale hardware UART bytes before a fresh read sequence
		en61107_reset_receive_buffer();
		uart_set_baudrate(UART0, BIT_RATE_300);
		uart_set_word_length(UART0, SEVEN_BITS);
		uart_set_parity(UART0, EVEN_BITS);
		uart_set_stop_bits(UART0, TWO_STOP_BIT);
#endif

#ifdef MC_66B
		// MC-B meters stay awake and get confused by NULL streams on periodic reads
		if (!en61107_serial_set) {
#ifndef DEBUG_NO_METER
			// Send wake-up stream
			for (i = 0; i < 60; i++) {
				uart_tx_one_char(UART0, '\0');
			}
#endif
			// Wait for the wake-up stream to finish before sending /?!\r\n.
			os_timer_disarm(&en61107_meter_wake_up_timer);
			os_timer_setfn(&en61107_meter_wake_up_timer, (os_timer_func_t *)en61107_uart_send_en61107_ident, NULL);
			os_timer_arm(&en61107_meter_wake_up_timer, EN61107_WAKEUP_STREAM_DELAY_MS, 0);
		} else {
			// No wake-up stream needed if already set, just send ident directly
			en61107_uart_send_en61107_ident();
		}
#else
		// Standard MC meters go to sleep and ALWAYS require the NULL wake-up stream
#ifndef DEBUG_NO_METER
		// Send wake-up stream
		for (i = 0; i < 60; i++) {
			uart_tx_one_char(UART0, '\0');
		}
#endif
		// Wait for the wake-up stream to finish before sending /?!\r\n.
		os_timer_disarm(&en61107_meter_wake_up_timer);
		os_timer_setfn(&en61107_meter_wake_up_timer, (os_timer_func_t *)en61107_uart_send_en61107_ident, NULL);
		os_timer_arm(&en61107_meter_wake_up_timer, EN61107_WAKEUP_STREAM_DELAY_MS, 0);
#endif

	} else {
		// Normal flow
		en61107_uart_send_en61107_ident();
	}
}

static bool en61107_ident_response_ready(void) {
	unsigned int count;
	unsigned int i, j, k;
	unsigned int valid_start = QUEUE_SIZE;
	unsigned int valid_length = 0;
	unsigned int last_complete_line = 0;
	char c;
	bool valid;

	ETS_UART_INTR_DISABLE();
	count = fifo_buffer_fill_count;
	if (count > QUEUE_SIZE) {
		count = QUEUE_SIZE;
	}

	for (i = 0; i < count; i++) {
		if (fifo_buffer[(fifo_tail + i) % QUEUE_SIZE] != '/') {
			continue;
		}

		for (j = i + 1; j < count; j++) {
			c = fifo_buffer[(fifo_tail + j) % QUEUE_SIZE];
			if (c != '\n') {
				continue;
			}

			valid = (j >= i + 7 && fifo_buffer[(fifo_tail + j - 1) % QUEUE_SIZE] == '\r');
			for (k = i + 1; valid && k < j - 1; k++) {
				c = fifo_buffer[(fifo_tail + k) % QUEUE_SIZE];
				if (c < 0x20 || c > 0x7e) {
					valid = false;
				}
			}

			if (valid) {
				valid_start = i;
				valid_length = j - i + 1;
			}
			break;
		}

		if (valid_start != QUEUE_SIZE) {
			break;
		}
	}

	if (valid_start != QUEUE_SIZE) {
		// Keep the valid identification line and discard any surrounding noise.
		fifo_tail = (fifo_tail + valid_start) % QUEUE_SIZE;
		fifo_head = (fifo_tail + valid_length) % QUEUE_SIZE;
		fifo_buffer_fill_count = valid_length;
		ETS_UART_INTR_ENABLE();
		return true;
	}

	// Discard completed noise lines but retain a trailing partial line.
	for (i = 0; i < count; i++) {
		if (fifo_buffer[(fifo_tail + i) % QUEUE_SIZE] == '\n') {
			last_complete_line = i + 1;
		}
	}
	if (last_complete_line) {
		fifo_tail = (fifo_tail + last_complete_line) % QUEUE_SIZE;
		fifo_buffer_fill_count = count - last_complete_line;
	}

	ETS_UART_INTR_ENABLE();
	return false;
}

static bool en61107_readout_frame_ready(void) {
	unsigned int count;
	unsigned int i;
	bool stx_seen = false;
	bool ready = false;
	char c;

	// Ignore stale receive-task events until an EN61107 frame and its BCC are present.
	ETS_UART_INTR_DISABLE();
	count = fifo_buffer_fill_count;
	if (count > QUEUE_SIZE) {
		count = QUEUE_SIZE;
	}
	for (i = 0; i < count; i++) {
		c = fifo_buffer[(fifo_tail + i) % QUEUE_SIZE];
		if (c == 0x02) {
			stx_seen = true;
		}
		else if (stx_seen && c == 0x03 && i + 1 < count) {
			ready = true;
			break;
		}
	}
	ETS_UART_INTR_ENABLE();
	return ready;
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
			// Only acknowledge a complete, plausible identification line.
			if (en61107_ident_response_ready()) {
				en61107_uart_send_en61107();
			}
			break;
		case UART_STATE_EN61107:
			if (!en61107_readout_frame_ready()) {
				break;
			}
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
				if (parsed_serial != 0) {
					en61107_serial = parsed_serial;
					en61107_serial_set = true;
				}
			}

			// ALWAYS advance to next state regardless of en61107_serial_set
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
				en61107_timeout_retries = 0;
				en61107_request_in_progress = false;
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
				if (mqtt_client && mqtt_client->msgQueue.buf) {
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
	en61107_timeout_retries = 0;
	en61107_request_in_progress = false;

	en61107_etx_received = false;

	register_system_restart_defered_cb(en61107_teardown_session);

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

//ICACHE_FLASH_ATTR
inline bool en61107_is_eod_char(uint8_t c) {
	if (en61107_eod == 0x03) {
		// we need to get the next char as well for this protocol
		if (c == en61107_eod) {
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

	// Do not interrupt an active read or recovery cycle.
	if (en61107_request_in_progress || en61107_uart_state != UART_STATE_NONE) {
		return;
	}

	en61107_request_in_progress = true;

	// Clear FIFO message buffer
	i = 0;
	while (en61107_fifo_get(&c) && (i <= EN61107_FRAME_L)) {
		i++;
	}

	en61107_request_num++;

	// Start a new request from the initial baud rate when the meter is idle.
	os_timer_disarm(&en61107_meter_wake_up_timer);
	os_timer_setfn(&en61107_meter_wake_up_timer, (os_timer_func_t *)en61107_meter_wake_up_timer_func, NULL);
	os_timer_arm(&en61107_meter_wake_up_timer, 400, 0);
#else
	static char cleartext[MQTT_MESSAGE_L];
	char topic[MQTT_TOPIC_L];
	static char message[EN61107_FRAME_L + AES_HMAC_OVERHEAD];
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
	en61107_eod = '\n';
	en61107_etx_received = false;	// DEBUG: should be in en61107_is_eod_char() but does not work

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
	os_timer_arm(&en61107_receive_timeout_timer, 3000, 0);         // 3 seconds for UART_STATE_EN61107_IDENT

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
	os_timer_arm(&en61107_delayed_uart_change_setting_timer, 400, 0);	// 400 mS
#endif
}

ICACHE_FLASH_ATTR
void en61107_uart_send_en61107() {
	en61107_eod = 0x03;
	en61107_etx_received = false;	// DEBUG: should be in en61107_is_eod_char() but does not work

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
	os_timer_arm(&en61107_receive_timeout_timer, 8000, 0);         // 8 seconds for UART_STATE_EN61107

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
	os_timer_arm(&en61107_delayed_uart_change_setting_timer, 400, 0);	// 400 mS
#endif
}

ICACHE_FLASH_ATTR
void en61107_uart_send_standard_data_1() {
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
	os_timer_arm(&en61107_receive_timeout_timer, 3000, 0);         // 3 seconds for UART_STATE_STANDARD_DATA_1

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
	os_timer_arm(&en61107_delayed_uart_change_setting_timer, 220, 0);	// 220 mS
#endif
}

ICACHE_FLASH_ATTR
void en61107_uart_send_standard_data_2() {
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
	os_timer_arm(&en61107_receive_timeout_timer, 3000, 0);         // 3 seconds for UART_STATE_STANDARD_DATA_2

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
	os_timer_arm(&en61107_delayed_uart_change_setting_timer, 220, 0);	// 220 mS
#endif
}

#ifndef MC_66B
ICACHE_FLASH_ATTR
void en61107_uart_send_inst_values() {
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
	os_timer_arm(&en61107_receive_timeout_timer, 3200, 0);         // 3.2 seconds for UART_STATE_INST_VALUES

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
	os_timer_arm(&en61107_delayed_uart_change_setting_timer, 300, 0);	// 300 mS
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
void en61107_request_destroy() {
	os_timer_disarm(&en61107_receive_timeout_timer);
	os_timer_disarm(&en61107_delayed_uart_change_setting_timer);
	os_timer_disarm(&en61107_meter_wake_up_timer);
}
