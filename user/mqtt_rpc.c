#include <esp8266.h>
#include "driver/uart.h"
#include "mqtt.h"
#include "mqtt_rpc.h"
#include "crypto/crypto.h"
#include "tinyprintf.h"
#include "wifi.h"
#include "config.h"
#include "user_config.h"
#include "unix_time.h"
#include "cron/cron.h"
#include "ac/ac_out.h"
#include "rtc_mem.h"
#include "utils.h"
#include "version.h"
#include "user_main.h"
#include "icmp_ping.h"
#include "led.h"
#include "ota.h"

#ifdef EN61107
#include "en61107_request.h"
#elif defined IMPULSE
// nothing
#else
#include "kmp_request.h"
#endif

#ifdef DEBUG_STACK_TRACE
#include "exception_handler.h"
#endif	// DEBUG_STACK_TRACE

static os_timer_t fallback_ap_timer;
bool fallback_ap_is_running = false;
bool ota_in_progress = false;

static os_timer_t mqtt_restart_ack_timer;

ICACHE_FLASH_ATTR void static mqtt_restart_ack_timer_func(void *arg) {
	MQTT_Client *client = (MQTT_Client *)arg;

	MQTT_DeleteClient(client);
#ifndef IMPULSE
#ifdef EN61107
	en61107_request_destroy();
#else
	kmp_request_destroy();
#endif
#endif	// IMPULSE
	system_restart_defered();
}

ICACHE_FLASH_ATTR
void mqtt_rpc_ping(MQTT_Client *client) {
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L];
	int mqtt_message_l;

#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/ping/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/ping/v2/%s/%llu", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/ping/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif
	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));
	// encrypt and send
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
	MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
}

ICACHE_FLASH_ATTR
void mqtt_rpc_version(MQTT_Client *client) {
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L];
	int mqtt_message_l;

#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/version/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/version/v2/%s/%llu", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/version/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif
	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));

	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%s-%s-%s-%s", system_get_sdk_version(), VERSION, LWIP_VERSION, HW_MODEL);

	// encrypt and send
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
	MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
}

ICACHE_FLASH_ATTR
void mqtt_rpc_uptime(MQTT_Client *client) {
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L];
	int mqtt_message_l;

#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/uptime/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/uptime/v2/%s/%llu", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/uptime/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif
	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));
	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%llu", get_uptime());
	// encrypt and send
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
	MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
}

ICACHE_FLASH_ATTR
void mqtt_rpc_vdd(MQTT_Client *client) {
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L];
	int mqtt_message_l;
	

#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/vdd/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/vdd/v2/%s/%llu", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/vdd/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif
	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));
	tfp_snprintf(cleartext, 8, "%.3f", (float)system_get_vdd33() / 1000.0);
#ifdef DEBUG
	printf("vdd: %s\n", cleartext);
#endif
	// encrypt and send
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
	MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
}

ICACHE_FLASH_ATTR
void mqtt_rpc_rssi(MQTT_Client *client) {
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L];
	int mqtt_message_l;
		
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/rssi/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/rssi/v2/%s/%llu", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/rssi/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif
	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));
	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%d", wifi_get_rssi());
	// encrypt and send
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
	MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
}

ICACHE_FLASH_ATTR
void mqtt_rpc_ssid(MQTT_Client *client) {
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L];
	int mqtt_message_l;
		
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/ssid/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/ssid/v2/%s/%llu", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/ssid/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif
	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));
	
	// Always report the configured target SSID, not the fallback one
	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%s", sys_cfg.sta_ssid);
	
	// encrypt and send
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen((char*)cleartext) + 1);
	MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
}

ICACHE_FLASH_ATTR
void mqtt_rpc_scan(MQTT_Client *client) {
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L];
	int mqtt_message_l;

	if (fallback_ap_is_running) return; // Prevent Fatal Exception 9

	// --- Send immediate acknowledgement reply before scanning ---
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/scan/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/scan/v2/%s/%llu", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/scan/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif
	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext)); // Empty cleartext

	// encrypt and send
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
	MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
	// -----------------------------------------------------------

	// register wifi scan callback to handle scan results when we do normal scanning in wifi.c
	// wifi_scan_result_cb_unregister() is called from wifi.c when scan is done
	wifi_scan_result_cb_register(mqtt_send_wifi_scan_results_cb);
}

