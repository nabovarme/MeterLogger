#include <esp8266.h>
#include "driver/uart.h"

#include "unix_time.h"
#include "mqtt.h"
#include "tinyprintf.h"
#include "en61107.h"
#include "en61107_request.h"

#define QUEUE_SIZE 256

unsigned int en61107_serial = 0;
//unsigned int mqtt_lwt_flag = 0;

// fifo
volatile unsigned int fifo_head, fifo_tail;
volatile unsigned char fifo_buffer[QUEUE_SIZE];

// allocate frame to send
char frame[EN61107_FRAME_L];
unsigned int frame_length;
uint16_t register_list[8];

// allocate struct for response
en61107_response_t response;

static MQTT_Client *mqtt_client = NULL;	// initialize to NULL

static os_timer_t en61107_get_standard_data_timer;
static os_timer_t en61107_get_unknown_1_timer;
static os_timer_t en61107_get_unknown_2_timer;
static os_timer_t en61107_get_serial_byte_1_timer;
static os_timer_t en61107_get_serial_byte_2_timer;
static os_timer_t en61107_get_serial_byte_3_timer;
static os_timer_t en61107_get_serial_byte_4_timer;
static os_timer_t en61107_get_serial_byte_5_timer;
static os_timer_t en61107_get_unknown_3_timer;

static os_timer_t en61107_get_serial_timer;


static os_timer_t en61107_receive_timeout_timer;

unsigned int en61107_requests_sent;
unsigned int en61107_get_serial_data_state;

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
	int key_value_l;
	int topic_l;
	int message_l;

	if (en61107_get_serial_data_state == 0) {
		return;
	}

	memset(message, 0x00, EN61107_FRAME_L);			// clear message buffer
	i = 0;
	while (en61107_fifo_get(&c) && (i <= EN61107_FRAME_L)) {
		message[i++] = c;
	}
	message_l = i;

	message[message_l] = 0;		// null terminate
	if (message[0] == '@') {	// remove first char @
		memmove(message, message + 1, strlen(message));
		message_l--;
	}

	switch (en61107_get_serial_data_state) {
		case 1:
			en61107_serial = atoi(message);
			break;
		case 2:
			en61107_serial += atoi(message) << 8;
			break;
		case 3:
			en61107_serial += atoi(message) << 16;
			break;
		case 4:
			en61107_serial += atoi(message) << 24;
			break;
		case 5:
			en61107_serial += atoi(message) << 32;
			break;
		case 6:
			en61107_serial += atoi(message) << 40;
			break;
	}
	
	current_unix_time = (uint32)(get_unix_time());		// TODO before 2038 ,-)
//	if (current_unix_time) {	// only send mqtt if we got current time via ntp
   		// prepare for mqtt transmission if we got serial number from meter
        
   		// format /sample/v1/serial/unix_time => val1=23&val2=val3&baz=blah
		memset(topic, 0, sizeof(topic));			// clear it
		tfp_snprintf(current_unix_time_string, 64, "%u", (uint32_t)current_unix_time);
		tfp_snprintf(topic, MQTT_TOPIC_L, "/sample/v1/%u/%s", en61107_serial, current_unix_time_string);

   		//strcpy(message, "");	// clear it
		
		key_value_l = os_sprintf(key_value, "test=%s&", message);
		strncpy(message, key_value, key_value_l);
//	}
	
	if (mqtt_client) {
		// if mqtt_client is initialized
		if (message_l > 1) {
			MQTT_Publish(mqtt_client, topic, message, message_l, 2, 0);	// QoS level 2
		}
	}
	en61107_requests_sent = 0;	// reset retry counter
	en61107_get_serial_data_state = 0;
}

