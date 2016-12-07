#include <esp8266.h>
#include "driver/uart.h"

#include "unix_time.h"
#include "mqtt.h"
#include "tinyprintf.h"
#include "en61107.h"
#include "en61107_request.h"
//#include "led.h"

#define QUEUE_SIZE 256

unsigned int en61107_serial = 0;
//unsigned int mqtt_lwt_flag = 0;

// fifo
volatile unsigned int fifo_head, fifo_tail;
volatile unsigned char fifo_buffer[QUEUE_SIZE];

// allocate frame to send
char frame[EN61107_FRAME_L];
unsigned int frame_length;

// allocate struct for response
en61107_response_t response;

static MQTT_Client *mqtt_client = NULL;	// initialize to NULL

static os_timer_t en61107_receive_timeout_timer;
static os_timer_t en61107_delayed_uart_change_setting_timer;

enum {
	UART_STATE_NONE,
	UART_STATE_STANDARD_DATA,
	UART_STATE_UNKNOWN_1,
	UART_STATE_UNKNOWN_2,
	UART_STATE_SERIAL_BYTE_1,
	UART_STATE_SERIAL_BYTE_2,
	UART_STATE_SERIAL_BYTE_3,
	UART_STATE_SERIAL_BYTE_4,
	UART_STATE_SERIAL_BYTE_5,
	UART_STATE_SERIAL_BYTE_6,
	UART_STATE_UNKNOWN_3,
	UART_STATE_UNKNOWN_4,
	UART_STATE_EN61107,
	UART_STATE_INST_VALUES
} en61107_uart_state;
UartDevice uart_settings;

volatile uint8_t en61107_eod;
bool en61107_etx_received;