ICACHE_FLASH_ATTR
void mqtt_rpc_set_ssid(MQTT_Client *client, char *ssid) {
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L];
	int mqtt_message_l;
	uint16_t calculated_crc;
	uint16_t saved_crc;
		
	// change sta_ssid, save if different
	if (strncmp(sys_cfg.sta_ssid, ssid, 32 - 1) != 0) {
		memset(sys_cfg.sta_ssid, 0, sizeof(sys_cfg.sta_ssid));
		strncpy(sys_cfg.sta_ssid, ssid, 32 - 1);
		if (!cfg_save(&calculated_crc, &saved_crc)) {
			mqtt_flash_error(calculated_crc, saved_crc);
		}
	}

	// send mqtt reply
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/set_ssid/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/set_ssid/v2/%s/%llu", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/set_ssid/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif
	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));
	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%s", sys_cfg.sta_ssid);
	// encrypt and send
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
	MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
}

ICACHE_FLASH_ATTR
void mqtt_rpc_set_pwd(MQTT_Client *client, char *password) {
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L];
	int mqtt_message_l;
	uint16_t calculated_crc;
	uint16_t saved_crc;
		
	// change sta_pwd, save if different
	if (strncmp(sys_cfg.sta_pwd, password, 64 - 1) != 0) {
		memset(sys_cfg.sta_pwd, 0, sizeof(sys_cfg.sta_pwd));
		strncpy(sys_cfg.sta_pwd, password, 64 - 1);
		if (!cfg_save(&calculated_crc, &saved_crc)) {
			mqtt_flash_error(calculated_crc, saved_crc);
		}
	}

	// send mqtt reply
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/set_pwd/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/set_pwd/v2/%s/%llu", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/set_pwd/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif
	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));
	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%s", sys_cfg.sta_pwd);
	// encrypt and send
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
	MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
}

ICACHE_FLASH_ATTR
void mqtt_rpc_set_ssid_pwd(MQTT_Client *client, char *ssid_pwd) {
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L];
	int mqtt_message_l;
	uint16_t calculated_crc;
	uint16_t saved_crc;

#ifdef DEBUG
	printf("param: %s\n", ssid_pwd);
#endif	// DEBUG

	if (!cfg_save_ssid_pwd(ssid_pwd, &calculated_crc, &saved_crc)) {
		mqtt_flash_error(calculated_crc, saved_crc);
	}
	
	// send mqtt reply
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/set_ssid_pwd/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/set_ssid_pwd/v2/%s/%llu", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/set_ssid_pwd/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif
	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));
	
	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "ssid=");
	strncpy(mqtt_message, sys_cfg.sta_ssid, MQTT_MESSAGE_L - 1);
	// escape & and =
	if (query_string_escape(mqtt_message, MQTT_MESSAGE_L) < 0) {
		// error
		return;
	}
	strcat(cleartext, mqtt_message);
	strcat(cleartext, "&pwd=");

	strncpy(mqtt_message, sys_cfg.sta_pwd, MQTT_MESSAGE_L - 1);
	// escape & and =
	if (query_string_escape(mqtt_message, MQTT_MESSAGE_L) < 0) {
		// error
		return;
	}
	strcat(cleartext, mqtt_message);

	// encrypt and send
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
	MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
}

ICACHE_FLASH_ATTR
void mqtt_rpc_set_ap_mesh_pwd(MQTT_Client *client, char *password) {
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L];
	int mqtt_message_l;
	uint16_t calculated_crc;
	uint16_t saved_crc;

	if (fallback_ap_is_running) return; // Prevent overwriting rescue AP config
		
	// change sta_pwd, save if different
	if (strncmp(sys_cfg.ap_mesh_pwd, password, 64 - 1) != 0) {
		memset(sys_cfg.ap_mesh_pwd, 0, sizeof(sys_cfg.ap_mesh_pwd));
		strncpy(sys_cfg.ap_mesh_pwd, password, 64 - 1);
		if (!cfg_save(&calculated_crc, &saved_crc)) {
			mqtt_flash_error(calculated_crc, saved_crc);
		}
	}

	// restart AP
	wifi_set_opmode_current(STATION_MODE);

	wifi_set_opmode_current(STATIONAP_MODE);
	wifi_softap_config((uint8_t*)mesh_ssid, (uint8_t*)sys_cfg.ap_mesh_pwd, AP_MESH_TYPE);
	wifi_softap_ip_config();	

	// send mqtt reply
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/set_ap_mesh_pwd/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/set_ap_mesh_pwd/v2/%s/%llu", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/set_ap_mesh_pwd/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif
	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));
	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%s", sys_cfg.ap_mesh_pwd);
	// encrypt and send
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
	MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
}

