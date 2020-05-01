/*
 * wifi.c
 *
 *  Created on: Dec 30, 2014
 *      Author: Minh
 */
#include <esp8266.h>
#include <lwip/ip.h>
#include <lwip/udp.h>
#include <lwip/tcp_impl.h>
#include <netif/etharp.h>
#include <lwip/netif.h>
#include <lwip/lwip_napt.h>
#include <lwip/dns.h>
#include <lwip/app/dhcpserver.h>
#include <lwip/opt.h>
#include <espconn.h>

#include "wifi.h"
#include "mqtt_msg.h"
#include "mqtt_utils.h"
#include "debug.h"
#include "user_config.h"
#include "config.h"
#include "led.h"
#include "tinyprintf.h"

static os_timer_t wifi_scan_timer;
static os_timer_t wifi_scan_timeout_timer;
static os_timer_t wifi_get_rssi_timer;

WifiCallback wifi_cb = NULL;
wifi_scan_result_event_cb_t wifi_scan_result_cb = NULL;

uint8_t channel = 0;
bool wifi_present = false;
volatile bool wifi_fallback_present = false;
bool wifi_fallback_last_present = false;
volatile bool wifi_scan_runnning = false;
//volatile bool wifi_reconnect = false;
volatile sint8_t rssi = 31;	// set rssi to fail state at init time
volatile bool get_rssi_running = false;
volatile bool wifi_default_ok = false;
volatile bool my_auto_connect = true;

static netif_input_fn orig_input_ap;
static netif_linkoutput_fn orig_output_ap;

static ip_addr_t sta_network_addr;
static ip_addr_t sta_network_mask;
static ip_addr_t ap_network_addr;
static ip_addr_t dns_ip;

ICACHE_FLASH_ATTR err_t my_input_ap(struct pbuf *p, struct netif *inp) {
	err_t err;
	
	if (acl_check_packet(p)) {
		err = orig_input_ap(p, inp);
	}
	else {
		pbuf_free(p);
		return ERR_ABRT;
	}
	return err;
}

ICACHE_FLASH_ATTR err_t my_output_ap(struct netif *outp, struct pbuf *p) {
	err_t err;
	err = orig_output_ap(outp, p);
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
	printf("patched!\n");
#endif
}

bool ICACHE_FLASH_ATTR acl_check_packet(struct pbuf *p) {
	struct eth_hdr *mac_h;
	struct ip_hdr *ip_h;
	uint8_t proto;
	struct udp_hdr *udp_h;
	struct tcp_hdr *tcp_h;
	uint16_t src_port = 0;
	uint16_t dest_port = 0;
	uint8_t *packet;
	
	if (p->len < sizeof(struct eth_hdr)) {
		return false;
	}

	mac_h = (struct eth_hdr *)p->payload;

	// Allow ARP
	if (ntohs(mac_h->type) == ETHTYPE_ARP) {
		 return true;
	}

	// Drop anything else if not IPv4
	if (ntohs(mac_h->type) != ETHTYPE_IP) {
		return false;
	}
   
	if (p->len < sizeof(struct eth_hdr)+sizeof(struct ip_hdr)) {
		return false;
	}

	packet = (uint8_t*)p->payload;
	ip_h = (struct ip_hdr *)&packet[sizeof(struct eth_hdr)];
	proto = IPH_PROTO(ip_h);

	switch (proto) {
		case IP_PROTO_UDP:
			if (p->len < sizeof(struct eth_hdr)+sizeof(struct ip_hdr)+sizeof(struct udp_hdr)) {
				return false;
			}
			udp_h = (struct udp_hdr *)&packet[sizeof(struct eth_hdr)+sizeof(struct ip_hdr)];
			src_port = ntohs(udp_h->src);
			dest_port = ntohs(udp_h->dest);
			break;

		case IP_PROTO_TCP:
			if (p->len < sizeof(struct eth_hdr)+sizeof(struct ip_hdr)+sizeof(struct tcp_hdr)) {
				return false;
			}
			tcp_h = (struct tcp_hdr *)&packet[sizeof(struct eth_hdr)+sizeof(struct ip_hdr)];
			src_port = ntohs(tcp_h->src);
			dest_port = ntohs(tcp_h->dest);
			break;

		case IP_PROTO_ICMP:
			src_port = dest_port = 0;
			break;
	}

	// allow dhcp requests to broadcast network
	if ((ip_h->dest.addr == IPADDR_BROADCAST) && (dest_port == 67)) {
		return true;
	}
	
	// allow dns requests to nameserver on uplink network
	if ((ip_h->dest.addr == dns_ip.addr) && (dest_port == 53)) {
		return true;
	}
	
	// deny connections to hosts on uplink network
	if ((ip_h->dest.addr & sta_network_mask.addr) == sta_network_addr.addr) {
#ifdef DEBUG
		printf("dropping packet to uplink network: src: %d.%d.%d.%d dst: %d.%d.%d.%d proto: %s sport:%d dport:%d\n", 
			IP2STR(&ip_h->src), IP2STR(&ip_h->dest), 
			proto == IP_PROTO_TCP ? "TCP" : proto == IP_PROTO_UDP ? "UDP" : "IP4", src_port, dest_port);
#endif
		return false;
	}

	// default allow everything else
    return true;
}

