/*
 * wifi.c
 *
 *  Created on: Dec 30, 2014
 *      Author: Minh
 */
#include <esp8266.h>
#include "wifi.h"
#include "mqtt_msg.h"
#include "mqtt_utils.h"
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

static netif_input_fn orig_input_ap;
static netif_linkoutput_fn orig_output_ap;

static ip_addr_t sta_network_addr;
static ip_addr_t ap_network_addr;
static ip_addr_t dns_ip;

ICACHE_FLASH_ATTR err_t my_input_ap (struct pbuf *p, struct netif *inp) {
	err_t err;
	os_printf("Got packet from STA, len: %d\r\n", p->len);
	err = orig_input_ap (p, inp);
	return err;
}

ICACHE_FLASH_ATTR err_t my_output_ap (struct netif *outp, struct pbuf *p) {
	err_t err;
	os_printf("Send packet to STA, len: %d\r\n", p->len);
	err = orig_output_ap (outp, p);
	return err;
}


ICACHE_FLASH_ATTR static void patch_netif_ap(netif_input_fn ifn, netif_linkoutput_fn ofn, bool nat) {
	struct netif *nif;
	ip_addr_t ap_ip;

	ap_ip = ap_network_addr;
	ip4_addr4(&ap_ip) = 1;
	
	for (nif = netif_list; nif != NULL && nif->ip_addr.addr != ap_ip.addr; nif = nif->next) {
		// skip
	}
	if (nif == NULL) {
		return;
	}

	nif->napt = nat ? 1 : 0;
	if (nif->input != ifn) {
		orig_input_ap = nif->input;
		nif->input = ifn;
	}
	if (nif->linkoutput != ofn) {
		orig_output_ap = nif->linkoutput;
		nif->linkoutput = ofn;
	}
#ifdef DEBUG
	os_printf("patched!\n");
#endif
}

void wifi_handle_event_cb(System_Event_t *evt) {
    uint8_t mac_str[20];
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
			// set default network status
    		if (os_strncmp(&stationConf.ssid, sys_cfg.sta_ssid, sizeof(sys_cfg.sta_ssid)) == 0) {
    			wifi_default_ok = false;
    		}
			break;
		case EVENT_STAMODE_GOT_IP:		
			// set default network status
    		if (os_strncmp(&stationConf.ssid, sys_cfg.sta_ssid, sizeof(sys_cfg.sta_ssid)) == 0) {
    			wifi_default_ok = true;
    		}
			sta_network_addr = evt->event_info.got_ip.ip;
			ip4_addr4(&sta_network_addr) = 0;

			UTILS_StrToIP(AP_NETWORK, &ap_network_addr);

			if (sta_network_addr.addr == 0) {
				ip4_addr3(&ap_network_addr) += 1;
			}

			// set dhcp dns to the one supplied from uplink
			dns_ip = dns_getserver(0);
			if (dns_ip.addr == 0) {
				// Google's DNS as default, as long as we havn't got one from DHCP
				IP4_ADDR(&dns_ip, 8, 8, 8, 8);
			}
			dhcps_set_DNS(&dns_ip);
			espconn_dns_setserver(0, &dns_ip);
#ifdef DEBUG
			os_printf("sta net:" IPSTR ",ap net:" IPSTR ",dns:" IPSTR "\n", IP2STR(&sta_network_addr), IP2STR(&ap_network_addr), IP2STR(&dns_ip));
#endif
			wifi_softap_ip_config();
			wifi_cb(wifi_status);
			break;
		case EVENT_SOFTAPMODE_STACONNECTED:
#ifdef DEBUG
			os_sprintf(mac_str, MACSTR, MAC2STR(evt->event_info.sta_connected.mac));
			os_printf("station: %s join, AID = %d\n", mac_str, evt->event_info.sta_connected.aid);
#endif
			patch_netif_ap(my_input_ap, my_output_ap, true);
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

	// go back to saved network
	os_printf("DEFAULT_SSID\r\n");
	wifi_station_disconnect();
	wifi_set_opmode_current(STATIONAP_MODE);
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
	wifi_set_opmode_current(STATIONAP_MODE);
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
	wifi_set_opmode(STATIONAP_MODE);
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
	wifi_set_event_handler_cb(wifi_handle_event_cb);
	wifi_station_connect();
	
	// start wifi rssi timer
	os_timer_disarm(&wifi_get_rssi_timer);
	os_timer_setfn(&wifi_get_rssi_timer, (os_timer_func_t *)wifi_get_rssi_timer_func, NULL);
	os_timer_arm(&wifi_get_rssi_timer, RSSI_CHECK_INTERVAL, 1);

	led_stop_pattern();
}

void ICACHE_FLASH_ATTR wifi_softap_config(uint8_t* ssid, uint8_t* pass, uint8_t authmode) {
	struct softap_config ap_conf;
	
	wifi_softap_get_config(&ap_conf);
	memset(ap_conf.ssid, 0, sizeof(ap_conf.ssid));
	memset(ap_conf.password, 0, sizeof(ap_conf.password));
	tfp_snprintf(ap_conf.ssid, 32, ssid);
	tfp_snprintf(ap_conf.password, 64, pass);
	ap_conf.authmode = authmode;
	ap_conf.ssid_len = 0;
	ap_conf.beacon_interval = 100;
	ap_conf.channel = 1;
	ap_conf.max_connection = 8;
	ap_conf.ssid_hidden = 0;

	wifi_softap_set_config(&ap_conf);
}


void ICACHE_FLASH_ATTR wifi_softap_ip_config(void) {
	struct ip_info info;
	struct dhcps_lease dhcp_lease;
	struct netif *nif;

	// find the netif of the AP (that with num != 0)
	for (nif = netif_list; nif != NULL && nif->num == 0; nif = nif->next) {
		// skip
	}
	if (nif == NULL) {
		return;
	}
	// if is not 1, set it to 1. 
	// kind of a hack, but the Espressif-internals expect it like this (hardcoded 1).
	nif->num = 1;

	// configure AP dhcp
	wifi_softap_dhcps_stop();

	// if we have not got an ip set ap ip to default
	if (ap_network_addr.addr == 0) {
		UTILS_StrToIP(AP_NETWORK, &ap_network_addr);
#ifdef DEBUG
		os_printf("defaulting ap_network_addr\n");
#endif
	}

	// if we have not got a dns server set to default
//	if (dns_ip.addr == 0) {
		// Google's DNS as default, as long as we havn't got one from DHCP
		IP4_ADDR(&dns_ip, 8, 8, 8, 8);
		dhcps_set_DNS(&dns_ip);
		espconn_dns_setserver(0, &dns_ip);
//	}
#ifdef DEBUG
	os_printf("wifi_softap_ip_config() sta net:" IPSTR ",ap net:" IPSTR ",dns:" IPSTR "\n", IP2STR(&sta_network_addr), IP2STR(&ap_network_addr), IP2STR(&dns_ip));
#endif

	info.ip = ap_network_addr;
	ip4_addr4(&info.ip) = 1;
	info.gw = info.ip;
	IP4_ADDR(&info.netmask, 255, 255, 255, 0);

	wifi_set_ip_info(nif->num, &info);

	dhcp_lease.start_ip = ap_network_addr;
	ip4_addr4(&dhcp_lease.start_ip) = 2;
	dhcp_lease.end_ip = ap_network_addr;
	ip4_addr4(&dhcp_lease.end_ip) = 254;
	wifi_softap_set_dhcps_lease(&dhcp_lease);

	wifi_softap_dhcps_start();
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