ICACHE_FLASH_ATTR
void mqtt_rpc_test_ssid_pwd(MQTT_Client *client, char *params) {
	char ssid[WIFI_TEST_SSID_MAX_LEN] = {0};
	char pwd[WIFI_TEST_PWD_MAX_LEN] = {0};
	char stay_str[WIFI_TEST_STAY_MAX_LEN] = {0};
	uint32_t stay_ms = 0;
	char *str, *key, *val;
	char *ctx1, *ctx2;
	char params_copy[COMMAND_PARAMS_L];
	
	// Variables for the immediate acknowledgement reply
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L];
	int mqtt_message_l;
	
	// Parse input parameters
	strncpy(params_copy, params, COMMAND_PARAMS_L);
	str = strtok_r(params_copy, "&", &ctx1);
	while (str != NULL) {
		key = strtok_r(str, "=", &ctx2);
		val = strtok_r(NULL, "=", &ctx2);
		if (key && val) {
			query_string_unescape(val);
			if (strncmp(key, "ssid", WIFI_TEST_SSID_MAX_LEN) == 0) strncpy(ssid, val, WIFI_TEST_SSID_MAX_LEN - 1);
			if (strncmp(key, "pwd", WIFI_TEST_PWD_MAX_LEN) == 0) strncpy(pwd, val, WIFI_TEST_PWD_MAX_LEN - 1);
			if (strncmp(key, "stay", WIFI_TEST_STAY_MAX_LEN) == 0) strncpy(stay_str, val, WIFI_TEST_STAY_MAX_LEN - 1);
		}
		str = strtok_r(NULL, "&", &ctx1);
	}

	if (strlen(ssid) == 0) return;
	if (strlen(stay_str) > 0) stay_ms = atoi(stay_str) * 1000;

#ifdef DEBUG
	os_printf("MQTT RPC: Triggering Wi-Fi test for SSID: %s\n", ssid);
#endif

	// --- Send immediate acknowledgement reply before testing ---
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/test_ssid_pwd/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/test_ssid_pwd/v2/%s/%llu", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/test_ssid_pwd/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif
	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));
	
	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "ssid=");
	strncpy(mqtt_message, ssid, MQTT_MESSAGE_L - 1);
	// escape & and =
	if (query_string_escape(mqtt_message, MQTT_MESSAGE_L) < 0) {
		// error
		return;
	}
	strcat(cleartext, mqtt_message);
	strcat(cleartext, "&pwd=");

	strncpy(mqtt_message, pwd, MQTT_MESSAGE_L - 1);
	// escape & and =
	if (query_string_escape(mqtt_message, MQTT_MESSAGE_L) < 0) {
		// error
		return;
	}
	strcat(cleartext, mqtt_message);
	
	// append the stay timer if provided
	if (strlen(stay_str) > 0) {
		strcat(cleartext, "&stay=");
		strcat(cleartext, stay_str);
	}
	
	// Encrypt and send MQTT acknowledgement
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
	MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2

	// Simply call wifi_test_ssid_pwd - idempotency and stay timer extensions are handled inside wifi.c
	wifi_test_ssid_pwd(ssid, pwd, stay_ms);
}

ICACHE_FLASH_ATTR
void mqtt_rpc_reconnect(MQTT_Client *client) {
	// reconnect with new password
	MQTT_Disconnect(client);
}

ICACHE_FLASH_ATTR
void mqtt_rpc_disconnect_count(MQTT_Client *client) {
	// send disconnect count
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L];
	int mqtt_message_l;
		
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/disconnect_count/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/disconnect_count/v2/%s/%llu", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/disconnect_count/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif
	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));
	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%u", disconnect_count);
	// encrypt and send
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
	MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
}

ICACHE_FLASH_ATTR
void mqtt_rpc_network_quality(MQTT_Client *client) {
	// send disconnect count
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L];
	int mqtt_message_l;
		
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/network_quality/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/network_quality/v2/%s/%llu", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/network_quality/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif
	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));
	if (ping_average_response_time_ms == 0) {
		// we have not received ping reply yet
		tfp_snprintf(cleartext, MQTT_MESSAGE_L, "ping_response_time=unknown&ping_average_packet_loss=unknown&disconnect_count=%u", disconnect_count);
	}
	else {
		tfp_snprintf(cleartext, MQTT_MESSAGE_L, "ping_response_time=%.3f mS&ping_average_packet_loss=%.2f%%&disconnect_count=%u", ping_average_response_time_ms, (100.0 * ping_average_packet_loss), disconnect_count);
	}
	// encrypt and send
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
	MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
}

