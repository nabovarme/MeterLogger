/*
 * wifi.h
 *
 *  Created on: Dec 30, 2014
 *      Author: Minh
 */

#ifndef USER_WIFI_H_
#define USER_WIFI_H_

#include <lwip/ip.h>

#define WIFI_SCAN_INTERVAL 5000
#define WIFI_SCAN_INTERVAL_LONG 20000
#define WIFI_SCAN_TIMEOUT 60000
#define RSSI_CHECK_INTERVAL 10000

extern uint32_t disconnect_count;

typedef void (*WifiCallback)(uint8_t);
typedef void (*wifi_scan_result_event_cb_t)(const struct bss_info *info);

bool ICACHE_FLASH_ATTR acl_check_packet(struct pbuf *p);

void wifi_handle_event_cb(System_Event_t *evt);
void ICACHE_FLASH_ATTR wifi_scan_done_cb(void *arg, STATUS status);
void ICACHE_FLASH_ATTR wifi_default();
void ICACHE_FLASH_ATTR wifi_fallback();
void ICACHE_FLASH_ATTR wifi_connect(WifiCallback cb);
void ICACHE_FLASH_ATTR wifi_softap_config(uint8_t* ssid, uint8_t* pass, uint8_t authmode);
void ICACHE_FLASH_ATTR wifi_softap_ip_config(void);
sint8_t ICACHE_FLASH_ATTR wifi_get_rssi();
uint32_t ICACHE_FLASH_ATTR wifi_get_status();
void ICACHE_FLASH_ATTR wifi_start_scan(uint32_t interval);
void ICACHE_FLASH_ATTR wifi_stop_scan();
bool ICACHE_FLASH_ATTR wifi_scan_is_running();
void ICACHE_FLASH_ATTR wifi_fallback_force_reset_state();
bool ICACHE_FLASH_ATTR wifi_fallback_is_present();
void ICACHE_FLASH_ATTR set_my_auto_connect(bool enabled);
void ICACHE_FLASH_ATTR wifi_destroy();

void wifi_scan_result_cb_register(wifi_scan_result_event_cb_t cb);
void wifi_scan_result_cb_unregister();
#ifdef DEBUG
void ICACHE_FLASH_ATTR debug_print_wifi_ip();
void ICACHE_FLASH_ATTR debug_print_wifi_config();
#endif
void ICACHE_FLASH_ATTR cnx_csa_fn_wrapper(void);
#endif /* USER_WIFI_H_ */
