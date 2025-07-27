#include <esp8266.h>
#include "driver/gpio16.h"
#include "watchdog.h"
#include "unix_time.h"
#include "wifi.h"
#include "config.h"
#include "led.h"
#include "rtc_mem.h"
#include "utils.h"
#include <lwip/dns.h>

#include "debug.h"

static os_timer_t watchdog_timer;
static os_timer_t ext_watchdog_timer;
static os_timer_t wifi_reconnect_timer;

typedef struct{
	volatile uint32_t id;
	volatile watchdog_type_t type;
	volatile uint32_t timeout;
	volatile uint32_t last_reset;
} watchdog_t;

watchdog_t watchdog_list[WATCHDOG_MAX];
volatile uint8_t watchdog_list_len;

ICACHE_FLASH_ATTR void static ext_watchdog_timer_func(void *arg) {
	if (gpio16_input_get()) {
		gpio16_output_set(0);
	}
	else {
		gpio16_output_set(1);
	}
}

ICACHE_FLASH_ATTR void static wifi_reconnect_timer_func(void *arg) {
	if (wifi_scan_is_running()) {
		// reschedule if wifi scan is running
		os_timer_disarm(&wifi_reconnect_timer);
		os_timer_setfn(&wifi_reconnect_timer, (os_timer_func_t *)wifi_reconnect_timer_func, NULL);
		os_timer_arm(&wifi_reconnect_timer, 1000, 0);		
#ifdef DEBUG
		printf("scanner still running - rescheduling reccont\n");
#endif	
	}
	else {
		os_timer_disarm(&wifi_reconnect_timer);
		if (sys_cfg.ap_enabled) {
			if (wifi_get_opmode() != STATIONAP_MODE) {
				wifi_set_opmode_current(STATIONAP_MODE);
			}
		}
		else {
			if (wifi_get_opmode() != STATION_MODE) {
				wifi_set_opmode_current(STATION_MODE);
			}
		}
		led_stop_pattern();	// DEBUG
		set_my_auto_connect(true);
		wifi_default();
		wifi_fallback_force_reset_state();			// dont asume we had seen a working backup network
		wifi_start_scan(WIFI_SCAN_INTERVAL_LONG);	// longer time to let it connect to wifi first
#ifdef DEBUG
		printf("watchdog restarted wifi and started wifi scanner\n");
#endif	
	}
}

ICACHE_FLASH_ATTR void static watchdog_timer_func(void *arg) {
	uint32_t i;
	uint32_t uptime = get_uptime();

	for (i = 0; i < WATCHDOG_MAX; i++) {
		// Skip disabled entries
		if (watchdog_list[i].type == NOT_ENABLED || watchdog_list[i].last_reset == 0) {
			continue;
		}

		// Use unsigned-safe wraparound check
		if ((uptime - watchdog_list[i].last_reset) > watchdog_list[i].timeout) {
#ifdef DEBUG
			printf("watchdog timeout, id: %u\n", watchdog_list[i].id);
#endif
			switch (watchdog_list[i].type) {
				case REBOOT:
					save_rtc_data(1);
					system_restart_defered();
#ifdef DEBUG
					printf("triggered system reboot\n");
#endif
					break;

				case NETWORK_RESTART:
					force_reset_wifi();
#ifdef DEBUG
					printf("triggered network restart\n");
#endif
					break;

				case REBOOT_VIA_EXT_WD:
					os_timer_disarm(&ext_watchdog_timer); // external watchdog will cause reset
#ifdef DEBUG
					printf("triggered reboot via external watchdog\n");
#endif
					break;

				default:
					// Unknown type, do nothing
					break;
			}

			// Reset last_reset so it doesn't trigger repeatedly
			watchdog_list[i].last_reset = uptime;
		}
	}
}

ICACHE_FLASH_ATTR void init_watchdog() {
	memset(watchdog_list, 0x00, sizeof(watchdog_list));
	watchdog_list_len = 0;
}