ICACHE_FLASH_ATTR
void mqtt_rpc_wifi_status(MQTT_Client *client) {
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L];
	int mqtt_message_l;
	uint32_t wifi_status;
	const char *reason_str;
		
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/wifi_status/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/wifi_status/v2/%s/%llu", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/wifi_status/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif
	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));
	wifi_status = wifi_get_status();
		reason_str = wifi_get_reason_desc(wifi_status);
		if (strcmp(reason_str, "unknown") == 0 || strcmp(reason_str, "none") == 0) {
			tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%s", "connected");
		} else {
			tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%s", reason_str);
		}
	
	// encrypt and send
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
	MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
}

ICACHE_FLASH_ATTR
void mqtt_rpc_ap_status(MQTT_Client *client) {
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L];
	int mqtt_message_l;
		
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/ap_status/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/ap_status/v2/%s/%llu", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/ap_status/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif
	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));
	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%s", (wifi_get_opmode() != STATION_MODE) ? "started" : "stopped");
	// encrypt and send
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
	MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
}

ICACHE_FLASH_ATTR
void mqtt_rpc_start_ap(MQTT_Client *client, char *mesh_ssid) {
	uint16_t calculated_crc;
	uint16_t saved_crc;

	if (fallback_ap_is_running) return; // Prevent overwriting rescue AP

	// start AP
	if (wifi_get_opmode() != STATIONAP_MODE) {
		wifi_set_opmode_current(STATIONAP_MODE);
		wifi_softap_config((uint8_t*)mesh_ssid, (uint8_t*)sys_cfg.ap_mesh_pwd, AP_MESH_TYPE);
		wifi_softap_ip_config();
	
		// ...and save setting to flash if changed
		if (sys_cfg.ap_enabled == false) {
			sys_cfg.ap_enabled = true;
			if (!cfg_save(&calculated_crc, &saved_crc)) {
				mqtt_flash_error(calculated_crc, saved_crc);
			}
		}
	}
}

ICACHE_FLASH_ATTR
void mqtt_rpc_stop_ap(MQTT_Client *client) {
	uint16_t calculated_crc;
	uint16_t saved_crc;

	if (fallback_ap_is_running) return; // Prevent tearing down rescue AP

	// stop AP
	if (wifi_get_opmode() != STATION_MODE) {
		wifi_set_opmode_current(STATION_MODE);
		// ...and save setting to flash if changed
		if (sys_cfg.ap_enabled == true) {
			sys_cfg.ap_enabled = false;
			if (!cfg_save(&calculated_crc, &saved_crc)) {
				mqtt_flash_error(calculated_crc, saved_crc);
			}
		}
	}
}

// ==============================================================================
// TIMER FOR TEMPORARY FALLBACK AP
// ==============================================================================
static void ICACHE_FLASH_ATTR fallback_ap_timer_func(void *arg) {
	char *mesh_ssid = (char *)arg;
#ifdef DEBUG
	os_printf("MQTT RPC: Fallback AP timer expired. Restoring normal state...\n");
#endif

	fallback_ap_is_running = false;
	
	// Revert to the user's saved AP preference without touching flash memory
	if (sys_cfg.ap_enabled) {
		wifi_set_opmode_current(STATIONAP_MODE);
		wifi_softap_config((uint8_t*)mesh_ssid, (uint8_t*)sys_cfg.ap_mesh_pwd, AP_MESH_TYPE);
		wifi_softap_ip_config();
	} else {
		wifi_set_opmode_current(STATION_MODE);
	}

	led_stop_pattern();

	// Resume background scanning now that the rescue AP is closed
	wifi_start_scan(WIFI_SCAN_INTERVAL_LONG);
}

ICACHE_FLASH_ATTR
void mqtt_rpc_start_fallback_ap(MQTT_Client *client, char *params, char *mesh_ssid) {
	uint32_t time_ms = 0;
	
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L];
	int mqtt_message_l;

	if (params != NULL && strlen(params) > 0) time_ms = atoi(params) * 1000;

	if (time_ms == 0) return; // Abort if no valid time was provided

#ifdef DEBUG
	os_printf("MQTT RPC: Starting temporary Fallback AP for %u seconds\n", time_ms / 1000);
