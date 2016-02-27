/*
 * wifi.c
 *
 *  Created on: Dec 30, 2014
 *      Author: Minh
 */
#include "wifi.h"
#include "user_interface.h"
#include "osapi.h"
#include "espconn.h"
#include "os_type.h"
//#include <ping.h>
#include "mem.h"
#include "mqtt_msg.h"
#include "debug.h"
#include "user_config.h"
#include "config.h"

#define NETWORK_CHECK_TIME 10000

static os_timer_t wifi_check_timer;
static os_timer_t wifi_reconnect_default_timer;
static os_timer_t network_check_timer;

WifiCallback wifi_cb = NULL;
uint8_t* config_ssid;
uint8_t* config_pass;
static uint8_t wifi_status = STATION_IDLE;
static uint8_t last_wifi_status = STATION_IDLE;
char wifi_fallback_present = 0;
char wifi_fallback_last_present = 0;

static void ICACHE_FLASH_ATTR wifi_check_timer_func(void *arg) {
	struct ip_info ipConfig;

	os_timer_disarm(&wifi_check_timer);
	
	wifi_get_ip_info(STATION_IF, &ipConfig);
	wifi_status = wifi_station_get_connect_status();
	if (wifi_status == STATION_GOT_IP && ipConfig.ip.addr != 0) {
		os_timer_setfn(&wifi_check_timer, (os_timer_func_t *)wifi_check_timer_func, NULL);
		os_timer_arm(&wifi_check_timer, 2000, 0);
	}
	else {
		if (wifi_station_get_connect_status() == STATION_WRONG_PASSWORD) {
			INFO("STATION_WRONG_PASSWORD\r\n");
			wifi_station_connect();
		}
		else if (wifi_station_get_connect_status() == STATION_NO_AP_FOUND) {
			INFO("STATION_NO_AP_FOUND\r\n");
			wifi_station_connect();
		}
		else if (wifi_station_get_connect_status() == STATION_CONNECT_FAIL) {
			INFO("STATION_CONNECT_FAIL\r\n");
			wifi_station_connect();
		}
		else {
			INFO("STATION_IDLE\r\n");
		}

		os_timer_setfn(&wifi_check_timer, (os_timer_func_t *)wifi_check_timer_func, NULL);
		os_timer_arm(&wifi_check_timer, 500, 0);
	}
	
	if (wifi_status != last_wifi_status) {
		last_wifi_status = wifi_status;
		if (wifi_cb) {
			wifi_cb(wifi_status);
		}
	}
}

static void ICACHE_FLASH_ATTR network_check_timer_func(void *arg) {
	struct scan_config config;

	// scan for fallback network
	os_memset(&config, 0, sizeof(config));
	config.ssid = STA_FALLBACK_SSID;
	wifi_station_scan(&config, wifi_scan_done_cb);
}

void ICACHE_FLASH_ATTR wifi_scan_done_cb(void *arg, STATUS status) {
	struct bss_info *info;
	
	wifi_fallback_present = 0;
	
	// check if fallback network is present
	if ((arg != NULL) && (status == OK)) {
		info = (struct bss_info *)arg;
		//info = info->next.stqe_next;	// ignore first. DEBUG: appearently not needed in newer SDK versions
		wifi_fallback_present == 0;
		
		while ((info != NULL) && (wifi_fallback_present == 0)) {
			info = info->next.stqe_next;
			if ((info != NULL) && (info->ssid != NULL) && (os_strncmp(info->ssid, STA_FALLBACK_SSID, sizeof(STA_FALLBACK_SSID)) == 0)) {
				wifi_fallback_present = 1;
			}
		}
		
		// if fallback network appeared connect to it
		if ((wifi_fallback_present == 1) && (wifi_fallback_last_present == 0)) {
			wifi_fallback();
		}
		// if fallback network disappeared connect to default network
		else if ((wifi_fallback_present == 0) && (wifi_fallback_last_present == 1)) {
			wifi_default();
		}
		
		wifi_fallback_last_present = wifi_fallback_present;
	}
	os_timer_disarm(&network_check_timer);
	os_timer_setfn(&network_check_timer, (os_timer_func_t *)network_check_timer_func, NULL);
	os_timer_arm(&network_check_timer, NETWORK_CHECK_TIME, 0);
}

void ICACHE_FLASH_ATTR wifi_default() {
	struct station_config stationConf;

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

	wifi_station_set_config(&stationConf);

	// start wifi link watchdog
	os_timer_disarm(&wifi_check_timer);
	os_timer_setfn(&wifi_check_timer, (os_timer_func_t *)wifi_check_timer_func, NULL);
	os_timer_arm(&wifi_check_timer, 1000, 0);
	
	// start network watchdog
	os_timer_disarm(&network_check_timer);
	os_timer_setfn(&network_check_timer, (os_timer_func_t *)network_check_timer_func, NULL);
	os_timer_arm(&network_check_timer, NETWORK_CHECK_TIME, 0);

	wifi_station_set_auto_connect(TRUE);
	wifi_station_connect();
}
