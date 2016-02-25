/*
 * wifi.h
 *
 *  Created on: Dec 30, 2014
 *      Author: Minh
 */

#ifndef USER_WIFI_H_
#define USER_WIFI_H_
#include "os_type.h"
typedef void (*WifiCallback)(uint8_t);

static void ICACHE_FLASH_ATTR wifi_reconnect_default_timer_func(void *arg);

static void ICACHE_FLASH_ATTR network_check_timer_func(void *arg);
void ICACHE_FLASH_ATTR wifi_scan_done_cb(void *arg, STATUS status);
void ICACHE_FLASH_ATTR wifi_fallback();
void ICACHE_FLASH_ATTR WIFI_Connect(uint8_t* ssid, uint8_t* pass, WifiCallback cb);
//void ICACHE_FLASH_ATTR user_ping_recv(void *arg, void *pdata);
//void ICACHE_FLASH_ATTR user_ping_sent(void *arg, void *pdata);
//void ICACHE_FLASH_ATTR user_test_ping(void);

#endif /* USER_WIFI_H_ */
