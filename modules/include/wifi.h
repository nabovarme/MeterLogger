/*
 * wifi.h
 *
 *  Created on: Dec 30, 2014
 *      Author: Minh
 */

#ifndef USER_WIFI_H_
#define USER_WIFI_H_
typedef void (*WifiCallback)(uint8_t);
typedef void (*wifi_scan_result_event_cb_t)(const struct bss_info *info);

#ifdef AP
bool ICACHE_FLASH_ATTR acl_check_packet(struct pbuf *p);
#endif	// AP

void wifi_handle_event_cb(System_Event_t *evt);
void ICACHE_FLASH_ATTR wifi_scan_done_cb(void *arg, STATUS status);
void ICACHE_FLASH_ATTR wifi_default();
void ICACHE_FLASH_ATTR wifi_fallback();
void ICACHE_FLASH_ATTR wifi_connect(uint8_t* ssid, uint8_t* pass, WifiCallback cb);
void ICACHE_FLASH_ATTR wifi_softap_config(uint8_t* ssid, uint8_t* pass, uint8_t authmode);
#ifdef AP
void ICACHE_FLASH_ATTR wifi_softap_ip_config(void);
#endif	// AP
sint8_t ICACHE_FLASH_ATTR wifi_get_rssi();
bool ICACHE_FLASH_ATTR wifi_get_status();
void ICACHE_FLASH_ATTR wifi_start_scan();
void ICACHE_FLASH_ATTR wifi_stop_scan();
bool ICACHE_FLASH_ATTR wifi_scan_is_running();
bool ICACHE_FLASH_ATTR wifi_fallback_is_present();
void ICACHE_FLASH_ATTR set_my_auto_connect(bool enabled);

void wifi_scan_result_cb_register(wifi_scan_result_event_cb_t cb);
void wifi_scan_result_cb_unregister();

#endif /* USER_WIFI_H_ */
