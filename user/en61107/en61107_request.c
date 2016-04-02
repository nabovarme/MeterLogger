#include <esp8266.h>
#include "driver/uart.h"

#include "unix_time.h"
#include "mqtt.h"
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

static os_timer_t en61107_get_data_timer;
static os_timer_t en61107_get_register_timer;

static os_timer_t en61107_receive_timeout_timer;

unsigned int en61107_requests_sent;

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

	memset(message, 0x00, EN61107_FRAME_L);			// clear message buffer
	i = 0;
	while (en61107_fifo_get(&c) && (i <= EN61107_FRAME_L)) {
		message[i++] = c;
	}
	message_l = i;
	
	current_unix_time = (uint32)(get_unix_time());		// TODO before 2038 ,-)
    if (current_unix_time) {	// only send mqtt if we got current time via ntp
   		// prepare for mqtt transmission if we got serial number from meter
        
   		// format /sample/v1/serial/unix_time => val1=23&val2=val3&baz=blah
   		os_sprintf(current_unix_time_string, "%llu", current_unix_time);
   		topic_l = os_sprintf(topic, "/sample/v1/%u/%s", atoi(DEFAULT_METER_SERIAL), current_unix_time_string);

   		//strcpy(message, "");	// clear it
		
		key_value_l = os_sprintf(key_value, "test=%s&", message);
		strncpy(message, key_value, key_value_l);
	}
	
	if (mqtt_client) {
		// if mqtt_client is initialized
		if (message_l > 1) {
			MQTT_Publish(mqtt_client, topic, message, message_l, 0, 0);
		}
	}
	en61107_requests_sent = 0;	// reset retry counter
}

ICACHE_FLASH_ATTR
void en61107_request_init() {
	fifo_head = 0;
	fifo_tail = 0;

	en61107_requests_sent = 0;
	
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
void en61107_get_data_timer_func() {
	// change to 300 bps
	uart_init(BIT_RATE_300, BIT_RATE_300);
	
	frame_length = en61107_get_data(frame);
	uart0_tx_buffer(frame, frame_length);     // send kmp request
	
	// reply comes at 300 bps 7e1 NOT 7e2 as stated in docs
	//uart_init(BIT_RATE_1200, BIT_RATE_1200);
}

ICACHE_FLASH_ATTR
void en61107_get_register_timer_func() {
    // get registers
    // prepare frame
    register_list[0] = 0x3c;    // heat energy (E1)
    register_list[1] = 0x44;    // volume register (V1)
    register_list[2] = 0x3EC;   // operational hour counter (HR)
    register_list[3] = 0x56;    // current flow temperature (T1)
    register_list[4] = 0x57;    // current return flow temperature (T2)
    register_list[5] = 0x59;    // current temperature difference (T1-T2)
    register_list[6] = 0x4A;    // current flow in flow (FLOW1)
    register_list[7] = 0x50;    // current power calculated on the basis of V1-T1-T2 (EFFEKT1)
    frame_length = en61107_get_register(frame, register_list, 8);
	
    // send frame
    uart0_tx_buffer(frame, frame_length);     // send kmp request
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
    os_timer_disarm(&en61107_get_data_timer);
    os_timer_setfn(&en61107_get_data_timer, (os_timer_func_t *)en61107_get_data_timer_func, NULL);
    os_timer_arm(&en61107_get_data_timer, 0, 0);		// now
	
    os_timer_disarm(&en61107_get_register_timer);
    os_timer_setfn(&en61107_get_register_timer, (os_timer_func_t *)en61107_get_register_timer_func, NULL);
    os_timer_arm(&en61107_get_register_timer, 4500, 0);		// after 4.5 seconds
	
	// start retransmission timeout timer
    os_timer_disarm(&en61107_receive_timeout_timer);
    os_timer_setfn(&en61107_receive_timeout_timer, (os_timer_func_t *)en61107_receive_timeout_timer_func, NULL);
    os_timer_arm(&en61107_receive_timeout_timer, 6000, 0);		// after 6 seconds
	
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
		MQTT_Publish(mqtt_client, topic, message, message_l, 0, 0);
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
