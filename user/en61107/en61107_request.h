#define en61107_received_task_prio			USER_TASK_PRIO_2
#define en61107_received_task_queue_length	64

os_event_t    en61107_received_task_queue[en61107_received_task_queue_length];

ICACHE_FLASH_ATTR
void en61107_request_init();

ICACHE_FLASH_ATTR
void en61107_set_mqtt_client(MQTT_Client* client);

ICACHE_FLASH_ATTR
unsigned int en61107_get_received_serial();

ICACHE_FLASH_ATTR
void en61107_get_data_timer_func();

ICACHE_FLASH_ATTR
void en61107_get_register_timer_func();

ICACHE_FLASH_ATTR
void en61107_receive_timeout_timer_func();

ICACHE_FLASH_ATTR
void en61107_request_send();

// fifo
ICACHE_FLASH_ATTR
unsigned int en61107_fifo_in_use();

ICACHE_FLASH_ATTR
unsigned char en61107_fifo_put(unsigned char c);

ICACHE_FLASH_ATTR
unsigned char en61107_fifo_get(unsigned char *c);

ICACHE_FLASH_ATTR
unsigned char en61107_fifo_snoop(unsigned char *c, unsigned int pos);