ICACHE_FLASH_ATTR void start_watchdog() {
#ifdef DEBUG
	printf("watchdog started\n");
#endif
	// start configurable software watchdog system
	os_timer_disarm(&watchdog_timer);
	os_timer_setfn(&watchdog_timer, (os_timer_func_t *)watchdog_timer_func, NULL);
	os_timer_arm(&watchdog_timer, 1000, 1);

	// start extern watchdog timer	(MCP1316)
	os_timer_disarm(&ext_watchdog_timer);
	os_timer_setfn(&ext_watchdog_timer, (os_timer_func_t *)ext_watchdog_timer_func, NULL);
	os_timer_arm(&ext_watchdog_timer, 1000, 1);
	//Set GPIO16 to output mode
	gpio16_output_conf();
	gpio16_output_set(1);	
}

ICACHE_FLASH_ATTR void stop_watchdog() {
#ifdef DEBUG
	printf("watchdog stopped\n");
#endif
	os_timer_disarm(&watchdog_timer);
	os_timer_disarm(&ext_watchdog_timer);
	os_timer_disarm(&wifi_reconnect_timer);
}

ICACHE_FLASH_ATTR bool add_watchdog(uint32_t id, watchdog_type_t type, uint32_t timeout) {	
	if (watchdog_list_len < WATCHDOG_MAX) {
#ifdef DEBUG
		printf("add watchdog, id: %d\n", id);
#endif
		watchdog_list[watchdog_list_len].id = id;
		watchdog_list[watchdog_list_len].type = type;
		watchdog_list[watchdog_list_len].timeout = timeout;
		watchdog_list[watchdog_list_len].last_reset = get_uptime();
		watchdog_list_len++;
		return true;
	}
	else {
#ifdef DEBUG
		printf("watchdog error, cant add more\n");
#endif
		return false;
	}
}

ICACHE_FLASH_ATTR bool remove_watchdog(uint32_t id) {
	uint32_t i;

	if (watchdog_list_len == 0) {
#ifdef DEBUG
		printf("watchdog error, no watchdogs to remove (id: %u)\n", id);
#endif
		return false;
	}

	// Find the entry with the given ID
	for (i = 0; i < watchdog_list_len; i++) {
		if (watchdog_list[i].id == id) {
			break;
		}
	}

	if (i == watchdog_list_len) {
#ifdef DEBUG
		printf("watchdog error, id not found: %u\n", id);
#endif
		return false; // No matching watchdog found
	}

#ifdef DEBUG
	printf("remove watchdog, id: %u (index %u)\n", id, i);
#endif

	// Shift the rest of the list down
	for (; i < watchdog_list_len - 1; i++) {
		watchdog_list[i] = watchdog_list[i + 1];
	}

	// Clear the now-unused last slot
	watchdog_list[watchdog_list_len - 1].id = 0;
	watchdog_list[watchdog_list_len - 1].type = NOT_ENABLED;
	watchdog_list[watchdog_list_len - 1].timeout = 0;
	watchdog_list[watchdog_list_len - 1].last_reset = 0;

	watchdog_list_len--;
	return true;
}

ICACHE_FLASH_ATTR void reset_watchdog(uint32_t id) {
	uint32_t i;
	uint32_t uptime;
	
	uptime = get_uptime();
#ifdef DEBUG
	printf("reset watchdog, id: %d\n", id);
#endif			
	for (i = 0; i < WATCHDOG_MAX; i++) {
		if (watchdog_list[i].id == id) {
			watchdog_list[i].last_reset = uptime;
		}
	}
}

// DEBUG: hack to get it to reconnect on weak wifi
// force reconnect to wireless
ICACHE_FLASH_ATTR void force_reset_wifi() {
	led_pattern_b();	// DEBUG to se if we ever try to restart network
	wifi_stop_scan();
	set_my_auto_connect(false);
	wifi_station_disconnect();
	wifi_set_opmode_current(NULL_MODE);
#ifdef DEBUG
	printf("stopped wifi and wifi scanner\n");
#endif				
	// and (re)-connect when last wifi scan is done - wait 6 second before starting
	os_timer_disarm(&wifi_reconnect_timer);
	os_timer_setfn(&wifi_reconnect_timer, (os_timer_func_t *)wifi_reconnect_timer_func, NULL);
	os_timer_arm(&wifi_reconnect_timer, NETWORK_RESTART_DELAY, 0);
#ifdef DEBUG
	printf("scheduled wifi for restart...\n");
#endif
	disconnect_count++;
#ifdef DEBUG
	printf("disconnect_count: %u\n", disconnect_count);
#endif
}