#endif

	// Immediate acknowledgement reply
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/start_fallback_ap/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/start_fallback_ap/v2/%s/%llu", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/start_fallback_ap/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif
	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));
	
	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%s", params);
	
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
	MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);

	// If we are already broadcasting the fallback AP, just extend the timer!
	// Skipping the wifi_softap_config prevents dropping already-connected meters.
	if (fallback_ap_is_running) {
		os_timer_disarm(&fallback_ap_timer);
		os_timer_arm(&fallback_ap_timer, time_ms, 0);
		return;
	}

	fallback_ap_is_running = true;

	// Start the Rescue AP
	wifi_set_opmode_current(STATIONAP_MODE);
	wifi_softap_config((uint8_t*)STA_FALLBACK_SSID, (uint8_t*)STA_FALLBACK_PASS, AP_MESH_TYPE);
	wifi_softap_ip_config();

	// Stop the background scanner! Sweeping channels while hosting 
	// active NAT clients causes radio instability and Fatal Exception 9.
	wifi_stop_scan();

	// Arm teardown timer
	os_timer_disarm(&fallback_ap_timer);
	os_timer_setfn(&fallback_ap_timer, (os_timer_func_t *)fallback_ap_timer_func, mesh_ssid);
	os_timer_arm(&fallback_ap_timer, time_ms, 0);

	// Start the special requested LED pattern!
	led_stop_pattern();
	led_pattern_d();
}

ICACHE_FLASH_ATTR
void mqtt_rpc_fallback_status(MQTT_Client *client) {
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L];
	int mqtt_message_l;
	struct station_config stationConf;
		
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/fallback_status/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/fallback_status/v2/%s/%llu", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/fallback_status/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif
	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));

	// Check if the currently active Wi-Fi configuration is the fallback network
	wifi_station_get_config(&stationConf);
	if (strncmp((char*)stationConf.ssid, STA_FALLBACK_SSID, sizeof(stationConf.ssid)) == 0) {
		tfp_snprintf(cleartext, MQTT_MESSAGE_L, "active");
	} else {
		tfp_snprintf(cleartext, MQTT_MESSAGE_L, "inactive");
	}

	// encrypt and send
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
	MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
}

ICACHE_FLASH_ATTR
void mqtt_rpc_mem(MQTT_Client *client) {
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L];
	int mqtt_message_l;
		
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/mem/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/mem/v2/%s/%llu", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/mem/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif
	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));

	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "heap=%u", system_get_free_heap_size());
	// encrypt and send
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
#ifdef DEBUG
	system_print_meminfo();
#endif
	MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
}

ICACHE_FLASH_ATTR
void mqtt_rpc_chip_id(MQTT_Client *client) {
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L];
	int mqtt_message_l;

	// Build MQTT topic with timestamp
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/chip_id/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/chip_id/v2/%s/%llu", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/chip_id/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif
	// Clear buffers
	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));

	// Store chip ID in cleartext
	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "0x%06x", system_get_chip_id());

	// Encrypt and get message length
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);

#ifdef DEBUG
	os_printf("MQTT chip_id: %s\n", cleartext);
#endif

	// Publish to MQTT broker with QoS 2
	MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);
}

ICACHE_FLASH_ATTR
void mqtt_rpc_flash_id(MQTT_Client *client) {
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L];
	int mqtt_message_l;
		
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/flash_id/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/flash_id/v2/%s/%llu", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/flash_id/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif
	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));

	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "0x%x", spi_flash_get_id());
	// encrypt and send
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
#ifdef DEBUG
	system_print_meminfo();
#endif
	MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
}

ICACHE_FLASH_ATTR
void mqtt_rpc_flash_size(MQTT_Client *client) {
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L];
	int mqtt_message_l;
		
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/flash_size/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/flash_size/v2/%s/%llu", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/flash_size/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif
	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));

	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%u kB", spi_flash_size() / 1024);
	// encrypt and send
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
#ifdef DEBUG
	system_print_meminfo();
#endif
	MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
}

ICACHE_FLASH_ATTR
void mqtt_rpc_crypto(MQTT_Client *client) {
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L];
	int mqtt_message_l;
		
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/crypto/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/crypto/v2/%s/%llu", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/crypto/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif
	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));

	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%s-%s-%s-%s", system_get_sdk_version(), VERSION, LWIP_VERSION, HW_MODEL);

	// encrypt and send
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
	MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
}

ICACHE_FLASH_ATTR
void mqtt_rpc_reset_reason(MQTT_Client *client) {
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L];
	int mqtt_message_l;
		
	uint32_t watchdog_rebooted;
	struct rst_info *rtc_info;
	
	rtc_info = system_get_rst_info();

