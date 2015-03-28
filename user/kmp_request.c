#include "user_interface.h"
#include "osapi.h"
#include "driver/uart.h"

#include "unix_time.h"
#include "mqtt.h"
#include "kmp.h"
#include "kmp_request.h"

#define QUEUE_SIZE 256

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
unsigned char unit_string[8];

MQTT_Client *mqtt_client = NULL;	// initialize to NULL

ICACHE_FLASH_ATTR
void kmp_request_init() {
	fifo_head = 0;
	fifo_tail = 0;

	system_os_task(kmp_received_task, kmp_received_task_prio, kmp_received_task_queue, kmp_received_task_queue_length);
}

ICACHE_FLASH_ATTR
void kmp_set_mqtt_client(MQTT_Client* client) {
	mqtt_client = client;
}

ICACHE_FLASH_ATTR
void kmp_request_send() {
	// get serial
	// prepare frame
	frame_length = kmp_get_serial(frame);
	uart0_tx_buffer(frame, frame_length);     // send kmp request
	
	
    os_delay_us(2000000);             // sleep 2 seconds

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
	
	uint32 random_value;								// DEBUG: for meter test data generation
	unsigned char hex_char[3];
	
    // allocate struct for response
    kmp_response_t response;
    unsigned char unit_string[16];

	//ETS_UART_INTR_DISABLE();

	memset(message, 0x00, KMP_FRAME_L);			// clear message buffer
	i = 0;
	while (kmp_fifo_get(&c) && (i <= KMP_FRAME_L)) {
		message[i++] = c;
	}
	message_l = i;

	// decode kmp frame
	kmp_decode_frame(message, message_l, &response);
	
	if (mqtt_client) {
		// if mqtt_client is initialized
		
		if (kmp_serial) {
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
			key_value_l = os_sprintf(key_value, "flow_temperature=%lu&", (uint32_t)kmp_value_to_double(response.kmp_response_register_list[3].value, response.kmp_response_register_list[3].si_ex));
			strcat(message, key_value);

			// return flow temperature
			key_value_l = os_sprintf(key_value, "return_flow_temperature=%lu&", (uint32_t)kmp_value_to_double(response.kmp_response_register_list[4].value, response.kmp_response_register_list[4].si_ex));
			strcat(message, key_value);
	
			// temperature difference
			key_value_l = os_sprintf(key_value, "temperature_difference=%lu&", (uint32_t)kmp_value_to_double(response.kmp_response_register_list[5].value, response.kmp_response_register_list[5].si_ex));
			strcat(message, key_value);
	
			// flow
			key_value_l = os_sprintf(key_value, "flow=%lu&", (uint32_t)kmp_value_to_double(response.kmp_response_register_list[6].value, response.kmp_response_register_list[6].si_ex));
			strcat(message, key_value);
	
			// current power
			key_value_l = os_sprintf(key_value, "current_power=%lu&", (uint32_t)((double)1000000 * kmp_value_to_double(response.kmp_response_register_list[7].value, response.kmp_response_register_list[7].si_ex)));
			strcat(message, key_value);
	
			// hours
			key_value_l = os_sprintf(key_value, "hours=%lu&", (uint32_t)kmp_value_to_double(response.kmp_response_register_list[2].value, response.kmp_response_register_list[2].si_ex));
			strcat(message, key_value);
	
			// volume
			key_value_l = os_sprintf(key_value, "volume=%lu&", (uint32_t)((double)1000 * kmp_value_to_double(response.kmp_response_register_list[1].value, response.kmp_response_register_list[1].si_ex)));
			strcat(message, key_value);
	
			// power
			key_value_l = os_sprintf(key_value, "power=%lu&", (uint32_t)((double)1000000 * kmp_value_to_double(response.kmp_response_register_list[0].value, response.kmp_response_register_list[0].si_ex)));
			strcat(message, key_value);
			
			os_printf("%s\n\r", message);
	
	
			// send it
			message_l = strlen(message);
			MQTT_Publish(mqtt_client, topic, message, message_l, 0, 0);
		}
	}
	else {
		// we are starting up, no mqtt
		if (response.kmp_response_serial) {
			kmp_serial = response.kmp_response_serial;	// save it for later use
		}
		os_printf("received serial number is: %u\n", response.kmp_response_serial);
		os_printf("saved serial number is: %u\n", kmp_serial);
	}
  
	if (UART_RXFIFO_FULL_INT_ST == (READ_PERI_REG(UART_INT_ST(UART0)) & UART_RXFIFO_FULL_INT_ST)) {
		WRITE_PERI_REG(UART_INT_CLR(UART0), UART_RXFIFO_FULL_INT_CLR);
	}
	else if (UART_RXFIFO_TOUT_INT_ST == (READ_PERI_REG(UART_INT_ST(UART0)) & UART_RXFIFO_TOUT_INT_ST)) {
		WRITE_PERI_REG(UART_INT_CLR(UART0), UART_RXFIFO_TOUT_INT_CLR);
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