ICACHE_FLASH_ATTR
void en61107_request_init() {
	fifo_head = 0;
	fifo_tail = 0;

	en61107_requests_sent = 0;
	en61107_get_serial_data_state = 0;
	
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

ICACHE_FLASH_ATTR
void en61107_get_standard_data_timer_func() {
	uart_set_baudrate(UART0, BIT_RATE_300);
	uart_set_word_length(UART0, SEVEN_BITS);
	uart_set_parity(UART0, EVEN_BITS);
	uart_set_stop_bits(UART0, TWO_STOP_BIT);
	
	strncpy(frame, "/#2\r", EN61107_FRAME_L);
	frame_length = strlen(frame);
	uart0_tx_buffer(frame, frame_length);     // send request
}

ICACHE_FLASH_ATTR
void en61107_get_unknown_1_timer_func() {
	uart_set_baudrate(UART0, BIT_RATE_300);
	uart_set_word_length(UART0, SEVEN_BITS);
	uart_set_parity(UART0, EVEN_BITS);
	uart_set_stop_bits(UART0, TWO_STOP_BIT);
	
	strncpy(frame, "/MP1\r", EN61107_FRAME_L);
	frame_length = strlen(frame);
	uart0_tx_buffer(frame, frame_length);     // send request
}

ICACHE_FLASH_ATTR
void en61107_get_unknown_2_timer_func() {
	uart_set_baudrate(UART0, BIT_RATE_1200);
	uart_set_word_length(UART0, SEVEN_BITS);
	uart_set_parity(UART0, EVEN_BITS);
	uart_set_stop_bits(UART0, TWO_STOP_BIT);
	
	strncpy(frame, "M32\r", EN61107_FRAME_L);
	frame_length = strlen(frame);
	uart0_tx_buffer(frame, frame_length);     // send request
}

ICACHE_FLASH_ATTR
void en61107_get_serial_byte_1_timer_func() {
	uart_set_baudrate(UART0, BIT_RATE_2400);
	uart_set_word_length(UART0, SEVEN_BITS);
	uart_set_parity(UART0, EVEN_BITS);
	uart_set_stop_bits(UART0, TWO_STOP_BIT);
	
	strncpy(frame, "M200654\r", EN61107_FRAME_L);
	frame_length = strlen(frame);
	uart0_tx_buffer(frame, frame_length);     // send request

	en61107_get_serial_data_state = 1;
}

ICACHE_FLASH_ATTR
void en61107_get_serial_byte_2_timer_func() {
	uart_set_baudrate(UART0, BIT_RATE_2400);
	uart_set_word_length(UART0, SEVEN_BITS);
	uart_set_parity(UART0, EVEN_BITS);
	uart_set_stop_bits(UART0, TWO_STOP_BIT);
	
	strncpy(frame, "M200655\r", EN61107_FRAME_L);
	frame_length = strlen(frame);
	uart0_tx_buffer(frame, frame_length);     // send request

	en61107_get_serial_data_state = 2;
}

ICACHE_FLASH_ATTR
void en61107_get_serial_byte_3_timer_func() {
	uart_set_baudrate(UART0, BIT_RATE_2400);
	uart_set_word_length(UART0, SEVEN_BITS);
	uart_set_parity(UART0, EVEN_BITS);
	uart_set_stop_bits(UART0, TWO_STOP_BIT);
	
	strncpy(frame, "M200656\r", EN61107_FRAME_L);
	frame_length = strlen(frame);
	uart0_tx_buffer(frame, frame_length);     // send request

	en61107_get_serial_data_state = 3;
}

ICACHE_FLASH_ATTR
void en61107_get_serial_byte_4_timer_func() {
	uart_set_baudrate(UART0, BIT_RATE_2400);
	uart_set_word_length(UART0, SEVEN_BITS);
	uart_set_parity(UART0, EVEN_BITS);
	uart_set_stop_bits(UART0, TWO_STOP_BIT);
	
	strncpy(frame, "M200657\r", EN61107_FRAME_L);
	frame_length = strlen(frame);
	uart0_tx_buffer(frame, frame_length);     // send request

	en61107_get_serial_data_state = 4;
}

ICACHE_FLASH_ATTR
void en61107_get_serial_byte_5_timer_func() {
	uart_set_baudrate(UART0, BIT_RATE_2400);
	uart_set_word_length(UART0, SEVEN_BITS);
	uart_set_parity(UART0, EVEN_BITS);
	uart_set_stop_bits(UART0, TWO_STOP_BIT);
	
	strncpy(frame, "M200658\r", EN61107_FRAME_L);
	frame_length = strlen(frame);
	uart0_tx_buffer(frame, frame_length);     // send request

	en61107_get_serial_data_state = 5;
}

ICACHE_FLASH_ATTR
void en61107_get_serial_byte_6_timer_func() {
	uart_set_baudrate(UART0, BIT_RATE_2400);
	uart_set_word_length(UART0, SEVEN_BITS);
	uart_set_parity(UART0, EVEN_BITS);
	uart_set_stop_bits(UART0, TWO_STOP_BIT);
	
	strncpy(frame, "M200659\r", EN61107_FRAME_L);
	frame_length = strlen(frame);
	uart0_tx_buffer(frame, frame_length);     // send request

	en61107_get_serial_data_state = 6;
}

ICACHE_FLASH_ATTR
void en61107_get_unknown_3_timer_func() {
	uart_set_baudrate(UART0, BIT_RATE_2400);
	uart_set_word_length(UART0, SEVEN_BITS);
	uart_set_parity(UART0, EVEN_BITS);
	uart_set_stop_bits(UART0, TWO_STOP_BIT);
	
	strncpy(frame, "M906540659\r", EN61107_FRAME_L);
	frame_length = strlen(frame);
	uart0_tx_buffer(frame, frame_length);     // send request
}

ICACHE_FLASH_ATTR
void en61107_receive_timeout_timer_func() {
	if (en61107_requests_sent > 0) {
		// if no reply received, retransmit
		en61107_request_send();
	}
}

ICACHE_FLASH_ATTR
void en61107_request_send() {
	os_timer_disarm(&en61107_get_standard_data_timer);
	os_timer_setfn(&en61107_get_standard_data_timer, (os_timer_func_t *)en61107_get_standard_data_timer_func, NULL);
	os_timer_arm(&en61107_get_standard_data_timer, 0, 0);

	os_timer_disarm(&en61107_get_unknown_1_timer);
	os_timer_setfn(&en61107_get_unknown_1_timer, (os_timer_func_t *)en61107_get_unknown_1_timer_func, NULL);
	os_timer_arm(&en61107_get_unknown_1_timer, 3200, 0);

	os_timer_disarm(&en61107_get_unknown_2_timer);
	os_timer_setfn(&en61107_get_unknown_2_timer, (os_timer_func_t *)en61107_get_unknown_2_timer_func, NULL);
	os_timer_arm(&en61107_get_unknown_2_timer, (3200 + 2200), 0);

	os_timer_disarm(&en61107_get_serial_byte_1_timer);
	os_timer_setfn(&en61107_get_serial_byte_1_timer, (os_timer_func_t *)en61107_get_serial_byte_1_timer_func, NULL);
	os_timer_arm(&en61107_get_serial_byte_1_timer, (3200 + 2200 + 400), 0);

	os_timer_disarm(&en61107_get_serial_byte_2_timer);
	os_timer_setfn(&en61107_get_serial_byte_2_timer, (os_timer_func_t *)en61107_get_serial_byte_2_timer_func, NULL);
	os_timer_arm(&en61107_get_serial_byte_2_timer, (3200 + 2200 + 400 + 200), 0);

	os_timer_disarm(&en61107_get_serial_byte_3_timer);
	os_timer_setfn(&en61107_get_serial_byte_3_timer, (os_timer_func_t *)en61107_get_serial_byte_3_timer_func, NULL);
	os_timer_arm(&en61107_get_serial_byte_3_timer, (3200 + 2200 + 400 + 400), 0);

	os_timer_disarm(&en61107_get_serial_byte_4_timer);
	os_timer_setfn(&en61107_get_serial_byte_4_timer, (os_timer_func_t *)en61107_get_serial_byte_4_timer_func, NULL);
	os_timer_arm(&en61107_get_serial_byte_4_timer, (3200 + 2200 + 400 + 600), 0);

	os_timer_disarm(&en61107_get_serial_byte_4_timer);
	os_timer_setfn(&en61107_get_serial_byte_4_timer, (os_timer_func_t *)en61107_get_serial_byte_4_timer_func, NULL);
	os_timer_arm(&en61107_get_serial_byte_4_timer, (3200 + 2200 + 400 + 800), 0);

	os_timer_disarm(&en61107_get_unknown_3_timer);
	os_timer_setfn(&en61107_get_unknown_3_timer, (os_timer_func_t *)en61107_get_unknown_3_timer_func, NULL);
	os_timer_arm(&en61107_get_unknown_3_timer, (3200 + 2200 + 400 + 1000), 0);

	// start retransmission timeout timer
	os_timer_disarm(&en61107_receive_timeout_timer);
	os_timer_setfn(&en61107_receive_timeout_timer, (os_timer_func_t *)en61107_receive_timeout_timer_func, NULL);
	os_timer_arm(&en61107_receive_timeout_timer, 10000, 0);		// after 10 seconds
	
	en61107_requests_sent++;
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
	en61107_requests_sent = 0;	// reset retry counter
#endif
}

// fifo
ICACHE_FLASH_ATTR
unsigned int en61107_fifo_in_use() {
	return fifo_head - fifo_tail;
}

ICACHE_FLASH_ATTR
unsigned char en61107_fifo_put(unsigned char c) {
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
