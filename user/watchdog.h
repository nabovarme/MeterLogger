#define WATCHDOG_MAX					2

#define MQTT_WATCHDOG_ID				1
#define MQTT_WATCHDOG_REBOOT_ID			2
#define MQTT_WATCHDOG_TIMEOUT			120		// restart wifi after 2 minutes without being able to send mqtt
#define MQTT_WATCHDOG_REBOOT_TIMEOUT	86400	// reboot after 24 hours without being able to send mqtt

#define NETWORK_RESTART_DELAY			6000	// wait 6 seconds between calling wifi_station_disconnect() and wifi_station_connect()

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
ICACHE_FLASH_ATTR void force_reset_wifi();
