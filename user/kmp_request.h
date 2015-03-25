#include "mqtt.h"

#define kmp_received_task_prio			1
#define kmp_received_task_queue_length	64

os_event_t    kmp_received_task_queue[kmp_received_task_queue_length];

ICACHE_FLASH_ATTR
void kmp_request_init(MQTT_Client* client);

ICACHE_FLASH_ATTR
void kmp_request_send();

ICACHE_FLASH_ATTR
static void kmp_received_task(os_event_t *events);

// fifo
ICACHE_FLASH_ATTR
unsigned int kmp_fifo_in_use();

ICACHE_FLASH_ATTR
unsigned char kmp_fifo_put(unsigned char c);

ICACHE_FLASH_ATTR
unsigned char kmp_fifo_get(unsigned char *c);

ICACHE_FLASH_ATTR
unsigned char kmp_fifo_snoop(unsigned char *c, unsigned int pos);
