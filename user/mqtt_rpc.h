#ifndef MQTT_RPC_H_
#define MQTT_RPC_H_

ICACHE_FLASH_ATTR void mqtt_rpc_ping(MQTT_Client *client);
ICACHE_FLASH_ATTR void mqtt_rpc_version(MQTT_Client *client);
ICACHE_FLASH_ATTR void mqtt_rpc_uptime(MQTT_Client *client);
ICACHE_FLASH_ATTR void mqtt_rpc_vdd(MQTT_Client *client);
ICACHE_FLASH_ATTR void mqtt_rpc_rssi(MQTT_Client *client);
ICACHE_FLASH_ATTR void mqtt_rpc_ssid(MQTT_Client *client);
ICACHE_FLASH_ATTR void mqtt_rpc_scan(MQTT_Client *client);
ICACHE_FLASH_ATTR void mqtt_rpc_set_ssid(MQTT_Client *client, char *ssid);
ICACHE_FLASH_ATTR void mqtt_rpc_set_pwd(MQTT_Client *client, char *password);
ICACHE_FLASH_ATTR void mqtt_rpc_set_ssid_pwd(MQTT_Client *client, char *ssid_pwd);
ICACHE_FLASH_ATTR void mqtt_rpc_reconnect(MQTT_Client *client);
ICACHE_FLASH_ATTR void mqtt_rpc_wifi_status(MQTT_Client *client);
#ifdef AP
ICACHE_FLASH_ATTR void mqtt_rpc_ap_status(MQTT_Client *client);
ICACHE_FLASH_ATTR void mqtt_rpc_start_ap(MQTT_Client *client, char *mesh_ssid);
ICACHE_FLASH_ATTR void mqtt_rpc_stop_ap(MQTT_Client *client);
#endif	// AP
ICACHE_FLASH_ATTR void mqtt_rpc_mem(MQTT_Client *client);
ICACHE_FLASH_ATTR void mqtt_rpc_crypto(MQTT_Client *client);
ICACHE_FLASH_ATTR void mqtt_rpc_reset_reason(MQTT_Client *client);
#ifndef IMPULSE
ICACHE_FLASH_ATTR void mqtt_rpc_set_cron(MQTT_Client *client, char *query);
ICACHE_FLASH_ATTR void mqtt_rpc_clear_cron(MQTT_Client *client);
ICACHE_FLASH_ATTR void mqtt_rpc_cron(MQTT_Client *client);
ICACHE_FLASH_ATTR void mqtt_rpc_open(MQTT_Client *client);
ICACHE_FLASH_ATTR void mqtt_rpc_open_until(MQTT_Client *client, char *value);
ICACHE_FLASH_ATTR void mqtt_rpc_open_until_delta(MQTT_Client *client, char *value);
ICACHE_FLASH_ATTR void mqtt_rpc_close(MQTT_Client *client);
ICACHE_FLASH_ATTR void mqtt_rpc_status(MQTT_Client *client);
#endif

#endif /* MQTT_RPC_H_ */

