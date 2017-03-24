/*
 * wifi.c
 *
 *  Created on: Dec 30, 2014
 *      Author: Minh
 */
#include <esp8266.h>
#include "wifi.h"
#include "mqtt_msg.h"
#include "debug.h"
#include "user_config.h"
#include "config.h"
#include "led.h"
#include "tinyprintf.h"

#define WIFI_SCAN_INTERVAL 5000
#define RSSI_CHECK_INTERVAL 10000

static os_timer_t wifi_scan_timer;
static os_timer_t wifi_get_rssi_timer;

WifiCallback wifi_cb = NULL;
uint8_t* config_ssid;
uint8_t* config_pass;
static uint8_t wifi_status = STATION_IDLE;
static uint8_t wifi_event = EVENT_STAMODE_DISCONNECTED;
bool wifi_present = false;
bool wifi_fallback_present = false;
bool wifi_fallback_last_present = false;
volatile bool wifi_scan_runnning = false;
//volatile bool wifi_reconnect = false;
volatile sint8_t rssi = 31;	// set rssi to fail state at init time
volatile bool get_rssi_running = false;
volatile bool wifi_default_ok = false; 

void wifi_handle_event_cb(System_Event_t *evt) {
	struct station_config stationConf;

	wifi_status = wifi_station_get_connect_status();
	wifi_event = evt->event;

	os_memset(&stationConf, 0, sizeof(struct station_config));
	wifi_station_get_config(&stationConf);

#ifdef DEBUG
	os_printf("E%dW%d\n", evt->event, wifi_status);
#endif
	switch (evt->event) {
		case EVENT_STAMODE_CONNECTED:
			// set default network status
			if (os_strncmp(&stationConf.ssid, sys_cfg.sta_ssid, sizeof(sys_cfg.sta_ssid)) == 0) {
				wifi_default_ok = true;
			}
			break;
		case EVENT_STAMODE_DISCONNECTED:
#ifdef DEBUG
			os_printf("disconnect from ssid %s, reason %d\n", evt->event_info.disconnected.ssid, evt->event_info.disconnected.reason);
#endif
//			if 	(wifi_reconnect) {
//				wifi_station_connect();	// reconnect
//			}
			// set default network status
    		if (os_strncmp(&stationConf.ssid, sys_cfg.sta_ssid, sizeof(sys_cfg.sta_ssid)) == 0) {
    			wifi_default_ok = false;
    		}
			break;
		case EVENT_STAMODE_GOT_IP:
//			wifi_reconnect = true;	// re-enable wifi_station_connect() in wifi_handle_event_cb()
		
			// set default network status
    		if (os_strncmp(&stationConf.ssid, sys_cfg.sta_ssid, sizeof(sys_cfg.sta_ssid)) == 0) {
    			wifi_default_ok = true;
    		}
		
			wifi_cb(wifi_status);
			break;
		default:
			break;
	}
}

static void ICACHE_FLASH_ATTR wifi_get_rssi_timer_func(void *arg) {
	get_rssi_running = true;
	if (wifi_default_ok) {	// only get rssi if connected to configured ssid
		rssi = wifi_station_get_rssi();
	}
	get_rssi_running = false;
}

static void ICACHE_FLASH_ATTR wifi_scan_timer_func(void *arg) {
//	struct scan_config config;
	
	if (!wifi_scan_runnning) {
		// scan for fallback network
//		os_memset(&config, 0, sizeof(config));
//		config.ssid = STA_FALLBACK_SSID;
//		wifi_station_scan(&config, wifi_scan_done_cb);	// scanning for specific ssid does not work in SDK 1.5.2
		wifi_scan_runnning = true;
#ifdef DEBUG
		os_printf("RSSI: %d\n", wifi_get_rssi());		// DEBUG: should not be here at all
#endif
		wifi_station_scan(NULL, wifi_scan_done_cb);
//		os_printf("scan running\n");
	}
}

void ICACHE_FLASH_ATTR wifi_scan_done_cb(void *arg, STATUS status) {
	struct bss_info *info;
	
	wifi_present = false;
	wifi_fallback_present = false;
	
	// check if fallback network is present
	if ((arg != NULL) && (status == OK)) {
		info = (struct bss_info *)arg;
		wifi_fallback_present = false;
		
		while (info != NULL) {
			if ((info != NULL) && (info->ssid != NULL) && (os_strncmp(info->ssid, sys_cfg.sta_ssid, sizeof(sys_cfg.sta_ssid)) == 0)) {
				wifi_present = true;
			}
			if ((info != NULL) && (info->ssid != NULL) && (os_strncmp(info->ssid, STA_FALLBACK_SSID, sizeof(STA_FALLBACK_SSID)) == 0)) {
				wifi_fallback_present = true;
			}
			info = info->next.stqe_next;
		}
		
		// if fallback network appeared connect to it
		if ((wifi_fallback_present) && (!wifi_fallback_last_present)) {
			wifi_fallback();
			led_pattern_a();
		}
		// if fallback network disappeared connect to default network
		else if ((!wifi_fallback_present) && (wifi_fallback_last_present)) {
			wifi_default();
			led_stop_pattern();
		}
		
		wifi_fallback_last_present = wifi_fallback_present;
#ifdef DEBUG
		os_printf("WiFi present: %s\n", (wifi_present ? "yes" : "no"));
#endif
	}
	
	wifi_scan_runnning = false;
//	os_printf("scan done\n");

	// start wifi scan timer again
	wifi_start_scan();
}