// define en61107_received_task() first
ICACHE_FLASH_ATTR
static void en61107_received_task(os_event_t *events) {
	unsigned char c;
	unsigned int i;
	uint64_t current_unix_time;
	char current_unix_time_string[64];	// BUGFIX var
	char key_value[128];
	unsigned char topic[128];
	unsigned char message[EN61107_FRAME_L];
	int32_t mc66cde_temp_array[8];			// DEBUG: should be removed
	int key_value_l;
	int topic_l;
	int message_l;

	memset(message, 0x00, EN61107_FRAME_L);			// clear message buffer
	i = 0;
	while (en61107_fifo_get(&c) && (i <= EN61107_FRAME_L)) {
		message[i++] = c;
	}
	message_l = i;

	switch (en61107_uart_state) {
		case UART_STATE_STANDARD_DATA:
			// 300 bps
			uart_set_baudrate(UART0, BIT_RATE_300);
			uart_set_word_length(UART0, SEVEN_BITS);
			uart_set_parity(UART0, EVEN_BITS);
			uart_set_stop_bits(UART0, TWO_STOP_BIT);
	
			strncpy(frame, "/MP1\r", EN61107_FRAME_L);
			frame_length = strlen(frame);
			uart0_tx_buffer(frame, frame_length);     // send request

			// reply is 300 bps, 7e1, not 7e2 as stated in standard
			uart_settings.baut_rate = BIT_RATE_300;
			uart_settings.stop_bits = ONE_STOP_BIT;
			// change uart settings after the data has been sent
			os_timer_disarm(&en61107_delayed_uart_change_setting_timer);
			os_timer_setfn(&en61107_delayed_uart_change_setting_timer, (os_timer_func_t *)en61107_delayed_uart_change_setting_timer_func, &uart_settings);
			os_timer_arm(&en61107_delayed_uart_change_setting_timer, 380, 0);	// 380 mS
			en61107_uart_state = UART_STATE_UNKNOWN_1;
			break;
		case UART_STATE_UNKNOWN_1:
			// 1200 bps, 7e1, not 7e2 as stated in standard
			uart_set_baudrate(UART0, BIT_RATE_1200);
			uart_set_word_length(UART0, SEVEN_BITS);
			uart_set_parity(UART0, EVEN_BITS);
			uart_set_stop_bits(UART0, ONE_STOP_BIT);
	
			strncpy(frame, "M32\r", EN61107_FRAME_L);
			frame_length = strlen(frame);
			uart0_tx_buffer(frame, frame_length);     // send request

			// reply is 1200 bps

			en61107_uart_state = UART_STATE_UNKNOWN_2;
			break;
		case UART_STATE_UNKNOWN_2:
			// 2400 bps, 7e2
			uart_set_baudrate(UART0, BIT_RATE_2400);
			uart_set_word_length(UART0, SEVEN_BITS);
			uart_set_parity(UART0, EVEN_BITS);
			uart_set_stop_bits(UART0, TWO_STOP_BIT);
	
			strncpy(frame, "M200654\r", EN61107_FRAME_L);
			frame_length = strlen(frame);
			uart0_tx_buffer(frame, frame_length);     // send request

			// reply is 2400 bps, 7e2

			en61107_uart_state = UART_STATE_SERIAL_BYTE_1;
			break;
		case UART_STATE_SERIAL_BYTE_1:
			message[message_l] = 0;		// null terminate
			if (message[0] == '@') {	// remove first char @
				memmove(message, message + 1, strlen(message));
				message_l--;
			}
			en61107_serial = atoi(message);

			// 2400 bps, 7e2
			uart_set_baudrate(UART0, BIT_RATE_2400);
			uart_set_word_length(UART0, SEVEN_BITS);
			uart_set_parity(UART0, EVEN_BITS);
			uart_set_stop_bits(UART0, TWO_STOP_BIT);
	
			strncpy(frame, "M200655\r", EN61107_FRAME_L);
			frame_length = strlen(frame);
			uart0_tx_buffer(frame, frame_length);     // send request

			// reply is 2400 bps, 7e2

			en61107_uart_state = UART_STATE_SERIAL_BYTE_2;
			break;
		case UART_STATE_SERIAL_BYTE_2:
			message[message_l] = 0;		// null terminate
			if (message[0] == '@') {	// remove first char @
				memmove(message, message + 1, strlen(message));
				message_l--;
			}
			en61107_serial += atoi(message) << 8;

			// 2400 bps, 7e2
			uart_set_baudrate(UART0, BIT_RATE_2400);
			uart_set_word_length(UART0, SEVEN_BITS);
			uart_set_parity(UART0, EVEN_BITS);
			uart_set_stop_bits(UART0, TWO_STOP_BIT);
	
			strncpy(frame, "M200656\r", EN61107_FRAME_L);
			frame_length = strlen(frame);
			uart0_tx_buffer(frame, frame_length);     // send request

			// reply is 2400 bps, 7e2

			en61107_uart_state = UART_STATE_SERIAL_BYTE_3;
			break;
		case UART_STATE_SERIAL_BYTE_3:
			message[message_l] = 0;		// null terminate
			if (message[0] == '@') {	// remove first char @
				memmove(message, message + 1, strlen(message));
				message_l--;
			}
			en61107_serial += atoi(message) << 16;

			// 2400 bps, 7e2
			uart_set_baudrate(UART0, BIT_RATE_2400);
			uart_set_word_length(UART0, SEVEN_BITS);
			uart_set_parity(UART0, EVEN_BITS);
			uart_set_stop_bits(UART0, TWO_STOP_BIT);
	
			strncpy(frame, "M200657\r", EN61107_FRAME_L);
			frame_length = strlen(frame);
			uart0_tx_buffer(frame, frame_length);     // send request

			// reply is 2400 bps, 7e2

			en61107_uart_state = UART_STATE_SERIAL_BYTE_4;
			break;
		case UART_STATE_SERIAL_BYTE_4:
			message[message_l] = 0;		// null terminate
			if (message[0] == '@') {	// remove first char @
				memmove(message, message + 1, strlen(message));
				message_l--;
			}
			en61107_serial += atoi(message) << 24;

			// 2400 bps, 7e2
			uart_set_baudrate(UART0, BIT_RATE_2400);
			uart_set_word_length(UART0, SEVEN_BITS);
			uart_set_parity(UART0, EVEN_BITS);
			uart_set_stop_bits(UART0, TWO_STOP_BIT);
	
			strncpy(frame, "M200658\r", EN61107_FRAME_L);
			frame_length = strlen(frame);
			uart0_tx_buffer(frame, frame_length);     // send request

			// reply is 2400 bps, 7e2

			en61107_uart_state = UART_STATE_SERIAL_BYTE_5;
			break;
		case UART_STATE_SERIAL_BYTE_5:
			message[message_l] = 0;		// null terminate
			if (message[0] == '@') {	// remove first char @
				memmove(message, message + 1, strlen(message));
				message_l--;
			}
			en61107_serial += atoi(message) << 32;

			// 2400 bps, 7e2
			uart_set_baudrate(UART0, BIT_RATE_2400);
			uart_set_word_length(UART0, SEVEN_BITS);
			uart_set_parity(UART0, EVEN_BITS);
			uart_set_stop_bits(UART0, TWO_STOP_BIT);
	
			strncpy(frame, "M200659\r", EN61107_FRAME_L);
			frame_length = strlen(frame);
			uart0_tx_buffer(frame, frame_length);     // send request

			// reply is 2400 bps, 7e2

			en61107_uart_state = UART_STATE_SERIAL_BYTE_6;
			break;
		case UART_STATE_SERIAL_BYTE_6:
			message[message_l] = 0;		// null terminate
			if (message[0] == '@') {	// remove first char @
				memmove(message, message + 1, strlen(message));
				message_l--;
			}
			en61107_serial += atoi(message) << 40;

			// 2400 bps, 7e2
			uart_set_baudrate(UART0, BIT_RATE_2400);
			uart_set_word_length(UART0, SEVEN_BITS);
			uart_set_parity(UART0, EVEN_BITS);
			uart_set_stop_bits(UART0, TWO_STOP_BIT);
	
			strncpy(frame, "M906540659\r", EN61107_FRAME_L);
			frame_length = strlen(frame);
			uart0_tx_buffer(frame, frame_length);     // send request

			// reply is 2400 bps, 7e2

			en61107_uart_state = UART_STATE_UNKNOWN_3;
			break;
		case UART_STATE_UNKNOWN_3:
			// 2400 bps, 7e2
			uart_set_baudrate(UART0, BIT_RATE_2400);
			uart_set_word_length(UART0, SEVEN_BITS);
			uart_set_parity(UART0, EVEN_BITS);
			uart_set_stop_bits(UART0, TWO_STOP_BIT);
	
			strncpy(frame, "*\r", EN61107_FRAME_L);
			frame_length = strlen(frame);
			uart0_tx_buffer(frame, frame_length);     // send request

			// reply is 2400 bps, 7e2

			en61107_uart_state = UART_STATE_UNKNOWN_4;
			break;
		case UART_STATE_UNKNOWN_4:
			en61107_eod = 0x03;
			en61107_etx_received = false;	// DEBUG: should be in en61107_is_eod_char() but does not work

			// 300 bps
			uart_set_baudrate(UART0, BIT_RATE_300);
			uart_set_word_length(UART0, SEVEN_BITS);
			uart_set_parity(UART0, EVEN_BITS);
			uart_set_stop_bits(UART0, TWO_STOP_BIT);

			strncpy(frame, "/?!\r\n", EN61107_FRAME_L);
			frame_length = strlen(frame);
			uart0_tx_buffer(frame, frame_length);     // send request

			// reply is 300 bps, 7e1, not 7e2 as stated in standard
			uart_settings.baut_rate = BIT_RATE_300;
			uart_settings.stop_bits = ONE_STOP_BIT;
			// change uart settings after the data has been sent
			os_timer_disarm(&en61107_delayed_uart_change_setting_timer);
			os_timer_setfn(&en61107_delayed_uart_change_setting_timer, (os_timer_func_t *)en61107_delayed_uart_change_setting_timer_func, &uart_settings);
			os_timer_arm(&en61107_delayed_uart_change_setting_timer, 400, 0);	// 400 mS

			en61107_uart_state = UART_STATE_EN61107;
			break;
		case UART_STATE_EN61107:
			en61107_eod = '\r';

			/*
			message[message_l] = 0;		// null terminate
			if (message[0] == '@') {	// remove first char @
				memmove(message, message + 1, strlen(message));
				message_l--;
			}

			if (parse_en61107_frame(&response, message, message_l) == 0) {
				return;
			}
			*/

			// 300 bps
			uart_set_baudrate(UART0, BIT_RATE_300);
			uart_set_word_length(UART0, SEVEN_BITS);
			uart_set_parity(UART0, EVEN_BITS);
			uart_set_stop_bits(UART0, TWO_STOP_BIT);

			strncpy(frame, "/#C", EN61107_FRAME_L);
			frame_length = strlen(frame);
			uart0_tx_buffer(frame, frame_length);     // send request

			// reply is 1200 bps, 7e1, not 7e2 as stated in standard
			uart_settings.baut_rate = BIT_RATE_1200;
			uart_settings.stop_bits = ONE_STOP_BIT;
			// change uart settings after the data has been sent
			os_timer_disarm(&en61107_delayed_uart_change_setting_timer);
			os_timer_setfn(&en61107_delayed_uart_change_setting_timer, (os_timer_func_t *)en61107_delayed_uart_change_setting_timer_func, &uart_settings);
			os_timer_arm(&en61107_delayed_uart_change_setting_timer, 300, 0);	// 300 mS

			en61107_uart_state = UART_STATE_INST_VALUES;
			break;
		case UART_STATE_INST_VALUES:
			message[message_l - 2] = 0;		// remove last two chars and null terminate

			memset(mc66cde_temp_array, 0, sizeof(mc66cde_temp_array));
			if (parse_mc66cde_frame(mc66cde_temp_array, message)) {
				led_on();

				current_unix_time = (uint32)(get_unix_time());		// TODO before 2038 ,-)
				if (current_unix_time) {	// only send mqtt if we got current time via ntp
        
   					// format /sample/v1/serial/unix_time => val1=23&val2=val3&baz=blah
					memset(topic, 0, sizeof(topic));			// clear it
					tfp_snprintf(current_unix_time_string, 64, "%u", (uint32_t)current_unix_time);
					tfp_snprintf(topic, MQTT_TOPIC_L, "/sample/v1/%u/%s", en61107_serial, current_unix_time_string);

					memset(message, 0, sizeof(char[EN61107_FRAME_L]));			// clear it

					// heap size
					tfp_snprintf(key_value, MQTT_TOPIC_L, "heap=%u&", system_get_free_heap_size());
					strcat(message, key_value);

					// heating meter specific
					// flow temperature
					tfp_snprintf(key_value, MQTT_TOPIC_L, "t1=%d C&", mc66cde_temp_array[0]);
					strcat(message, key_value);
        	
					// return flow temperature
					tfp_snprintf(key_value, MQTT_TOPIC_L, "t2=%d C&", mc66cde_temp_array[2]);
					strcat(message, key_value);

					message_l = strlen(message);				

					if (mqtt_client) {
						// if mqtt_client is initialized
						if (message_l > 1) {
							MQTT_Publish(mqtt_client, topic, message, message_l, 2, 0);	// QoS level 2
						}
					}
					en61107_uart_state = UART_STATE_NONE;
				}
			}
			break;
	}
}

