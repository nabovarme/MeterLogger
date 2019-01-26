#include <esp8266.h>
#include "driver/uart.h"
#include "unix_time.h"
#include "mqtt.h"
#include "tinyprintf.h"
#include "utils.h"
#include "kmp.h"
#include "kmp_request.h"
#include "config.h"
#include "crypto/crypto.h"
#include "crypto/aes.h"

#define QUEUE_SIZE 256

uint32_t kmp_serial = 0;
meter_is_ready_cb kmp_meter_is_ready_cb = NULL;
bool meter_is_ready_cb_called = false;

meter_sent_data_cb kmp_meter_sent_data_cb = NULL;
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

#ifdef FORCED_FLOW_METER
volatile uint32_t v1_l = 0;
#else
volatile uint32_t e1_kwh = 0;
#endif

#ifdef DEBUG_NO_METER
uint8_t pseudo_data_debug_no_meter = 0;
#endif

ICACHE_FLASH_ATTR
static void kmp_received_task(os_event_t *events) {
	unsigned char c;
	unsigned int i;
	uint64_t current_unix_time;
	char current_unix_time_string[64];	// BUGFIX var
	char key_value[256];
	unsigned char topic[MQTT_TOPIC_L];
	unsigned char message[KMP_FRAME_L];
	int message_l;
		
	// vars for aes encryption
	uint8_t cleartext[KMP_FRAME_L];

    // allocate struct for response
    kmp_response_t response;
    unsigned char kmp_unit_string[16];
	unsigned char kmp_value_string[64];

#ifdef FORCED_FLOW_METER
	char v1_l_string[64];
#else
	char e1_kwh_string[64];
#endif	// FORCED_FLOW_METER

	//ETS_UART_INTR_DISABLE();

	memset(message, 0x00, KMP_FRAME_L);			// clear message buffer
	i = 0;
	while (kmp_fifo_get(&c) && (i <= KMP_FRAME_L)) {
		message[i++] = c;
	}
	message_l = i;

	// decode kmp frame
	if (kmp_decode_frame(message, message_l, &response) > 0) {
		// tell user_main we got a serial
		if (kmp_meter_is_ready_cb && !meter_is_ready_cb_called) {
			meter_is_ready_cb_called = true;
			kmp_meter_is_ready_cb();
		}
#ifdef DEBUG
		else {
			os_printf("tried to call kmp_meter_is_ready_cb() before it was set - should not happen\n");
		}
#endif
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
        	
#ifndef FORCED_FLOW_METER
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
#endif	// FORCED_FLOW_METER
        	
			// flow
			kmp_value_to_string(response.kmp_response_register_list[6].value, response.kmp_response_register_list[6].si_ex, kmp_value_string);
			kmp_unit_to_string(response.kmp_response_register_list[6].unit, kmp_unit_string);
			tfp_snprintf(key_value, MQTT_TOPIC_L, "flow1=%s %s&", kmp_value_string, kmp_unit_string);
			strcat(message, key_value);
        	
#ifndef FORCED_FLOW_METER
			// current power
			kmp_value_to_string(response.kmp_response_register_list[7].value, response.kmp_response_register_list[7].si_ex, kmp_value_string);
			kmp_unit_to_string(response.kmp_response_register_list[7].unit, kmp_unit_string);
			tfp_snprintf(key_value, MQTT_TOPIC_L, "effect1=%s %s&", kmp_value_string, kmp_unit_string);
			strcat(message, key_value);
#endif	// FORCED_FLOW_METER
        	
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

#ifdef FORCED_FLOW_METER
			// save volume for later use in kmp_get_received_volume_m3()
			multiply_str_by_1000(kmp_value_string, v1_l_string);
			v1_l = atoi(v1_l_string);
#endif
        	
#ifndef FORCED_FLOW_METER
			// power
			kmp_value_to_string(response.kmp_response_register_list[0].value, response.kmp_response_register_list[0].si_ex, kmp_value_string);
			kmp_unit_to_string(response.kmp_response_register_list[0].unit, kmp_unit_string);
			tfp_snprintf(key_value, MQTT_TOPIC_L, "e1=%s %s&", kmp_value_string, kmp_unit_string);
			strcat(message, key_value);
#endif	// FORCED_FLOW_METER

#ifndef FORCED_FLOW_METER
			// save energy for later use in kmp_get_received_energy_kwh()
			if (strncmp(kmp_unit_string, "MWh", 3) == 0) {
				mw_to_kw_str(kmp_value_string, e1_kwh_string);
				e1_kwh = atoi(e1_kwh_string);
			}
			else {
				e1_kwh = atoi(kmp_value_string);
			}
#endif
        	
			memset(cleartext, 0, sizeof(message));
			os_strncpy(cleartext, message, sizeof(message));	// make a copy of message for later use
			os_memset(message, 0, sizeof(message));			// ...and clear it
						
			// encrypt and send
			message_l = encrypt_aes_hmac_combined(message, topic, strlen(topic), cleartext, strlen(cleartext) + 1);
						
			if (mqtt_client) {
				// if mqtt_client is initialized
				if (kmp_serial && (message_l > 1)) {
					// if we received both serial and registers send it
					MQTT_Publish(mqtt_client, topic, message, message_l, 2, 0);	// QoS level 2
				}
			}
			// tell user_main we got data from meter
			if (kmp_meter_sent_data_cb) {
				kmp_meter_sent_data_cb();
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

ICACHE_FLASH_ATTR
void kmp_register_meter_is_ready_cb(meter_is_ready_cb cb) {
	kmp_meter_is_ready_cb = cb;
}

ICACHE_FLASH_ATTR
void kmp_register_meter_sent_data_cb(meter_sent_data_cb cb) {
	kmp_meter_sent_data_cb = cb;
}

// helper function to pass received kmp_serial to user_main.c
ICACHE_FLASH_ATTR
unsigned int kmp_get_received_serial() {
	return kmp_serial;
}

#ifdef FORCED_FLOW_METER
ICACHE_FLASH_ATTR
uint32_t kmp_get_received_volume_l() {
#ifdef DEBUG_NO_METER
	return pseudo_data_debug_no_meter;
#else
	return v1_l;
#endif
}
#else
// helper function to pass energy to user_main.c
ICACHE_FLASH_ATTR
uint32_t kmp_get_received_energy_kwh() {
#ifdef DEBUG_NO_METER
	return pseudo_data_debug_no_meter;
#else
	return e1_kwh;
#endif
}
#endif	// FORCED_FLOW_METER

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
	char cleartext[MQTT_MESSAGE_L];
	char topic[MQTT_TOPIC_L];
	char message[KMP_FRAME_L];
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
	memset(message, 0, sizeof(message));
	memset(topic, 0, sizeof(topic));

	// fake serial for testing without meter
	kmp_serial = atoi(DEFAULT_METER_SERIAL);

	tfp_snprintf(topic, MQTT_TOPIC_L, "/sample/v2/%u/%u", kmp_serial, get_unix_time());
	memset(cleartext, 0, sizeof(cleartext));
	tfp_snprintf(cleartext, KMP_FRAME_L, "heap=%u&t1=%u.00 C&t2=%u.00 C&tdif=%u.00 K&flow1=%u l/h&effect1=%u.0 kW&hr=%u h&v1=%u.00 m3&e1=%u kWh&",
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
	e1_kwh = pseudo_data_debug_no_meter;
	pseudo_data_debug_no_meter++;	// let it wrap around

	// tell user_main we got a serial
	if (kmp_meter_is_ready_cb && !meter_is_ready_cb_called) {
		meter_is_ready_cb_called = true;
		kmp_meter_is_ready_cb();
	}
	// tell user_main we got data from meter
	if (kmp_meter_sent_data_cb) {
		kmp_meter_sent_data_cb();
	}

#ifdef DEBUG
	else {
		os_printf("tried to call kmp_meter_is_ready_cb() before it was set - should not happen\n");
	}
#endif

	// encrypt and send
	message_l = encrypt_aes_hmac_combined(message, topic, strlen(topic), cleartext, strlen(cleartext) + 1);

	if (mqtt_client) {
		// if mqtt_client is initialized
		MQTT_Publish(mqtt_client, topic, message, message_l, 2, 0);	// QoS level 2
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
