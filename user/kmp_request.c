#include "user_interface.h"
#include "osapi.h"
#include "driver/uart.h"

#include "unix_time.h"
#include "mqtt.h"
#include "kmp.h"
#include "kmp_request.h"

#define QUEUE_SIZE 256
#define KMP_REQUEST_RETRIES 3

unsigned int kmp_serial = 0;

// fifo
volatile unsigned int fifo_head, fifo_tail;
volatile unsigned char fifo_buffer[QUEUE_SIZE];

// allocate frame to send
unsigned char frame[KMP_FRAME_L];
unsigned int frame_length;
uint16_t register_list[8];

// allocate struct for response
kmp_response_t response;

MQTT_Client *mqtt_client = NULL;	// initialize to NULL

static volatile os_timer_t kmp_get_serial_timer;
static volatile os_timer_t kmp_get_register_timer;

unsigned int kmp_request_retries;

ICACHE_FLASH_ATTR
void kmp_request_init() {
	fifo_head = 0;
	fifo_tail = 0;

	kmp_request_retries = 0;
	
	system_os_task(kmp_received_task, kmp_received_task_prio, kmp_received_task_queue, kmp_received_task_queue_length);
}

ICACHE_FLASH_ATTR
void kmp_set_mqtt_client(MQTT_Client* client) {
	mqtt_client = client;
}

ICACHE_FLASH_ATTR
void kmp_get_serial_timer_func() {
	// get serial
	// prepare frame
	frame_length = kmp_get_serial(frame);
	uart0_tx_buffer(frame, frame_length);     // send kmp request
}