ICACHE_FLASH_ATTR
void en61107_request_init() {
	fifo_head = 0;
	fifo_tail = 0;

	en61107_uart_state = UART_STATE_NONE;

	en61107_etx_received = false;
	
	system_os_task(en61107_received_task, en61107_received_task_prio, en61107_received_task_queue, en61107_received_task_queue_length);
}

// helper function to pass mqtt client struct from user_main.c to here
ICACHE_FLASH_ATTR
void en61107_set_mqtt_client(MQTT_Client* client) {
	mqtt_client = client;
}

// helper function to pass received kmp_serial to user_main.c
ICACHE_FLASH_ATTR
unsigned int en61107_get_received_serial() {
	return en61107_serial;
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
//			en61107_etx_received = false;	// DEBUG: should really be here
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
}

ICACHE_FLASH_ATTR
void en61107_receive_timeout_timer_func() {
	if (en61107_uart_state != UART_STATE_NONE) {
		// if no reply received, retransmit
		en61107_request_send();
	}
}

ICACHE_FLASH_ATTR
void en61107_delayed_uart_change_setting_timer_func(UartDevice *uart_settings) {
	uart_set_baudrate(UART0, uart_settings->baut_rate);
//	uart_set_word_length(UART0, SEVEN_BITS);
//	uart_set_parity(UART0, EVEN_BITS);
	uart_set_stop_bits(UART0, uart_settings->stop_bits);
}


