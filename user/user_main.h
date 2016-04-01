ICACHE_FLASH_ATTR void wifi_changed_cb(uint8_t status);
ICACHE_FLASH_ATTR void mqttConnectedCb(uint32_t *args);
ICACHE_FLASH_ATTR void mqttDisconnectedCb(uint32_t *args);
ICACHE_FLASH_ATTR void mqttPublishedCb(uint32_t *args);
ICACHE_FLASH_ATTR void mqttDataCb(uint32_t *args, const char* topic, uint32_t topic_len, const char *data, uint32_t data_len);

#ifdef IMPULSE
ICACHE_FLASH_ATTR void gpio_int_init();
void gpio_int_handler();
ICACHE_FLASH_ATTR void impulse_meter_init(void);
#endif

ICACHE_FLASH_ATTR void user_init(void);
ICACHE_FLASH_ATTR void system_init_done(void);
