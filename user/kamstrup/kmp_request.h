#include "mqtt.h"

#define kmp_received_task_prio			USER_TASK_PRIO_2
#define kmp_received_task_queue_length	64

typedef void (*meter_is_ready_cb)(void);
typedef void (*meter_sent_data_cb)(void);

os_event_t    kmp_received_task_queue[kmp_received_task_queue_length];

ICACHE_FLASH_ATTR
void kmp_request_init();

ICACHE_FLASH_ATTR
void kmp_set_mqtt_client(MQTT_Client* client);

ICACHE_FLASH_ATTR
void kmp_register_meter_is_ready_cb(meter_is_ready_cb cb);

ICACHE_FLASH_ATTR
void kmp_register_meter_sent_data_cb(meter_sent_data_cb cb);

ICACHE_FLASH_ATTR
unsigned int kmp_get_received_serial();

#ifdef FORCED_FLOW_METER
ICACHE_FLASH_ATTR
uint32_t kmp_get_received_volume_m3();
#else
ICACHE_FLASH_ATTR
uint32_t kmp_get_received_energy_kwh();
#endif

ICACHE_FLASH_ATTR
void kmp_request_send();

// fifo
unsigned int kmp_fifo_in_use();

unsigned char kmp_fifo_put(unsigned char c);

unsigned char kmp_fifo_get(unsigned char *c);

unsigned char kmp_fifo_snoop(unsigned char *c, unsigned int pos);
