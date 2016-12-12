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

