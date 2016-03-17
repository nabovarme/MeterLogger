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

#define NETWORK_CHECK_TIME 5000

static os_timer_t network_check_timer;

WifiCallback wifi_cb = NULL;
uint8_t* config_ssid;
uint8_t* config_pass;
static uint8_t wifi_status = STATION_IDLE;
static uint8_t last_wifi_status = STATION_IDLE;
bool wifi_fallback_present = false;
bool wifi_fallback_last_present = false;
volatile bool wifi_scan_runnning = false;
volatile bool wifi_reconnect = false;

void wifi_handle_event_cb(System_Event_t *evt) {
	struct ip_info ipConfig;

	wifi_get_ip_info(STATION_IF, &ipConfig);
	wifi_status = wifi_station_get_connect_status();

//	os_printf("event %x\n", evt->event);
	switch (evt->event) {
		case EVENT_STAMODE_CONNECTED:
			break;
		case EVENT_STAMODE_DISCONNECTED:
#ifdef DEBUG
			os_printf("disconnect from ssid %s, reason %d\n",
						evt->event_info.disconnected.ssid,
						evt->event_info.disconnected.reason);
#endif
			if 	(wifi_reconnect) {
				wifi_station_connect();	// reconnect
			}
			break;
//		case EVENT_STAMODE_AUTHMODE_CHANGE:
//			os_printf("mode: %d -> %d\n",
//						evt->event_info.auth_change.old_mode,
//						evt->event_info.auth_change.new_mode);
//			break;
		case EVENT_STAMODE_GOT_IP:
			wifi_reconnect = true;	// re-enable wifi_station_connect() in wifi_handle_event_cb()
			wifi_cb(wifi_status);
			break;
//		case EVENT_SOFTAPMODE_STACONNECTED:
//			os_printf("station: " MACSTR "join, AID = %d\n",
//						MAC2STR(evt->event_info.sta_connected.mac),
//						evt->event_info.sta_connected.aid);
//			break;
//		case EVENT_SOFTAPMODE_STADISCONNECTED:
//			os_printf("station: " MACSTR "leave, AID = %d\n",
//						MAC2STR(evt->event_info.sta_disconnected.mac),
//						evt->event_info.sta_disconnected.aid);
//			break;
		default:
			break;
	}
}

static void ICACHE_FLASH_ATTR network_check_timer_func(void *arg) {
	struct scan_config config;
	
	if (!wifi_scan_runnning) {
		// scan for fallback network
//		os_memset(&config, 0, sizeof(config));
//		config.ssid = STA_FALLBACK_SSID;
//		wifi_station_scan(&config, wifi_scan_done_cb);	// scanning for specific ssid does not work in SDK 1.5.2
		wifi_scan_runnning = true;
		wifi_station_scan(NULL, wifi_scan_done_cb);
//		os_printf("scan running\n");
	}
}

void ICACHE_FLASH_ATTR wifi_scan_done_cb(void *arg, STATUS status) {
	struct bss_info *info;
	
	wifi_fallback_present = false;
	
	// check if fallback network is present
	if ((arg != NULL) && (status == OK)) {
		info = (struct bss_info *)arg;
		wifi_fallback_present = false;
		
		while ((info != NULL) && (!wifi_fallback_present)) {
			if ((info != NULL) && (info->ssid != NULL) && (os_strncmp(info->ssid, STA_FALLBACK_SSID, sizeof(STA_FALLBACK_SSID)) == 0)) {
				wifi_fallback_present = true;
			}
			info = info->next.stqe_next;
		}
		
		// if fallback network appeared connect to it
		if ((wifi_fallback_present) && (!wifi_fallback_last_present)) {
			wifi_fallback();
			led_pattern_b();
		}
		// if fallback network disappeared connect to default network
		else if ((!wifi_fallback_present) && (wifi_fallback_last_present)) {
			wifi_default();
			led_stop_pattern();
		}
		
		wifi_fallback_last_present = wifi_fallback_present;
	}
	
	wifi_scan_runnning = false;
//	os_printf("scan done\n");

	// start network watchdog again
	os_timer_disarm(&network_check_timer);
	os_timer_setfn(&network_check_timer, (os_timer_func_t *)network_check_timer_func, NULL);
	os_timer_arm(&network_check_timer, NETWORK_CHECK_TIME, 0);
}

void ICACHE_FLASH_ATTR wifi_default() {
	struct station_config stationConf;
	
	wifi_reconnect = false;		// disable wifi_handle_event_cb() from connecting - done here

	// go back to saved network
	os_printf("DEFAULT_SSID\r\n");
	wifi_station_disconnect();
	wifi_set_opmode_current(STATION_MODE);
	os_memset(&stationConf, 0, sizeof(struct station_config));
	wifi_station_get_config(&stationConf);
    
	os_sprintf(stationConf.ssid, "%s", config_ssid);
	os_sprintf(stationConf.password, "%s", config_pass);
    
	wifi_station_set_config_current(&stationConf);
	wifi_station_connect();
}

void ICACHE_FLASH_ATTR wifi_fallback() {
	struct station_config stationConf;

	wifi_reconnect = false;		// disable wifi_handle_event_cb() from connecting - done here

	// try fallback network
	os_printf("FALLBACK_SSID\r\n");
	wifi_station_disconnect();
	wifi_set_opmode_current(STATION_MODE);
	os_memset(&stationConf, 0, sizeof(struct station_config));
	wifi_station_get_config(&stationConf);
	
	os_sprintf(stationConf.ssid, "%s", STA_FALLBACK_SSID);
	os_sprintf(stationConf.password, "%s", STA_FALLBACK_PASS);
	
	wifi_station_set_config_current(&stationConf);
	wifi_station_connect();
}

void ICACHE_FLASH_ATTR wifi_connect(uint8_t* ssid, uint8_t* pass, WifiCallback cb) {
	struct station_config stationConf;

	INFO("WIFI_INIT\r\n");
	wifi_set_opmode(STATION_MODE);
	wifi_station_set_auto_connect(FALSE);
	wifi_cb = cb;
	config_ssid = ssid;
	config_pass = pass;

	os_memset(&stationConf, 0, sizeof(struct station_config));

	os_sprintf(stationConf.ssid, "%s", ssid);
	os_sprintf(stationConf.password, "%s", pass);

	wifi_station_disconnect();
	wifi_station_set_config(&stationConf);

	// start network watchdog
	os_timer_disarm(&network_check_timer);
	os_timer_setfn(&network_check_timer, (os_timer_func_t *)network_check_timer_func, NULL);
	os_timer_arm(&network_check_timer, NETWORK_CHECK_TIME, 0);

	wifi_station_set_auto_connect(TRUE);
	wifi_reconnect = true;	// let wifi_handle_event_cb() handle reconnect on disconnect
	wifi_set_event_handler_cb(wifi_handle_event_cb);
	wifi_station_connect();
	
	led_stop_pattern();
}