ICACHE_FLASH_ATTR
void en61107_request_send() {
	en61107_eod = '\r';

	// 300 bps
	uart_set_baudrate(UART0, BIT_RATE_300);
	uart_set_word_length(UART0, SEVEN_BITS);
	uart_set_parity(UART0, EVEN_BITS);
	uart_set_stop_bits(UART0, TWO_STOP_BIT);
	
	strncpy(frame, "/#2\r", EN61107_FRAME_L);
	frame_length = strlen(frame);
	uart0_tx_buffer(frame, frame_length);     // send request

	// reply is 1200 bps, 7e1, not 7e2 as stated in standard
	uart_settings.baut_rate = BIT_RATE_1200;
	uart_settings.stop_bits = ONE_STOP_BIT;
	// change uart settings after the data has been sent
	os_timer_disarm(&en61107_delayed_uart_change_setting_timer);
	os_timer_setfn(&en61107_delayed_uart_change_setting_timer, (os_timer_func_t *)en61107_delayed_uart_change_setting_timer_func, &uart_settings);
	os_timer_arm(&en61107_delayed_uart_change_setting_timer, 220, 0);	// 220 mS

	// start retransmission timeout timer
	os_timer_disarm(&en61107_receive_timeout_timer);
	os_timer_setfn(&en61107_receive_timeout_timer, (os_timer_func_t *)en61107_receive_timeout_timer_func, NULL);
	os_timer_arm(&en61107_receive_timeout_timer, 16000, 0);         // after 16 seconds

	en61107_uart_state = UART_STATE_STANDARD_DATA;
#ifdef DEBUG_NO_METER
	unsigned char topic[128];
	unsigned char message[EN61107_FRAME_L];
	int topic_l;
	int message_l;
	
	// fake serial for testing without meter
	en61107_serial = atoi(DEFAULT_METER_SERIAL);
	
	topic_l = os_sprintf(topic, "/sample/v1/%u/%u", en61107_serial, get_unix_time());
	message_l = os_sprintf(message, "heap=20000&t1=25.00 C&t2=15.00 C&tdif=10.00 K&flow1=0 l/h&effect1=0.0 kW&hr=0 h&v1=0.00 m3&e1=0 kWh&");
	
	if (mqtt_client) {
		// if mqtt_client is initialized
		MQTT_Publish(mqtt_client, topic, message, message_l, 2, 0);	// QoS level 2
	}
#endif
}

