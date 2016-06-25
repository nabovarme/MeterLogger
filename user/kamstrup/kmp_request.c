#include <esp8266.h>
#include "driver/uart.h"
#include "unix_time.h"
#include "mqtt.h"
#include "tinyprintf.h"
#include "kmp.h"
#include "kmp_request.h"
#include "config.h"
#include "crypto/crypto.h"
#include "crypto/aes.h"

#define QUEUE_SIZE 256

uint32_t kmp_serial = 0;
//unsigned int mqtt_lwt_flag = 0;

// fifo
volatile unsigned int fifo_head, fifo_tail;
volatile unsigned char fifo_buffer[QUEUE_SIZE];

// allocate frame to send
unsigned char frame[KMP_FRAME_L];
unsigned int frame_length;
uint16_t register_list[8];

// allocate struct for response
kmp_response_t response;

static MQTT_Client *mqtt_client = NULL;	// initialize to NULL

static os_timer_t kmp_get_serial_timer;
static os_timer_t kmp_get_register_timer;

static os_timer_t kmp_receive_timeout_timer;

unsigned int kmp_requests_sent;

ICACHE_FLASH_ATTR
static void kmp_received_task(os_event_t *events) {
	unsigned char c;
	unsigned int i;
	uint64_t current_unix_time;
	char current_unix_time_string[64];	// BUGFIX var
	char key_value[256];
	_align_32_bit unsigned char topic[MQTT_TOPIC_L];
	_align_32_bit unsigned char message[KMP_FRAME_L];
	int message_l;
		
	// vars for aes encryption
	_align_32_bit uint8_t cleartext[KMP_FRAME_L];	// is casted in crypto lib

    // allocate struct for response
    kmp_response_t response;
    unsigned char kmp_unit_string[16];
	unsigned char kmp_value_string[64];

	//ETS_UART_INTR_DISABLE();

	memset(message, 0x00, KMP_FRAME_L);			// clear message buffer
	i = 0;
	while (kmp_fifo_get(&c) && (i <= KMP_FRAME_L)) {
		message[i++] = c;
	}
	message_l = i;

	// decode kmp frame
	if (kmp_decode_frame(message, message_l, &response) > 0) {
		message_l = 0;		// zero it so we can reuse it for mqtt string
		current_unix_time = (uint32)(get_unix_time());		// TODO before 2038 ,-)
	
		if (response.kmp_response_serial) {
			kmp_serial = response.kmp_response_serial;	// save it for later use
		}
		else if (current_unix_time) {	// only send mqtt if we got current time via ntp
			// prepare for mqtt transmission if we got serial number from meter
        	
			// format /sample/v1/serial/unix_time => val1=23&val2=val3&baz=blah
			//topic_l = os_sprintf(topic, "/sample/v1/%lu/%lu", kmp_serial, current_unix_time);
			// BUG here.                        returns 0 -^
			// this is a fix
			memset(topic, 0, sizeof(topic));			// clear it
			tfp_snprintf(current_unix_time_string, 64, "%u", (uint32_t)current_unix_time);
			tfp_snprintf(topic, MQTT_TOPIC_L, "/sample/v2/%u/%s", kmp_serial, current_unix_time_string);

			memset(message, 0, sizeof(message));			// clear it
        	
			// heap size
			tfp_snprintf(key_value, MQTT_TOPIC_L, "heap=%u&", system_get_free_heap_size());
			strcat(message, key_value);
        	
			// heating meter specific
			// flow temperature
			kmp_value_to_string(response.kmp_response_register_list[3].value, response.kmp_response_register_list[3].si_ex, kmp_value_string);
			kmp_unit_to_string(response.kmp_response_register_list[3].unit, kmp_unit_string);
			tfp_snprintf(key_value, MQTT_TOPIC_L, "t1=%s %s&", kmp_value_string, kmp_unit_string);
			strcat(message, key_value);
        	
			// return flow temperature
			kmp_value_to_string(response.kmp_response_register_list[4].value, response.kmp_response_register_list[4].si_ex, kmp_value_string);
			kmp_unit_to_string(response.kmp_response_register_list[4].unit, kmp_unit_string);
			tfp_snprintf(key_value, MQTT_TOPIC_L, "t2=%s %s&", kmp_value_string, kmp_unit_string);
			strcat(message, key_value);
        	
			// temperature difference
			kmp_value_to_string(response.kmp_response_register_list[5].value, response.kmp_response_register_list[5].si_ex, kmp_value_string);
			kmp_unit_to_string(response.kmp_response_register_list[5].unit, kmp_unit_string);
			tfp_snprintf(key_value, MQTT_TOPIC_L, "tdif=%s %s&", kmp_value_string, kmp_unit_string);
			strcat(message, key_value);
        	
			// flow
			kmp_value_to_string(response.kmp_response_register_list[6].value, response.kmp_response_register_list[6].si_ex, kmp_value_string);
			kmp_unit_to_string(response.kmp_response_register_list[6].unit, kmp_unit_string);
			tfp_snprintf(key_value, MQTT_TOPIC_L, "flow1=%s %s&", kmp_value_string, kmp_unit_string);
			strcat(message, key_value);
        	
			// current power
			kmp_value_to_string(response.kmp_response_register_list[7].value, response.kmp_response_register_list[7].si_ex, kmp_value_string);
			kmp_unit_to_string(response.kmp_response_register_list[7].unit, kmp_unit_string);
			tfp_snprintf(key_value, MQTT_TOPIC_L, "effect1=%s %s&", kmp_value_string, kmp_unit_string);
			strcat(message, key_value);
        	
			// hours
			kmp_value_to_string(response.kmp_response_register_list[2].value, response.kmp_response_register_list[2].si_ex, kmp_value_string);
			kmp_unit_to_string(response.kmp_response_register_list[2].unit, kmp_unit_string);
			tfp_snprintf(key_value, MQTT_TOPIC_L, "hr=%s %s&", kmp_value_string, kmp_unit_string);
			strcat(message, key_value);
        	
			// volume
			kmp_value_to_string(response.kmp_response_register_list[1].value, response.kmp_response_register_list[1].si_ex, kmp_value_string);
			kmp_unit_to_string(response.kmp_response_register_list[1].unit, kmp_unit_string);
			tfp_snprintf(key_value, MQTT_TOPIC_L, "v1=%s %s&", kmp_value_string, kmp_unit_string);
			strcat(message, key_value);
        	
			// power
			kmp_value_to_string(response.kmp_response_register_list[0].value, response.kmp_response_register_list[0].si_ex, kmp_value_string);
			kmp_unit_to_string(response.kmp_response_register_list[0].unit, kmp_unit_string);
			tfp_snprintf(key_value, MQTT_TOPIC_L, "e1=%s %s&", kmp_value_string, kmp_unit_string);
			strcat(message, key_value);
        	
			memset(cleartext, 0, sizeof(message));
			os_strncpy(cleartext, message, sizeof(message));	// make a copy of message for later use
			os_memset(message, 0, sizeof(message));			// ...and clear it
						
			// encrypt and send
			message_l = encrypt_aes_hmac_combined(message, topic, strlen(topic), cleartext, strlen(cleartext) + 1);
			
			if (mqtt_client) {
				// if mqtt_client is initialized
				if (kmp_serial && (message_l > 1)) {
					// if we received both serial and registers send it
					MQTT_Publish(mqtt_client, topic, message, message_l, 0, 0);
				}
			}
			kmp_requests_sent = 0;	// reset retry counter
			// disable timer
		}
	}
	else {
		// error decoding frame - kmp_receive_timeout_timer_func() retransmits after timeout
	}
}

