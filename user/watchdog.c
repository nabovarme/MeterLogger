#include <esp8266.h>
#include "driver/gpio16.h"
#include "watchdog.h"
#include "unix_time.h"
#include "wifi.h"
#include "config.h"

#include "debug.h"

static os_timer_t watchdog_timer;
static os_timer_t ext_watchdog_timer;
static os_timer_t wifi_reconnect_timer;

typedef struct{
	uint32_t id;
	watchdog_type_t type;
	uint32_t timeout;
	uint32_t last_reset;
} watchdog_t;

watchdog_t watchdog_list[WATCHDOG_MAX];
uint8_t watchdog_list_len;

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
		os_printf("scanner still running - rescheduling reccont\n");
#endif	
	}
	else {
		os_timer_disarm(&wifi_reconnect_timer);
#ifdef AP
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
#else
		if (wifi_get_opmode() != STATION_MODE) {
			wifi_set_opmode_current(STATION_MODE);
		}
#endif	// AP
		set_my_auto_connect(true);
		wifi_station_connect();
		wifi_start_scan();
#ifdef DEBUG
		os_printf("watchdog restarted wifi and started wifi scanner\n");
#endif	
	}
}

ICACHE_FLASH_ATTR void static watchdog_timer_func(void *arg) {
	uint32_t i;
	uint32_t uptime;
	
	uptime = get_uptime();
//	if (uptime) {	// only run watchdog if we have unix time
	for (i = 0; i < WATCHDOG_MAX; i++) {
		if ((watchdog_list[i].type != NOT_ENABLED) && 
			(watchdog_list[i].last_reset) && 
			((int32_t)watchdog_list[i].last_reset < (int32_t)(uptime - watchdog_list[i].timeout))) {
#ifdef DEBUG
			os_printf("watchdog timeout, id: %d\n", watchdog_list[i].id);
#endif			
			switch (watchdog_list[i].type) {
				case REBOOT:
					system_restart();
#ifdef DEBUG
					os_printf("reboot\n");
#endif	
					break;
					
				case NETWORK_RESTART:
					// DEBUG: hack to get it to reconnect on weak wifi
					// force reconnect to wireless
					wifi_stop_scan();
					set_my_auto_connect(false);
					wifi_station_disconnect();
					wifi_set_opmode_current(NULL_MODE);
#ifdef DEBUG
					os_printf("stopped wifi and wifi scanner\n");
#endif				
					// and (re)-connect when last wifi scan is done - wait 1 second before testing
					os_timer_disarm(&wifi_reconnect_timer);
					os_timer_setfn(&wifi_reconnect_timer, (os_timer_func_t *)wifi_reconnect_timer_func, NULL);
					os_timer_arm(&wifi_reconnect_timer, 1000, 0);
#ifdef DEBUG
					os_printf("scheduled wifi for restart...\n");
#endif	
					break;
					
				case REBOOT_VIA_EXT_WD:
					os_timer_disarm(&ext_watchdog_timer);
#ifdef DEBUG
					os_printf("reboot via ext watchdog\n");
#endif	
					break;
			}
			watchdog_list[i].last_reset = get_uptime();
		}
	}
}

ICACHE_FLASH_ATTR void init_watchdog() {
	memset(watchdog_list, 0x00, sizeof(watchdog_list));
	watchdog_list_len = 0;
}

ICACHE_FLASH_ATTR void start_watchdog() {
#ifdef DEBUG
	os_printf("watchdog started\n");
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
	os_printf("watchdog stopped\n");
#endif
	os_timer_disarm(&ext_watchdog_timer);
}

ICACHE_FLASH_ATTR bool add_watchdog(uint32_t id, watchdog_type_t type, uint32_t timeout) {	
	if (watchdog_list_len < WATCHDOG_MAX) {
#ifdef DEBUG
		os_printf("add watchdog, id: %d\n", id);
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
		os_printf("watchdog error, cant add more\n");
#endif
		return false;
	}
}

ICACHE_FLASH_ATTR bool remove_watchdog(uint32_t id) {
	if (watchdog_list_len > 0) {
#ifdef DEBUG
		os_printf("remove watchdog, id: %d\n", id);
#endif
		watchdog_list[watchdog_list_len].id = 0;
		watchdog_list[watchdog_list_len].type = NOT_ENABLED;
		watchdog_list[watchdog_list_len].timeout = 0;
		watchdog_list_len--;
		return true;
	}
	else {
#ifdef DEBUG
		os_printf("watchdog error, cant remove %d\n", id);
#endif
		return false;
	}
}

ICACHE_FLASH_ATTR void reset_watchdog(uint32_t id) {
	uint32_t i;
	
#ifdef DEBUG
	os_printf("reset watchdog, id: %d\n", id);
#endif			
	for (i = 0; i < WATCHDOG_MAX; i++) {
		if (watchdog_list[i].id == id) {
			watchdog_list[i].last_reset = get_uptime();
		}
	}
}
