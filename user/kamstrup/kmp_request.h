#include "mqtt.h"

#define kmp_received_task_prio			USER_TASK_PRIO_2
#define kmp_received_task_queue_length	64

os_event_t    kmp_received_task_queue[kmp_received_task_queue_length];

ICACHE_FLASH_ATTR
void kmp_request_init();

ICACHE_FLASH_ATTR
void kmp_set_mqtt_client(MQTT_Client* client);

ICACHE_FLASH_ATTR
unsigned int kmp_get_received_serial();

ICACHE_FLASH_ATTR
void kmp_request_send();

// fifo
unsigned int kmp_fifo_in_use();

unsigned char kmp_fifo_put(unsigned char c);

unsigned char kmp_fifo_get(unsigned char *c);

unsigned char kmp_fifo_snoop(unsigned char *c, unsigned int pos);