ICACHE_FLASH_ATTR
void kmp_get_register_timer_func() {
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
void kmp_request_send() {
    os_timer_disarm(&kmp_get_serial_timer);
    os_timer_setfn(&kmp_get_serial_timer, (os_timer_func_t *)kmp_get_serial_timer_func, NULL);
    os_timer_arm(&kmp_get_serial_timer, 0, 0);		// now
	
    os_timer_disarm(&kmp_get_register_timer);
    os_timer_setfn(&kmp_get_register_timer, (os_timer_func_t *)kmp_get_register_timer_func, NULL);
    os_timer_arm(&kmp_get_register_timer, 400, 0);		// after 0.4 seconds
}

/**
  * @brief  Uart receive task.
  * @param  events: contain the uart receive data
  * @retval None
  */
ICACHE_FLASH_ATTR
static void kmp_received_task(os_event_t *events) {
	unsigned char c;
	unsigned int i;
	uint64 current_unix_time;
	char key_value[128];
	unsigned char topic[128];
	unsigned char message[KMP_FRAME_L];
	int key_value_l;
	int topic_l;
	int message_l;
		
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
	
		if (response.kmp_response_serial) {
			kmp_serial = response.kmp_response_serial;	// save it for later use
		}
		else {
			// prepare for mqtt transmission if we got serial number from meter
			current_unix_time = (uint32)(get_unix_time());		// TODO before 2038 ,-)
        	
			// format /sample/unix_time => val1=23&val2=val3&baz=blah
			topic_l = os_sprintf(topic, "/sample/%lu", current_unix_time);
			strcpy(message, "");	// clear it
        	
			// serial
			key_value_l = os_sprintf(key_value, "serial=%lu&", kmp_serial);
			strcat(message, key_value);
        	
			// heap size
			key_value_l = os_sprintf(key_value, "heap=%lu&", system_get_free_heap_size());
			strcat(message, key_value);
        	
			// heating meter specific
			// flow temperature
			kmp_value_to_string(response.kmp_response_register_list[3].value, response.kmp_response_register_list[3].si_ex, kmp_value_string);
			kmp_unit_to_string(response.kmp_response_register_list[3].unit, kmp_unit_string);
			key_value_l = os_sprintf(key_value, "t1=%s %s&", kmp_value_string, kmp_unit_string);
			strcat(message, key_value);
        	
			// return flow temperature
			kmp_value_to_string(response.kmp_response_register_list[4].value, response.kmp_response_register_list[4].si_ex, kmp_value_string);
			kmp_unit_to_string(response.kmp_response_register_list[4].unit, kmp_unit_string);
			key_value_l = os_sprintf(key_value, "t2=%s %s&", kmp_value_string, kmp_unit_string);
			strcat(message, key_value);
        	
			// temperature difference
			kmp_value_to_string(response.kmp_response_register_list[5].value, response.kmp_response_register_list[5].si_ex, kmp_value_string);
			kmp_unit_to_string(response.kmp_response_register_list[5].unit, kmp_unit_string);
			key_value_l = os_sprintf(key_value, "tdif=%s %s&", kmp_value_string, kmp_unit_string);
			strcat(message, key_value);
        	
			// flow
			kmp_value_to_string(response.kmp_response_register_list[6].value, response.kmp_response_register_list[6].si_ex, kmp_value_string);
			kmp_unit_to_string(response.kmp_response_register_list[6].unit, kmp_unit_string);
			key_value_l = os_sprintf(key_value, "flow1=%s %s&", kmp_value_string, kmp_unit_string);
			strcat(message, key_value);
        	
			// current power
			kmp_value_to_string(response.kmp_response_register_list[7].value, response.kmp_response_register_list[7].si_ex, kmp_value_string);
			kmp_unit_to_string(response.kmp_response_register_list[7].unit, kmp_unit_string);
			key_value_l = os_sprintf(key_value, "effect1=%s %s&", kmp_value_string, kmp_unit_string);
			strcat(message, key_value);
        	
			// hours
			kmp_value_to_string(response.kmp_response_register_list[2].value, response.kmp_response_register_list[2].si_ex, kmp_value_string);
			kmp_unit_to_string(response.kmp_response_register_list[2].unit, kmp_unit_string);
			key_value_l = os_sprintf(key_value, "hr=%s %s&", kmp_value_string, kmp_unit_string);
			strcat(message, key_value);
        	
			// volume
			kmp_value_to_string(response.kmp_response_register_list[1].value, response.kmp_response_register_list[1].si_ex, kmp_value_string);
			kmp_unit_to_string(response.kmp_response_register_list[1].unit, kmp_unit_string);
			key_value_l = os_sprintf(key_value, "v1=%s %s&", kmp_value_string, kmp_unit_string);
			strcat(message, key_value);
        	
			// power
			kmp_value_to_string(response.kmp_response_register_list[0].value, response.kmp_response_register_list[0].si_ex, kmp_value_string);
			kmp_unit_to_string(response.kmp_response_register_list[0].unit, kmp_unit_string);
			key_value_l = os_sprintf(key_value, "e1=%s %s&", kmp_value_string, kmp_unit_string);
			strcat(message, key_value);
        	
			message_l = strlen(message);
	    	
			if (mqtt_client) {
				// if mqtt_client is initialized
				if (kmp_serial && (message_l > 1)) {
					// if we received both serial and registers send it
					MQTT_Publish(mqtt_client, topic, message, message_l, 0, 0);
				}
			}
			kmp_request_retries = 0;	// reset retry counter
		}
	}
	else {
		// error decoding frame - retransmitting request KMP_REQUEST_RETRIES times
		if (kmp_request_retries++ < KMP_REQUEST_RETRIES) {
			kmp_request_send();
		}
	}
}


// fifo
ICACHE_FLASH_ATTR
unsigned int kmp_fifo_in_use() {
	return fifo_head - fifo_tail;
}

ICACHE_FLASH_ATTR
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

ICACHE_FLASH_ATTR
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

ICACHE_FLASH_ATTR
unsigned char kmp_fifo_snoop(unsigned char *c, unsigned int pos) {
	if (kmp_fifo_in_use() > (pos)) {
        *c = fifo_buffer[(fifo_tail + pos) % QUEUE_SIZE];
		return 1;
	}
	else {
		return 0;
	}
}
