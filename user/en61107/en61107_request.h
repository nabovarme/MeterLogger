#ifndef EN61107_REQUEST_H
#define EN61107_REQUEST_H

#include "mqtt.h"
#include "en61107.h"

#define en61107_received_task_prio			USER_TASK_PRIO_2
#define en61107_received_task_queue_length	64

os_event_t    en61107_received_task_queue[en61107_received_task_queue_length];

ICACHE_FLASH_ATTR
void en61107_request_init();

ICACHE_FLASH_ATTR
void en61107_set_mqtt_client(MQTT_Client* client);

ICACHE_FLASH_ATTR
unsigned int en61107_get_received_serial();

//ICACHE_FLASH_ATTR
inline bool en61107_is_eod_char(uint8_t c);

ICACHE_FLASH_ATTR
void en61107_request_send();

// uart send en61107 commands
ICACHE_FLASH_ATTR
void en61107_uart_send_standard_data_2();

ICACHE_FLASH_ATTR
void en61107_uart_send_unknown_1();

ICACHE_FLASH_ATTR
void en61107_uart_send_unknown_2();

ICACHE_FLASH_ATTR
void en61107_uart_send_serial_byte_1();

ICACHE_FLASH_ATTR
void en61107_uart_send_serial_byte_2();

ICACHE_FLASH_ATTR
void en61107_uart_send_serial_byte_3();

ICACHE_FLASH_ATTR
void en61107_uart_send_serial_byte_4();

ICACHE_FLASH_ATTR
void en61107_uart_send_serial_byte_5();

ICACHE_FLASH_ATTR
void en61107_uart_send_serial_byte_6();

ICACHE_FLASH_ATTR
void en61107_uart_send_unknown_3();

ICACHE_FLASH_ATTR
void en61107_uart_send_unknown_4();

ICACHE_FLASH_ATTR
void en61107_uart_send_en61107();

ICACHE_FLASH_ATTR
void en61107_uart_send_inst_values();

// fifo
ICACHE_FLASH_ATTR
unsigned int en61107_fifo_in_use();

//ICACHE_FLASH_ATTR
inline unsigned char en61107_fifo_put(unsigned char c);

ICACHE_FLASH_ATTR
unsigned char en61107_fifo_get(unsigned char *c);

ICACHE_FLASH_ATTR
unsigned char en61107_fifo_snoop(unsigned char *c, unsigned int pos);

#endif

