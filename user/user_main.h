ICACHE_FLASH_ATTR void sample_mode_timer_func(void *arg);
ICACHE_FLASH_ATTR void config_mode_timer_func(void *arg);
ICACHE_FLASH_ATTR void sample_timer_func(void *arg);
#ifndef EN61107
ICACHE_FLASH_ATTR void kmp_request_send_timer_func(void *arg);
#else
ICACHE_FLASH_ATTR void en61107_request_send_timer_func(void *arg);
#endif
ICACHE_FLASH_ATTR void wifiConnectCb(uint8_t status);
ICACHE_FLASH_ATTR void mqttConnectedCb(uint32_t *args);
ICACHE_FLASH_ATTR void mqttDisconnectedCb(uint32_t *args);
ICACHE_FLASH_ATTR void mqttPublishedCb(uint32_t *args);
ICACHE_FLASH_ATTR void mqttDataCb(uint32_t *args, const char* topic, uint32_t topic_len, const char *data, uint32_t data_len);
//ICACHE_FLASH_ATTR static void mqtt_dispatch_proc_task(os_event_t *events);
ICACHE_FLASH_ATTR void user_init(void);
ICACHE_FLASH_ATTR void system_init_done(void);