#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/reset_reason/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/reset_reason/v2/%s/%llu", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/reset_reason/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif
	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));
	if (rtc_info != NULL) {
		if (load_rtc_data(&watchdog_rebooted) && watchdog_rebooted) {
			tfp_snprintf(cleartext, MQTT_MESSAGE_L, "reason=%d&exccause=%d&epc1=0x%08x&epc2=0x%08x&epc3=0x%08x&excvaddr=0x%08x&depc=0x%08x", 7, rtc_info->exccause, rtc_info->epc1, rtc_info->epc2, rtc_info->epc3, rtc_info->excvaddr, rtc_info->depc);
		}
		else {
			tfp_snprintf(cleartext, MQTT_MESSAGE_L, "reason=%d&exccause=%d&epc1=0x%08x&epc2=0x%08x&epc3=0x%08x&excvaddr=0x%08x&depc=0x%08x", rtc_info->reason, rtc_info->exccause, rtc_info->epc1, rtc_info->epc2, rtc_info->epc3, rtc_info->excvaddr, rtc_info->depc);
		}
	}
	else {
		if (load_rtc_data(&watchdog_rebooted) && watchdog_rebooted) {
			tfp_snprintf(cleartext, MQTT_MESSAGE_L, "reason=%d", 7);
		}
		else {
			tfp_snprintf(cleartext, MQTT_MESSAGE_L, "reason=%d", -1);
		}
	}
	// encrypt and send
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
	MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
}

ICACHE_FLASH_ATTR
void mqtt_rpc_restart(MQTT_Client *client) {
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L];
	int mqtt_message_l;
		
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/restart/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/restart/v2/%s/%llu", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/restart/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif
	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));
	// Empty payload

	// encrypt and send
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
	MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2

	// Delay the actual teardown and restart by 2 seconds to let the QoS 2 ACK transmit
	os_timer_disarm(&mqtt_restart_ack_timer);
	os_timer_setfn(&mqtt_restart_ack_timer, (os_timer_func_t *)mqtt_restart_ack_timer_func, client);
	os_timer_arm(&mqtt_restart_ack_timer, 2000, 0);
}

ICACHE_FLASH_ATTR
void mqtt_rpc_ota_upgrade(MQTT_Client *client, char *params) {
	char base_url[128] = {0};
	char new_key[64] = {0};
	char *str, *param_key, *param_val, *ctx1, *ctx2;
	char params_copy[MQTT_MESSAGE_L];

	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L];
	int mqtt_message_l;
	
	uint8_t target_rom = 0;
	bool success = false;

	// 1. Check if an OTA upgrade is already running
	if (ota_in_progress) {
#ifdef DEBUG
		os_printf("OTA: Upgrade already in progress. Ignoring request.\r\n");
#endif
#ifdef EN61107
		tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/ota_upgrade/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
		tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/ota_upgrade/v2/%s/%llu", sys_cfg.impulse_meter_serial, get_unix_time());
#else
		tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/ota_upgrade/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif
		memset(mqtt_message, 0, sizeof(mqtt_message));
		memset(cleartext, 0, sizeof(cleartext));
		tfp_snprintf(cleartext, MQTT_MESSAGE_L, "status=error_busy");

		mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
		MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0); // QoS 2
		return;
	}

	// Lock the OTA function until reboot
	ota_in_progress = true;

	// 2. Parse input parameters (e.g., "url=http://api.domain.com/user2.bin&key=ef500c9268cf749016d26d6cbfaaf7bf")
	strncpy(params_copy, params, MQTT_MESSAGE_L);
	str = strtok_r(params_copy, "&", &ctx1);
	while (str != NULL) {
		param_key = strtok_r(str, "=", &ctx2);
		param_val = strtok_r(NULL, "=", &ctx2);
		if (param_key && param_val) {
			if (strncmp(param_key, "url", 3) == 0) {
				query_string_unescape(param_val);
				strncpy(base_url, param_val, sizeof(base_url) - 1);
			} else if (strncmp(param_key, "key", 3) == 0) {
				query_string_unescape(param_val);
				strncpy(new_key, param_val, sizeof(new_key) - 1);
			}
		}
		str = strtok_r(NULL, "&", &ctx1);
	}

	// Fallback: If it doesn't contain "url=", assume the whole string is the URL
	if (strlen(base_url) == 0 && strncmp(params, "http", 4) == 0) {
		strncpy(base_url, params, sizeof(base_url) - 1);
	}

	// 3. Save the new master key to flash before updating
	if (strlen(new_key) >= 32) {
		if (cfg_save_key(new_key)) {
#ifdef DEBUG
			os_printf("OTA: Provisioned new master key to flash.\r\n");
#endif
		}
	} else if (strlen(new_key) > 0) {
#ifdef DEBUG
		os_printf("OTA: Provided key is too short (%d). Skipping key save.\r\n", strlen(new_key));
#endif
	}

	// 4. Start the OTA download
	if (strlen(base_url) > 0) {
		success = start_ota_upgrade(base_url, &target_rom);
	}

	// 5. Send Encrypted MQTT Acknowledgment
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/ota_upgrade/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/ota_upgrade/v2/%s/%llu", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/ota_upgrade/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif

	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));
	
	if (success) {
		tfp_snprintf(cleartext, MQTT_MESSAGE_L, "status=started&target_rom=%d", target_rom + 1);
	} else {
		tfp_snprintf(cleartext, MQTT_MESSAGE_L, "status=error_no_url");
		ota_in_progress = false;
	}

	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
	MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0); // QoS 2
}

