/*
 * wifi.h
 *
 *  Created on: Dec 30, 2014
 *      Author: Minh
 */

#ifndef USER_WIFI_H_
#define USER_WIFI_H_
typedef void (*WifiCallback)(uint8_t);

void wifi_handle_event_cb(System_Event_t *evt);
void ICACHE_FLASH_ATTR wifi_scan_done_cb(void *arg, STATUS status);
void ICACHE_FLASH_ATTR wifi_default();
void ICACHE_FLASH_ATTR wifi_fallback();
void ICACHE_FLASH_ATTR wifi_connect(uint8_t* ssid, uint8_t* pass, WifiCallback cb);

#endif /* USER_WIFI_H_ */