// static functions
static void ICACHE_FLASH_ATTR wifi_get_rssi_timer_func(void *arg);
static void ICACHE_FLASH_ATTR wifi_scan_timer_func(void *arg);
static void ICACHE_FLASH_ATTR wifi_scan_timeout_timer_func(void *arg);

void wifi_handle_event_cb(System_Event_t *evt) {
	static uint8_t wifi_status;
//	static uint8_t wifi_event;
#ifdef DEBUG
	uint8_t mac_str[20];
#endif
	struct station_config stationConf;

	wifi_status = wifi_station_get_connect_status();
//	wifi_event = evt->event;

	memset(&stationConf, 0, sizeof(struct station_config));
	wifi_station_get_config(&stationConf);

#ifdef DEBUG
//	printf("E%dW%dR%d\n", evt->event, wifi_status, evt->event_info.disconnected.reason);
#endif
	switch (evt->event) {
		case EVENT_STAMODE_CONNECTED:
#ifdef DEBUG
			printf("connected to ssid %s\n", evt->event_info.connected.ssid);
#endif
			// set default network status
			if (strncmp((char *)&stationConf.ssid, sys_cfg.sta_ssid, sizeof(sys_cfg.sta_ssid)) == 0) {
				wifi_default_ok = true;
			}
			break;
		case EVENT_STAMODE_DISCONNECTED:
#ifdef DEBUG
			printf("disconnected from ssid %s, reason %d\n", evt->event_info.disconnected.ssid, evt->event_info.disconnected.reason);
#endif
			// set default network status
			if (strncmp((char *)&stationConf.ssid, sys_cfg.sta_ssid, sizeof(sys_cfg.sta_ssid)) == 0) {
				wifi_default_ok = false;
			}
			if (my_auto_connect) {
#ifdef DEBUG
				printf("reconnecting on disconnect\n");
#endif
				wifi_station_connect();	// reconnect on disconnect
			}
			else {
#ifdef DEBUG
				printf("autoconnect not enabled!\n\r");
#endif
			}
			break;
		case EVENT_STAMODE_AUTHMODE_CHANGE:
#ifdef DEBUG
			printf("mode: %d -> %d\n",
					evt->event_info.auth_change.old_mode,
					evt->event_info.auth_change.new_mode);
#endif
			break;
		case EVENT_STAMODE_GOT_IP:		
			// set default network status
#ifdef DEBUG
			printf("got ip:" IPSTR ", netmask:" IPSTR "\n", IP2STR(&evt->event_info.got_ip.ip), IP2STR(&evt->event_info.got_ip.mask));
#endif
			if (strncmp((char *)&stationConf.ssid, sys_cfg.sta_ssid, sizeof(sys_cfg.sta_ssid)) == 0) {
				wifi_default_ok = true;
			}
			// set ap_network_addr from uplink
			sta_network_addr = evt->event_info.got_ip.ip;
			sta_network_mask = evt->event_info.got_ip.mask;
			sta_network_addr.addr &= sta_network_mask.addr;

			wifi_softap_ip_config();

			wifi_station_set_auto_connect(0);	// disale auto connect, we handle reconnect with this event handler
			wifi_station_set_reconnect_policy(0);
			wifi_cb(wifi_status);
			break;
		case EVENT_STAMODE_DHCP_TIMEOUT:
#ifdef DEBUG
			printf("dhcp timeout\n");
#endif
			// set default network status
			if (strncmp((char *)&stationConf.ssid, sys_cfg.sta_ssid, sizeof(sys_cfg.sta_ssid)) == 0) {
				wifi_default_ok = false;
			}
			if (my_auto_connect) {
#ifdef DEBUG
				printf("reconnecting on dhcp timeout\n");
#endif
				wifi_station_disconnect();
				wifi_station_connect();
			}
			break;
	case EVENT_SOFTAPMODE_STACONNECTED:
#ifdef DEBUG
			sprintf(mac_str, MACSTR, MAC2STR(evt->event_info.sta_connected.mac));
			printf("station: %s join, AID = %d\n", mac_str, evt->event_info.sta_connected.aid);
#endif
			patch_netif_ap(my_input_ap, my_output_ap, true);
			break;
	case EVENT_SOFTAPMODE_STADISCONNECTED:
#ifdef DEBUG
			sprintf(mac_str, MACSTR, MAC2STR(evt->event_info.sta_disconnected.mac));
			printf("station: %s disconnected, AID = %d\n", mac_str, evt->event_info.sta_disconnected.aid);
#endif
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
#ifdef DEBUG
	printf ("\t-> %s()\n\r", __FUNCTION__);
#endif
	
	if (!wifi_scan_runnning) {
		// scan for fallback network
		wifi_scan_runnning = true;
//		channel = wifi_get_channel();	// save channel nummber
#ifdef DEBUG
		printf("RSSI: %d\n", wifi_get_rssi());		// DEBUG: should not be here at all
#endif
		// start wifi scan timeout timer
		// hack to avoid wifi_station_scan() sometimes doesnt calling the callback
		os_timer_disarm(&wifi_scan_timeout_timer);
		os_timer_setfn(&wifi_scan_timeout_timer, (os_timer_func_t *)wifi_scan_timeout_timer_func, NULL);
		os_timer_arm(&wifi_scan_timeout_timer, WIFI_SCAN_TIMEOUT, 0);
		if (wifi_station_scan(NULL, wifi_scan_done_cb) == false) {
			// something went wrong, restart scanner
#ifdef DEBUG
			printf("wifi_station_scan() returned false, restarting scanner\n");
#endif
			wifi_scan_runnning = false;
//			led_pattern_a();	// DEBUG slow led blink to show if wifi_station_scan() returned false (indicates scanner restarting)
			wifi_start_scan(WIFI_SCAN_INTERVAL);
		}
		else {
//			led_pattern_b();	// DEBUG fast led blink to show if wifi_scan_done_cb() returned true (indicates wifi scanner running)
		}
//		printf("scan running\n");
	}
}

static void ICACHE_FLASH_ATTR wifi_scan_timeout_timer_func(void *arg) {
#ifdef DEBUG
	printf ("\t-> %s()\n\r", __FUNCTION__);
	printf("scan timeout called\n");
#endif
//	wifi_set_channel(channel);	// restore channel number
	wifi_scan_runnning = false;
	os_timer_disarm(&wifi_scan_timeout_timer);
//	led_stop_pattern();	// DEBUG

	// start wifi scan timer again
	wifi_start_scan(WIFI_SCAN_INTERVAL);
}

void ICACHE_FLASH_ATTR wifi_scan_done_cb(void *arg, STATUS status) {
	struct bss_info *info;
	
#ifdef DEBUG
	printf ("\t-> %s(%x, %d)\n\r", __FUNCTION__, arg == NULL ? 0 : (unsigned int)arg, status);
#endif
	wifi_present = false;
	wifi_fallback_present = false;
	
	// check if fallback network is present
//	if (arg != NULL) {
	if ((arg != NULL) && (status == OK)) {
		info = (struct bss_info *)arg;
		wifi_fallback_present = false;
		
		while (info != NULL) {
			if ((info != NULL) && (info->ssid != NULL) && (strncmp(info->ssid, sys_cfg.sta_ssid, sizeof(sys_cfg.sta_ssid)) == 0)) {
				wifi_present = true;
				channel = info->channel;
//#ifdef DEBUG
//				printf("channel set to %d\n\r", channel);
//#endif
			}
			if ((info != NULL) && (info->ssid != NULL) && (strncmp(info->ssid, STA_FALLBACK_SSID, sizeof(STA_FALLBACK_SSID)) == 0)) {
				wifi_fallback_present = true;
			}
//#ifdef DEBUG
//			printf("channel: %d, ssid: %s, bssid %02x:%02x:%02x:%02x:%02x:%02x, rssi: %d, freq_offset: %d, freqcal_val: %d\n\r", info->channel, 
//				info->ssid,
//				info->bssid[0], 
//				info->bssid[1], 
//				info->bssid[2], 
//				info->bssid[3], 
//				info->bssid[4], 
//				info->bssid[5],
//				info->rssi,
//				info->freq_offset,
//				info->freqcal_val
//			);
//#endif
			
			// handle sending scan results via mqtt
			if (wifi_scan_result_cb) {
				wifi_scan_result_cb(info);
			}
			
			info = info->next.stqe_next;
		}
		wifi_scan_result_cb_unregister();	// done sending via mqtt
		
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
#ifdef DEBUG
		uint8_t s;
		s = wifi_station_get_connect_status();
		printf("wifi present: %s\n", (wifi_present ? "yes" : "no"));
		printf("wifi fallback present: %s\n", (wifi_fallback_present ? "yes" : "no"));
		printf("wifi status: %s (%u)\n", (s == STATION_GOT_IP) ? "connected" : "not connected", s);
#endif
		wifi_fallback_last_present = wifi_fallback_present;
	}
	
//	wifi_set_channel(channel);	// restore channel number
	wifi_scan_runnning = false;
//	printf("scan done\n");
	os_timer_disarm(&wifi_scan_timeout_timer);
//	led_stop_pattern();	// DEBUG

	// start wifi scan timer again
	wifi_start_scan(WIFI_SCAN_INTERVAL);
}

void ICACHE_FLASH_ATTR wifi_default() {
	struct station_config stationConf;

	// go back to saved network
#ifdef DEBUG
	printf("DEFAULT_SSID\r\n");
#endif
	my_auto_connect = false;		// handle_event_cb() based auto connect
	wifi_station_disconnect();
	if (sys_cfg.ap_enabled == true) {
		wifi_set_opmode_current(STATIONAP_MODE);
	}
	else {
		wifi_set_opmode_current(STATION_MODE);
	}
	memset(&stationConf, 0, sizeof(struct station_config));
	wifi_station_get_config(&stationConf);
	
	tfp_snprintf(stationConf.ssid, 32, "%s", sys_cfg.sta_ssid);
	tfp_snprintf(stationConf.password, 64, "%s", sys_cfg.sta_pwd);
	
	wifi_station_set_config_current(&stationConf);
	my_auto_connect = true;		// handle_event_cb() based auto connect
//	wifi_set_channel(channel);	// restore channel number
	wifi_station_connect();	// reconnect
	
	// start wifi rssi timer
	os_timer_disarm(&wifi_get_rssi_timer);
	os_timer_setfn(&wifi_get_rssi_timer, (os_timer_func_t *)wifi_get_rssi_timer_func, NULL);
	os_timer_arm(&wifi_get_rssi_timer, RSSI_CHECK_INTERVAL, 1);
}

void ICACHE_FLASH_ATTR wifi_fallback() {
	struct station_config stationConf;

	// try fallback network
#ifdef DEBUG
	printf("FALLBACK_SSID\r\n");
#endif
	my_auto_connect = false;		// handle_event_cb() based auto connect
	wifi_station_disconnect();
	if (sys_cfg.ap_enabled == true) {
		wifi_set_opmode_current(STATIONAP_MODE);
	}
	else {
		wifi_set_opmode_current(STATION_MODE);
	}
	memset(&stationConf, 0, sizeof(struct station_config));
	wifi_station_get_config(&stationConf);
	
	tfp_snprintf(stationConf.ssid, 32, "%s", STA_FALLBACK_SSID);
	tfp_snprintf(stationConf.password, 64, "%s", STA_FALLBACK_PASS);
	
	wifi_station_set_config_current(&stationConf);

	my_auto_connect = true;		// handle_event_cb() based auto connect
	wifi_station_connect();	// reconnect
}

void ICACHE_FLASH_ATTR wifi_connect(WifiCallback cb) {
	struct station_config stationConf;

	INFO("WIFI_INIT\r\n");
	wifi_set_opmode_current(NULL_MODE);

	if (sys_cfg.ap_enabled) {
		wifi_set_opmode_current(STATIONAP_MODE);
	}
	else {
		wifi_set_opmode_current(STATION_MODE);
	}

	wifi_cb = cb;

	memset(&stationConf, 0, sizeof(struct station_config));

	tfp_snprintf(stationConf.ssid, 32, "%s", sys_cfg.sta_ssid);
	tfp_snprintf(stationConf.password, 64, "%s", sys_cfg.sta_pwd);

	wifi_station_set_config(&stationConf);	// save to flash so it will reconnect at boot
	wifi_station_set_config_current(&stationConf);

	// start wifi scan timer
	wifi_start_scan(WIFI_SCAN_INTERVAL_LONG);	// longer time to let it connect to wifi first

	wifi_set_event_handler_cb(wifi_handle_event_cb);
	my_auto_connect = true;	// handle_event_cb() based auto connect

//	wifi_set_channel(channel);	// restore channel number
	wifi_station_connect();

	if (wifi_station_dhcpc_status() == DHCP_STOPPED) {
#ifdef DEBUG
		printf("starting dhcp client\n\r");
#endif
		wifi_station_dhcpc_set_maxtry(255);
		wifi_station_dhcpc_start();
	}
	
	// start wifi rssi timer
	os_timer_disarm(&wifi_get_rssi_timer);
	os_timer_setfn(&wifi_get_rssi_timer, (os_timer_func_t *)wifi_get_rssi_timer_func, NULL);
	os_timer_arm(&wifi_get_rssi_timer, RSSI_CHECK_INTERVAL, 1);

//	led_stop_pattern();
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
	ap_conf.channel = channel;
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
	}
	// increment ap_network_addr if same as sta_network_addr
	if (ip_addr_cmp(&sta_network_addr, &ap_network_addr)) {
		ip4_addr3(&ap_network_addr) += 1;
	}

	// if we have not got a dns server set to default
	dns_ip = dns_getserver(0);
	if (dns_ip.addr == 0) {
		// Google's DNS as default, as long as we havn't got one from DHCP
		IP4_ADDR(&dns_ip, 8, 8, 8, 8);
	}
	dhcps_set_DNS(&dns_ip);
	espconn_dns_setserver(0, &dns_ip);
#ifdef DEBUG
	printf("sta net:" IPSTR ",ap net:" IPSTR ",dns:" IPSTR "\n", IP2STR(&sta_network_addr), IP2STR(&ap_network_addr), IP2STR(&dns_ip));
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

void ICACHE_FLASH_ATTR wifi_start_scan(uint32_t interval) {
#ifdef DEBUG
	printf ("\t-> %s(%s)\n\r", __FUNCTION__, (interval == WIFI_SCAN_INTERVAL) ? "WIFI_SCAN_INTERVAL" : "WIFI_SCAN_INTERVAL_LONG");
#endif
	// start wifi scan timer
	os_timer_disarm(&wifi_scan_timer);
	os_timer_setfn(&wifi_scan_timer, (os_timer_func_t *)wifi_scan_timer_func, NULL);
	os_timer_arm(&wifi_scan_timer, interval, 0);
}

void ICACHE_FLASH_ATTR wifi_stop_scan() {
#ifdef DEBUG
	printf ("\t-> %s()\n\r", __FUNCTION__);
#endif
	// stop wifi scan timeout timer and wifi scan timer
	os_timer_disarm(&wifi_scan_timeout_timer);
	os_timer_disarm(&wifi_scan_timer);
	wifi_scan_runnning = false;
}

bool ICACHE_FLASH_ATTR wifi_scan_is_running() {
	return wifi_scan_runnning;
}

void ICACHE_FLASH_ATTR wifi_fallback_force_reset_state() {	// helper function to let watchdog reset the state of the wifi_fallback_*
	wifi_fallback_present = false;
	wifi_fallback_last_present = false;
}

bool ICACHE_FLASH_ATTR wifi_fallback_is_present() {
	return wifi_fallback_present;
}

void ICACHE_FLASH_ATTR set_my_auto_connect(bool enabled) {
	my_auto_connect = enabled;
}

void ICACHE_FLASH_ATTR wifi_destroy() {
	// only call this before restarting system_restart() to stop all timers
	os_timer_disarm(&wifi_scan_timer);
	os_timer_disarm(&wifi_scan_timeout_timer);
	os_timer_disarm(&wifi_get_rssi_timer);
}

void wifi_scan_result_cb_register(wifi_scan_result_event_cb_t cb) {
	wifi_scan_result_cb = cb;
}

void wifi_scan_result_cb_unregister(wifi_scan_result_event_cb_t cb) {
	wifi_scan_result_cb = NULL;
}

#ifdef DEBUG
void ICACHE_FLASH_ATTR debug_print_wifi_ip() {
	struct netif *nif;
	
	for (nif = netif_list; nif != NULL; nif = nif->next) {
		printf("nif %c%c%d (mac: %02x:%02x:%02x:%02x:%02x:%02x): " IPSTR "%s%s\n", 
			nif->name[0], 
			nif->name[1], 
			nif->num, 
			nif->hwaddr[0], 
			nif->hwaddr[1], 
			nif->hwaddr[2], 
			nif->hwaddr[3], 
			nif->hwaddr[4], 
			nif->hwaddr[5], 
			IP2STR(&nif->ip_addr.addr), 
			nif->dhcp != NULL ? ", dhcp enabled" : "", 
			nif->num == netif_default->num ? ", default" : ""
		);
	}
}

void ICACHE_FLASH_ATTR debug_print_wifi_config() {
	printf("ssid: %s, pass: %s\n\r", sys_cfg.sta_ssid, sys_cfg.sta_pwd);
}
#endif	// DEBUG