ICACHE_FLASH_ATTR
void mqtt_rpc_set_key(MQTT_Client *client, char *params) {
	char new_key[64] = {0};
	char *str, *param_key, *param_val, *ctx1, *ctx2;
	char params_copy[MQTT_MESSAGE_L];

	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L];
	int mqtt_message_l;
	bool success = false;

	// Parse input parameters (e.g., "key=ef500c9268cf749016d26d6cbfaaf7bf")
	strncpy(params_copy, params, MQTT_MESSAGE_L);
	str = strtok_r(params_copy, "&", &ctx1);
	while (str != NULL) {
		param_key = strtok_r(str, "=", &ctx2);
		param_val = strtok_r(NULL, "=", &ctx2);
		if (param_key && param_val && strncmp(param_key, "key", 3) == 0) {
			query_string_unescape(param_val);
			strncpy(new_key, param_val, sizeof(new_key) - 1);
		}
		str = strtok_r(NULL, "&", &ctx1);
	}

	// Fallback: If it doesn't contain "key=", assume the whole string is the key
	if (strlen(new_key) == 0 && strlen(params) >= 32) {
		strncpy(new_key, params, sizeof(new_key) - 1);
	}

	// Save to flash
	if (strlen(new_key) >= 32) {
		success = cfg_save_key(new_key);
	}

#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/set_key/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/set_key/v2/%s/%llu", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/set_key/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif

	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));
	
	if (success) {
		tfp_snprintf(cleartext, MQTT_MESSAGE_L, "status=ok");
	} else {
		tfp_snprintf(cleartext, MQTT_MESSAGE_L, "status=error_invalid_key");
	}

	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
	MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0); // QoS 2
}

#ifdef DEBUG_STACK_TRACE
ICACHE_FLASH_ATTR
void mqtt_rpc_stack_trace(MQTT_Client *client) {
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L];
	int mqtt_message_l;
		
	exception_handler_init();
	
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/stack_trace/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/stack_trace/v2/%s/%llu", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/stack_trace/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif
	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));
	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "enabled");

	// encrypt and send
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
	MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
}
#endif	// DEBUG_STACK_TRACE

#ifndef IMPULSE
#ifndef NO_CRON
ICACHE_FLASH_ATTR
void mqtt_rpc_set_cron(MQTT_Client *client, char *query) {
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L];
	int mqtt_message_l;

	add_cron_job_from_query(query);

#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/set_cron/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/set_cron/v2/%s/%llu", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/set_cron/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif
	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));
	strncpy(cleartext, query, MQTT_MESSAGE_L);
	// encrypt and send
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
	MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
}

ICACHE_FLASH_ATTR
void mqtt_rpc_clear_cron(MQTT_Client *client) {
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L];
	int mqtt_message_l;

	clear_cron_jobs();

#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/clear_cron/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/clear_cron/v2/%s/%llu", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/clear_cron/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif
	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));	// empty reply
	// encrypt and send
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
	MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
}

ICACHE_FLASH_ATTR
void mqtt_rpc_cron(MQTT_Client *client) {
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L];
	int mqtt_message_l;
	
#ifdef DEBUG
	debug_cron_jobs();
#endif
		
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/cron/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/cron/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif
	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));
	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%u", sys_cfg.cron_jobs.n);
	// encrypt and send
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
	MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
}
#endif	// NO_CRON