ICACHE_FLASH_ATTR
void kmp_request_init() {
	fifo_head = 0;
	fifo_tail = 0;

	kmp_requests_sent = 0;
	
	system_os_task(kmp_received_task, kmp_received_task_prio, kmp_received_task_queue, kmp_received_task_queue_length);
}

// helper function to pass mqtt client struct from user_main.c to here
ICACHE_FLASH_ATTR
void kmp_set_mqtt_client(MQTT_Client* client) {
	mqtt_client = client;
}

// helper function to pass received kmp_serial to user_main.c
ICACHE_FLASH_ATTR
unsigned int kmp_get_received_serial() {
	return kmp_serial;
}

ICACHE_FLASH_ATTR
void static kmp_get_serial_timer_func() {
	// get serial
	// prepare frame
	frame_length = kmp_get_serial(frame);
	uart0_tx_buffer(frame, frame_length);     // send kmp request
}

ICACHE_FLASH_ATTR
void static kmp_get_register_timer_func() {
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
    frame_length = kmp_get_register(frame, register_list, 8);
	
    // send frame
    uart0_tx_buffer(frame, frame_length);     // send kmp request
}

ICACHE_FLASH_ATTR
void static kmp_receive_timeout_timer_func() {
	if (kmp_requests_sent > 0) {
		// if no reply received, retransmit
		kmp_request_send();
	}
}

