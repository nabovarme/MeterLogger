#define WATCHDOG_MAX	1

#define MQTT_WATCHDOG_ID		1
#define MQTT_WATCHDOG_TIMEOUT	300		// restart wifi after 5 minutes without being able to send mqtt

typedef enum {
	NOT_ENABLED,
	REBOOT,
	REBOOT_VIA_EXT_WD,
	NETWORK_RESTART
} watchdog_type_t;

ICACHE_FLASH_ATTR void init_watchdog();
ICACHE_FLASH_ATTR void start_watchdog();
ICACHE_FLASH_ATTR void stop_watchdog();
ICACHE_FLASH_ATTR bool add_watchdog(uint32_t id, watchdog_type_t type, uint32_t timeout);
ICACHE_FLASH_ATTR bool remove_watchdog(uint32_t id);
ICACHE_FLASH_ATTR void reset_watchdog(uint32_t id);