ICACHE_FLASH_ATTR
void mqtt_rpc_open(MQTT_Client *client) {
	//ac_motor_valve_open();
	ac_thermo_open();
}

ICACHE_FLASH_ATTR
void mqtt_rpc_open_until(MQTT_Client *client, char *value) {
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L];
	int mqtt_message_l;
	int int_value;
	uint16_t calculated_crc;
	uint16_t saved_crc;
#ifdef FLOW_METER
	// use liters internally for FLOW_METER
	char volume_string[32];
	
	multiply_str_by_1000(value, volume_string);
	int_value = atoi(volume_string);
#else
	int_value = atoi(value);
#endif	// FLOW_METER
		
	if (atoi(value) >= 0) {	// only open valve if not negative value
		ac_thermo_open();
		if (sys_cfg.offline_close_at != int_value) {	// only write to flash if changed
			// save if changed
#ifdef EN61107
			sys_cfg.offline_close_at = int_value;
#else
			sys_cfg.offline_close_at = int_value;
#endif
			if (!cfg_save(&calculated_crc, &saved_crc)) {
				mqtt_flash_error(calculated_crc, saved_crc);
			}
		}
	}
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/open_until/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/open_until/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif
	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));
#ifdef FLOW_METER
	// use liters internally for FLOW_METER
	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%.3f", (float)sys_cfg.offline_close_at / 1000.0);
#else
	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%u", sys_cfg.offline_close_at);
#endif	// FLOW_METER
	// encrypt and send
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
	MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2

	// send status
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/status/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/status/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif
	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));
	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%s", sys_cfg.ac_thermo_state ? "open" : "close");
	// encrypt and send
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
	MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
}

ICACHE_FLASH_ATTR
void mqtt_rpc_open_until_delta(MQTT_Client *client, char *value) {
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L];
	int mqtt_message_l;
	int int_value;
	uint16_t calculated_crc;
	uint16_t saved_crc;
		
#ifdef FLOW_METER
	// use liters internally for FLOW_METER
	char volume_string[32];
	
	multiply_str_by_1000(value, volume_string);
	int_value = atoi(volume_string);
#else
	int_value = atoi(value);
#endif	// FLOW_METER
	if (atoi(value) >= 0) {	// only open valve if not negative value
		ac_thermo_open();
		if (sys_cfg.offline_close_at != int_value) {	// only write to flash if changed
			// save if changed
#ifdef EN61107
#ifdef FLOW_METER
			sys_cfg.offline_close_at = en61107_get_received_volume_l() + int_value;
#else
			sys_cfg.offline_close_at = en61107_get_received_energy_kwh() + int_value;
#endif	// FLOW_METER
#else
#ifdef FLOW_METER
			sys_cfg.offline_close_at = kmp_get_received_volume_l() + int_value;
#else
			sys_cfg.offline_close_at = kmp_get_received_energy_kwh() + int_value;
#endif	// FLOW_METER
#endif
			if (!cfg_save(&calculated_crc, &saved_crc)) {
				mqtt_flash_error(calculated_crc, saved_crc);
			}
		}
	}
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/open_until_delta/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/open_until_delta/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif
	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));
#ifdef EN61107
#ifdef FLOW_METER
	// use liters internally for FLOW_METER
	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%.3f", (float)(sys_cfg.offline_close_at - en61107_get_received_volume_l()) / 1000.0);
#else
	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%u", sys_cfg.offline_close_at - en61107_get_received_energy_kwh());
#endif	// FLOW_METER
#else
#ifdef FLOW_METER
	// use liters internally for FLOW_METER
	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%.3f", (float)(sys_cfg.offline_close_at - kmp_get_received_volume_l()) / 1000.0);
#else
	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%u", sys_cfg.offline_close_at - kmp_get_received_energy_kwh());
#endif	// FLOW_METER
#endif
	// encrypt and send
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
	MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2

	// send status
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/status/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/status/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif
	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));
	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%s", sys_cfg.ac_thermo_state ? "open" : "close");
	// encrypt and send
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
	MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
}

ICACHE_FLASH_ATTR
void mqtt_rpc_close(MQTT_Client *client) {
	//ac_motor_valve_close();
	ac_thermo_close();
}

ICACHE_FLASH_ATTR
void mqtt_rpc_status(MQTT_Client *client) {
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L];
	int mqtt_message_l;
		
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/status/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/status/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif
	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));
	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%s", sys_cfg.ac_thermo_state ? "open" : "close");
	// encrypt and send
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
	MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
}
#endif