void ICACHE_FLASH_ATTR wifi_default() {
	struct station_config stationConf;
	
//	wifi_reconnect = false;		// disable wifi_handle_event_cb() from connecting - done here

	// go back to saved network
	os_printf("DEFAULT_SSID\r\n");
	wifi_station_disconnect();
	wifi_set_opmode_current(STATION_MODE);
	os_memset(&stationConf, 0, sizeof(struct station_config));
	wifi_station_get_config(&stationConf);
    
	tfp_snprintf(stationConf.ssid, 32, "%s", config_ssid);
	tfp_snprintf(stationConf.password, 64, "%s", config_pass);
    
	wifi_station_set_config_current(&stationConf);
	wifi_station_connect();
	
	// start wifi rssi timer
	os_timer_disarm(&wifi_get_rssi_timer);
	os_timer_setfn(&wifi_get_rssi_timer, (os_timer_func_t *)wifi_get_rssi_timer_func, NULL);
	os_timer_arm(&wifi_get_rssi_timer, RSSI_CHECK_INTERVAL, 1);
}

void ICACHE_FLASH_ATTR wifi_fallback() {
	struct station_config stationConf;

	// try fallback network
	os_printf("FALLBACK_SSID\r\n");
	wifi_station_disconnect();
	wifi_set_opmode_current(STATION_MODE);
	os_memset(&stationConf, 0, sizeof(struct station_config));
	wifi_station_get_config(&stationConf);
	
	tfp_snprintf(stationConf.ssid, 32, "%s", STA_FALLBACK_SSID);
	tfp_snprintf(stationConf.password, 64, "%s", STA_FALLBACK_PASS);
	
	wifi_station_set_config_current(&stationConf);
	wifi_station_connect();
}

void ICACHE_FLASH_ATTR wifi_connect(uint8_t* ssid, uint8_t* pass, WifiCallback cb) {
	struct station_config stationConf;

	INFO("WIFI_INIT\r\n");
	wifi_set_opmode(STATION_MODE);
	wifi_station_set_auto_connect(false);
	wifi_cb = cb;
	config_ssid = ssid;
	config_pass = pass;

	os_memset(&stationConf, 0, sizeof(struct station_config));

	tfp_snprintf(stationConf.ssid, 32, "%s", ssid);
	tfp_snprintf(stationConf.password, 64, "%s", pass);

	wifi_station_disconnect();
	wifi_station_set_config(&stationConf);

	// start wifi scan timer
	wifi_start_scan();

	wifi_station_set_auto_connect(true);
//	wifi_reconnect = true;	// let wifi_handle_event_cb() handle reconnect on disconnect
	wifi_set_event_handler_cb(wifi_handle_event_cb);
	wifi_station_connect();
	
	// start wifi rssi timer
	os_timer_disarm(&wifi_get_rssi_timer);
	os_timer_setfn(&wifi_get_rssi_timer, (os_timer_func_t *)wifi_get_rssi_timer_func, NULL);
	os_timer_arm(&wifi_get_rssi_timer, RSSI_CHECK_INTERVAL, 1);

	led_stop_pattern();
}

sint8_t ICACHE_FLASH_ATTR wifi_get_rssi() {
	while (get_rssi_running == true) {
		// wait for lock
	}
	return rssi;
}

bool ICACHE_FLASH_ATTR wifi_get_status() {
	return wifi_default_ok;
}

void ICACHE_FLASH_ATTR wifi_start_scan() {
	// start wifi scan timer
	os_timer_disarm(&wifi_scan_timer);
	os_timer_setfn(&wifi_scan_timer, (os_timer_func_t *)wifi_scan_timer_func, NULL);
	os_timer_arm(&wifi_scan_timer, WIFI_SCAN_INTERVAL, 0);
}

void ICACHE_FLASH_ATTR wifi_stop_scan() {
	// stop wifi scan timer
	os_timer_disarm(&wifi_scan_timer);
}

bool ICACHE_FLASH_ATTR wifi_scan_is_running() {
	return wifi_scan_runnning;
}

bool ICACHE_FLASH_ATTR wifi_fallback_is_present() {
	return wifi_fallback_present;
}