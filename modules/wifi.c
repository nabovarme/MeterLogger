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

#define NETWORK_CHECK_TIME 8000

static os_timer_t wifi_check_timer;
static os_timer_t wifi_reconnect_default_timer;
static os_timer_t network_check_timer;

WifiCallback wifiCb = NULL;
uint8_t* config_ssid;
uint8_t* config_pass;
static uint8_t wifiStatus = STATION_IDLE;
static uint8_t lastWifiStatus = STATION_IDLE;
int networkStatus = 0;		// check network state
char wifi_fallback_present = 0;
char wifi_fallback_last_present = 0;

static void ICACHE_FLASH_ATTR wifi_check_timer_func(void *arg)
{
	struct ip_info ipConfig;

	os_timer_disarm(&wifi_check_timer);
	wifi_get_ip_info(STATION_IF, &ipConfig);
	wifiStatus = wifi_station_get_connect_status();
	if (wifiStatus == STATION_GOT_IP && ipConfig.ip.addr != 0)
	{

		//os_printf("\n\rUP\n\r");
		os_timer_setfn(&wifi_check_timer, (os_timer_func_t *)wifi_check_timer_func, NULL);
		os_timer_arm(&wifi_check_timer, 2000, 0);


	}
	else
	{
		//os_printf("\n\rDOWN\n\r");
		if(wifi_station_get_connect_status() == STATION_WRONG_PASSWORD)
		{

			INFO("STATION_WRONG_PASSWORD\r\n");
			wifi_station_connect();


		}
		else if(wifi_station_get_connect_status() == STATION_NO_AP_FOUND)
		{

			INFO("STATION_NO_AP_FOUND\r\n");
			if (wifi_fallback_present == 0) {
				os_timer_disarm(&wifi_reconnect_default_timer);
				os_timer_setfn(&wifi_reconnect_default_timer, (os_timer_func_t *)wifi_reconnect_default_timer_func, NULL);
				os_timer_arm(&wifi_reconnect_default_timer, 0, 0);
			}
			else {
				wifi_station_connect();
			}

		}
		else if(wifi_station_get_connect_status() == STATION_CONNECT_FAIL)
		{

			INFO("STATION_CONNECT_FAIL\r\n");
			if (wifi_fallback_present == 0) {
				os_timer_disarm(&wifi_reconnect_default_timer);
				os_timer_setfn(&wifi_reconnect_default_timer, (os_timer_func_t *)wifi_reconnect_default_timer_func, NULL);
				os_timer_arm(&wifi_reconnect_default_timer, 0, 0);
			}
			else {
				wifi_station_connect();
			}

		}
		else
		{
			INFO("STATION_IDLE\r\n");
		}

		os_timer_setfn(&wifi_check_timer, (os_timer_func_t *)wifi_check_timer_func, NULL);
		os_timer_arm(&wifi_check_timer, 500, 0);
	}
	if(wifiStatus != lastWifiStatus){
		lastWifiStatus = wifiStatus;
		if(wifiCb)
			wifiCb(wifiStatus);
	}
}

static void ICACHE_FLASH_ATTR wifi_reconnect_default_timer_func(void *arg) {
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

static void ICACHE_FLASH_ATTR network_check_timer_func(void *arg) {
//	user_test_ping();
	wifi_station_scan(NULL, wifi_scan_done_cb);
}

void ICACHE_FLASH_ATTR wifi_scan_done_cb(void *arg, STATUS status) {
	struct bss_info *info = (struct bss_info *)arg;
	
	if (status == OK) {
		info = info->next.stqe_next;	// ignore first
		
		while (info != NULL) {
			info = info->next.stqe_next;
			if ((info != NULL) && (info->ssid != NULL) && (os_strncmp(info->ssid, STA_FALLBACK_SSID, sizeof(STA_FALLBACK_SSID)) == 0)) {
				wifi_fallback_present = 1;
				if (wifi_fallback_last_present == 0) {	// if fallback network just appeared
					wifi_fallback();					// change to it...
					wifi_fallback_last_present = 1;
				}
			}
			else {
				wifi_fallback_present = 0;
				wifi_fallback_last_present = 0;
			}
		}
	}
	os_timer_disarm(&network_check_timer);
	os_timer_setfn(&network_check_timer, (os_timer_func_t *)network_check_timer_func, NULL);
	os_timer_arm(&network_check_timer, NETWORK_CHECK_TIME, 0);
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

void ICACHE_FLASH_ATTR wifi_connect(uint8_t* ssid, uint8_t* pass, WifiCallback cb)
{
	struct station_config stationConf;

	INFO("WIFI_INIT\r\n");
	wifi_set_opmode(STATION_MODE);
	wifi_station_set_auto_connect(FALSE);
	wifiCb = cb;
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

/*
void ICACHE_FLASH_ATTR user_ping_recv(void *arg, void *pdata) {
	struct ping_resp *ping_resp = pdata;
	struct ping_option *ping_opt = arg;
	
	if (ping_resp->ping_err == -1) {
		//os_printf("ping host fail \r\n");
		networkStatus = -1;	// ping host failed state
	}
	else {
	    //os_printf("ping recv: byte = %d, time = %d ms \r\n",ping_resp->bytes,ping_resp->resp_time);
		networkStatus = 1;	// network ok state
	}
}

void ICACHE_FLASH_ATTR user_ping_sent(void *arg, void *pdata) {
	//os_printf("user ping finish \r\n");
	if ((networkStatus == -1) && (wifi_fallback_enabled == 0)) {
		wifi_fallback();
		wifi_station_connect();
	}
}


void ICACHE_FLASH_ATTR user_test_ping(void) {
	struct ping_option *ping_opt = NULL;
	const char* ping_ip = "8.8.8.8";
	
	ping_opt = (struct ping_option *)os_zalloc(sizeof(struct ping_option));
	
	ping_opt->count = 2;    //  try to ping how many times
	ping_opt->coarse_time = 2;  // ping interval
	ping_opt->ip = ipaddr_addr(ping_ip);
	
	ping_regist_recv(ping_opt,user_ping_recv);
	ping_regist_sent(ping_opt,user_ping_sent);
	
	ping_start(ping_opt);
	
}
*/