ICACHE_FLASH_ATTR
void kmp_request_send() {
#ifdef DEBUG_NO_METER
	_align_32_bit char cleartext[MQTT_MESSAGE_L];	// is casted in crypto lib
	_align_32_bit char topic[MQTT_TOPIC_L];
	_align_32_bit char message[KMP_FRAME_L];
	int message_l;
#endif
    os_timer_disarm(&kmp_get_serial_timer);
    os_timer_setfn(&kmp_get_serial_timer, (os_timer_func_t *)kmp_get_serial_timer_func, NULL);
    os_timer_arm(&kmp_get_serial_timer, 0, 0);		// now
	
    os_timer_disarm(&kmp_get_register_timer);
    os_timer_setfn(&kmp_get_register_timer, (os_timer_func_t *)kmp_get_register_timer_func, NULL);
    os_timer_arm(&kmp_get_register_timer, 400, 0);		// after 0.4 seconds
	
	// start retransmission timeout timer
    os_timer_disarm(&kmp_receive_timeout_timer);
    os_timer_setfn(&kmp_receive_timeout_timer, (os_timer_func_t *)kmp_receive_timeout_timer_func, NULL);
    os_timer_arm(&kmp_receive_timeout_timer, 2000, 0);		// after 2 seconds
	
	kmp_requests_sent++;
#ifdef DEBUG_NO_METER
	// clear data
//	memset(message, 0, sizeof(message));
//	memset(topic, 0, sizeof(message));

	// fake serial for testing without meter
	kmp_serial = atoi(DEFAULT_METER_SERIAL);

	tfp_snprintf(topic, MQTT_TOPIC_L, "/sample/v2/%u/%u", kmp_serial, get_unix_time());
//	memset(cleartext, 0, sizeof(cleartext));
	tfp_snprintf(cleartext, KMP_FRAME_L, "heap=%lu&t1=25.00 C&t2=15.00 C&tdif=10.00 K&flow1=0 l/h&effect1=0.0 kW&hr=0 h&v1=0.00 m3&e1=0 kWh&", system_get_free_heap_size());

	// encrypt and send
	message_l = encrypt_aes_hmac_combined(message, topic, strlen(topic), cleartext, strlen(cleartext) + 1);

	if (mqtt_client) {
		// if mqtt_client is initialized
		MQTT_Publish(mqtt_client, topic, message, message_l, 0, 0);
	}
#endif
	kmp_requests_sent = 0;	// reset retry counter
}

// fifo
unsigned int kmp_fifo_in_use() {
	return fifo_head - fifo_tail;
}

unsigned char kmp_fifo_put(unsigned char c) {
	if (kmp_fifo_in_use() != QUEUE_SIZE) {
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

unsigned char kmp_fifo_get(unsigned char *c) {
	if (kmp_fifo_in_use() != 0) {
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

unsigned char kmp_fifo_snoop(unsigned char *c, unsigned int pos) {
	if (kmp_fifo_in_use() > (pos)) {
        *c = fifo_buffer[(fifo_tail + pos) % QUEUE_SIZE];
		return 1;
	}
	else {
		return 0;
	}
}