// fifo
ICACHE_FLASH_ATTR
unsigned int en61107_fifo_in_use() {
	return fifo_head - fifo_tail;
}

//ICACHE_FLASH_ATTR
inline unsigned char en61107_fifo_put(unsigned char c) {
	if (en61107_fifo_in_use() != QUEUE_SIZE) {
		fifo_buffer[fifo_head++ % QUEUE_SIZE] = c;
		// wrap
		if (fifo_head == QUEUE_SIZE) {
			fifo_head = 0;
		}
		return 1;
	}
	else {
		return 0;
	}
}

ICACHE_FLASH_ATTR
unsigned char en61107_fifo_get(unsigned char *c) {
	if (en61107_fifo_in_use() != 0) {
		*c = fifo_buffer[fifo_tail++ % QUEUE_SIZE];
		// wrap
		if (fifo_tail == QUEUE_SIZE) {
			fifo_tail = 0;
		}
		return 1;
	}
	else {
		return 0;
	}
}

ICACHE_FLASH_ATTR
unsigned char en61107_fifo_snoop(unsigned char *c, unsigned int pos) {
	if (en61107_fifo_in_use() > (pos)) {
        *c = fifo_buffer[(fifo_tail + pos) % QUEUE_SIZE];
		return 1;
	}
	else {
		return 0;
	}
}

