#include "user_interface.h"
#include "osapi.h"
#include "driver/uart.h"

#include "mqtt.h"
#include "kmp.h"
#include "kmp_request.h"

#define QUEUE_SIZE 256

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

MQTT_Client *mqtt_client;

ICACHE_FLASH_ATTR
void kmp_request_init(MQTT_Client* client) {
	mqtt_client = client;
	system_os_task(kmp_received_task, kmp_received_task_prio, kmp_received_task_queue, kmp_received_task_queue_length);
}

ICACHE_FLASH_ATTR
void kmp_request_send() {
	// get serial
	// prepare frame
	frame_length = kmp_get_serial(frame);
	uart0_tx_buffer(frame, frame_length);     // send kmp request
	
	
    os_delay_us(20000);             // sleep 2 seconds

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
    
    os_delay_us(20000);             // sleep 2 seconds
}

/**
  * @brief  Uart receive task.
  * @param  events: contain the uart receive data
  * @retval None
  */
ICACHE_FLASH_ATTR
static void kmp_received_task(os_event_t *events) {
	uint8_t c;
	unsigned int i;
	//unsigned char topic[128];
	unsigned char message[1024];
	//int topic_l;
	int message_l;
	unsigned char hex_char[3];

	c = events->par;
	i = 0;
	if ((c == '\r') || (c == '\n')) {
		memset(message, 0x00, 1024);			// clear it
		while (kmp_fifo_get(&c)) {				// DEBUG: possible BUFFER OVERFLOW here
			os_sprintf(hex_char, "%02x", c);
			strcat(message, hex_char);
			i++;
		}
		// send it
		message_l = strlen(message);
		MQTT_Publish(mqtt_client, "/console", message, message_l, 0, 0);
	}
  
	if (UART_RXFIFO_FULL_INT_ST == (READ_PERI_REG(UART_INT_ST(UART0)) & UART_RXFIFO_FULL_INT_ST)) {
		WRITE_PERI_REG(UART_INT_CLR(UART0), UART_RXFIFO_FULL_INT_CLR);
	}
	else if (UART_RXFIFO_TOUT_INT_ST == (READ_PERI_REG(UART_INT_ST(UART0)) & UART_RXFIFO_TOUT_INT_ST)) {
		WRITE_PERI_REG(UART_INT_CLR(UART0), UART_RXFIFO_TOUT_INT_CLR);
	}
	ETS_UART_INTR_ENABLE();
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
