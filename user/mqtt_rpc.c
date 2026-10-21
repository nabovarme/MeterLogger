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
#include "rboot-api.h"

#ifdef EN61107
#include "en61107_request.h"
#else
#include "kmp_request.h"
#endif

#ifdef DEBUG_STACK_TRACE
#include "exception_handler.h"
#endif	// DEBUG_STACK_TRACE

#define MQTT_RESTART_DELAY_MS 2000
#define WIFI_DEFERRED_RECONNECT_MS 500

static os_timer_t fallback_ap_timer;
bool fallback_ap_is_running = false;
extern bool ota_in_progress;

static os_timer_t mqtt_restart_ack_timer;
static os_timer_t delayed_wifi_default_timer;

static void ICACHE_FLASH_ATTR delayed_wifi_default_cb(void *arg) {
	wifi_default();
}

ICACHE_FLASH_ATTR void static mqtt_restart_ack_timer_func(void *arg) {
	MQTT_Client *client = (MQTT_Client *)arg;

	MQTT_DeleteClient(client);
#ifdef EN61107
	en61107_request_destroy();
#else
	kmp_request_destroy();
#endif
	system_restart_defered();
}

ICACHE_FLASH_ATTR
void mqtt_rpc_ping(MQTT_Client *client) {
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L + AES_HMAC_OVERHEAD];
	int mqtt_message_l;

#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/ping/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
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
	char mqtt_message[MQTT_MESSAGE_L + AES_HMAC_OVERHEAD];
	int mqtt_message_l;

#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/version/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
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
	char mqtt_message[MQTT_MESSAGE_L + AES_HMAC_OVERHEAD];
	int mqtt_message_l;

#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/uptime/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
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
	char mqtt_message[MQTT_MESSAGE_L + AES_HMAC_OVERHEAD];
	int mqtt_message_l;
	

#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/vdd/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
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
	char mqtt_message[MQTT_MESSAGE_L + AES_HMAC_OVERHEAD];
	int mqtt_message_l;
		
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/rssi/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
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
	char mqtt_message[MQTT_MESSAGE_L + AES_HMAC_OVERHEAD];
	int mqtt_message_l;
	struct station_config stationConf;
		
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/ssid/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/ssid/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif
	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));
	
	// Fetch the actual active Wi-Fi configuration from the hardware
	wifi_station_get_config(&stationConf);
	
	// If currently on the fallback rescue network, report the saved user configuration.
	// Otherwise, report the active runtime SSID (which supports test_ssid_pwd routines).
	if (strncmp((char*)stationConf.ssid, STA_FALLBACK_SSID, sizeof(stationConf.ssid)) == 0) {
		tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%s", sys_cfg.sta_ssid);
	} else {
		tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%s", stationConf.ssid);
	}
	
	// encrypt and send
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen((char*)cleartext) + 1);
	MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
}

ICACHE_FLASH_ATTR
void mqtt_rpc_scan(MQTT_Client *client) {
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L + AES_HMAC_OVERHEAD];
	int mqtt_message_l;

	if (fallback_ap_is_running) return; // Prevent Fatal Exception 9

	// --- Send immediate acknowledgement reply before scanning ---
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/scan/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
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
	char mqtt_message[MQTT_MESSAGE_L + AES_HMAC_OVERHEAD];
	int mqtt_message_l;
	uint16_t calculated_crc;
	uint16_t saved_crc;

	if (ssid == NULL) {
		return;
	}

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
	char mqtt_message[MQTT_MESSAGE_L + AES_HMAC_OVERHEAD];
	int mqtt_message_l;
	uint16_t calculated_crc;
	uint16_t saved_crc;

	if (password == NULL) {
		return;
	}

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
	char mqtt_message[MQTT_MESSAGE_L + AES_HMAC_OVERHEAD];
	int mqtt_message_l;
	uint16_t calculated_crc;
	uint16_t saved_crc;

	if (ssid_pwd == NULL) {
		return;
	}

#ifdef DEBUG
	printf("param: %s\n", ssid_pwd);
#endif	// DEBUG

	if (!cfg_save_ssid_pwd(ssid_pwd, &calculated_crc, &saved_crc)) {
		mqtt_flash_error(calculated_crc, saved_crc);
	} else {
		// Defer wifi_default() by 500 ms so the MQTT QoS 2 PUBCOMP ACK can be cleanly transmitted
		os_timer_disarm(&delayed_wifi_default_timer);
		os_timer_setfn(&delayed_wifi_default_timer, (os_timer_func_t *)delayed_wifi_default_cb, NULL);
		os_timer_arm(&delayed_wifi_default_timer, WIFI_DEFERRED_RECONNECT_MS, 0);
	}
	
	// send mqtt reply
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/set_ssid_pwd/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
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
void mqtt_rpc_set_ap_mesh_pwd(MQTT_Client *client, char *ap_mesh_pwd) {
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L + AES_HMAC_OVERHEAD];
	int mqtt_message_l;
	uint16_t calculated_crc;
	uint16_t saved_crc;

	if (ap_mesh_pwd == NULL) {
		return;
	}

#ifdef DEBUG
	printf("ap_mesh_pwd: %s\n", ap_mesh_pwd);
#endif	// DEBUG

	if (strncmp((char *)sys_cfg.ap_mesh_pwd, ap_mesh_pwd, 64 - 1) != 0) {
		memset(sys_cfg.ap_mesh_pwd, 0, sizeof(sys_cfg.ap_mesh_pwd));
		strncpy((char *)sys_cfg.ap_mesh_pwd, ap_mesh_pwd, 64 - 1);

		if (!cfg_save(&calculated_crc, &saved_crc)) {
			mqtt_flash_error(calculated_crc, saved_crc);
		} else {
			// If SoftAP is running, re-enable it to apply the new password immediately
			if (wifi_get_opmode() == STATIONAP_MODE || wifi_get_opmode() == SOFTAP_MODE) {
				wifi_set_opmode_current(STATION_MODE);
				wifi_set_opmode_current(STATIONAP_MODE);
				wifi_softap_config((uint8_t *)mesh_ssid, (uint8_t *)sys_cfg.ap_mesh_pwd, AP_MESH_TYPE);
				wifi_softap_ip_config();
			}
		}
	}

	// send mqtt reply
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/set_ap_mesh_pwd/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/set_ap_mesh_pwd/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif
	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));

	strncpy(cleartext, sys_cfg.ap_mesh_pwd, MQTT_MESSAGE_L - 1);

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
	char mqtt_message[MQTT_MESSAGE_L + AES_HMAC_OVERHEAD];
	int mqtt_message_l;

	if (params == NULL) {
		return;
	}
	
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
	char mqtt_message[MQTT_MESSAGE_L + AES_HMAC_OVERHEAD];
	int mqtt_message_l;
		
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/disconnect_count/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
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
	char mqtt_message[MQTT_MESSAGE_L + AES_HMAC_OVERHEAD];
	int mqtt_message_l;
		
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/network_quality/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
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
	char mqtt_message[MQTT_MESSAGE_L + AES_HMAC_OVERHEAD];
	int mqtt_message_l;
	uint32_t wifi_status;
	const char *reason_str;
		
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/wifi_status/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
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
	char mqtt_message[MQTT_MESSAGE_L + AES_HMAC_OVERHEAD];
	int mqtt_message_l;
		
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/ap_status/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
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

	if (mesh_ssid == NULL) {
		return;
	}

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
	
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L + AES_HMAC_OVERHEAD];
	int mqtt_message_l;
	uint8_t cleartext[MQTT_MESSAGE_L];

	if (mesh_ssid == NULL) {
		return;
	}

	if (params != NULL && strlen(params) > 0) {
		time_ms = atoi(params) * 1000;
	}

#ifdef DEBUG
	os_printf("MQTT RPC: Fallback AP requested with %u seconds\n", time_ms / 1000);
#endif

	// Immediate acknowledgement reply
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/start_fallback_ap/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/start_fallback_ap/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif
	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));
	
	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%s", params != NULL ? params : "0");
	
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
	MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);

	// If we are already broadcasting the fallback AP, just extend the timer!
	// Skipping the wifi_softap_config prevents dropping already-connected meters.
	// If time is 0, disable immediately
	if (time_ms == 0) {
		if (fallback_ap_is_running) {
			os_timer_disarm(&fallback_ap_timer);
			fallback_ap_timer_func(mesh_ssid);
		}
		return;
	}

	// If already running, just extend the timer without resetting softap
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
	char mqtt_message[MQTT_MESSAGE_L + AES_HMAC_OVERHEAD];
	int mqtt_message_l;
	struct station_config stationConf;
		
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/fallback_status/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
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
	char mqtt_message[MQTT_MESSAGE_L + AES_HMAC_OVERHEAD];
	int mqtt_message_l;
		
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/mem/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
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
	char mqtt_message[MQTT_MESSAGE_L + AES_HMAC_OVERHEAD];
	int mqtt_message_l;

	// Build MQTT topic with timestamp
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/chip_id/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
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
	char mqtt_message[MQTT_MESSAGE_L + AES_HMAC_OVERHEAD];
	int mqtt_message_l;
		
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/flash_id/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
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
	char mqtt_message[MQTT_MESSAGE_L + AES_HMAC_OVERHEAD];
	int mqtt_message_l;
		
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/flash_size/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
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
void mqtt_rpc_reset_reason(MQTT_Client *client) {
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L + AES_HMAC_OVERHEAD];
	int mqtt_message_l;
		
	uint32_t watchdog_rebooted;
	struct rst_info *rtc_info;
	
	rtc_info = system_get_rst_info();

#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/reset_reason/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
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
	char mqtt_message[MQTT_MESSAGE_L + AES_HMAC_OVERHEAD];
	int mqtt_message_l;
		
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/restart/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
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
	os_timer_arm(&mqtt_restart_ack_timer, MQTT_RESTART_DELAY_MS, 0);
}

ICACHE_FLASH_ATTR
void mqtt_rpc_ota_update(MQTT_Client *client, char *params) {
	char base_url[128] = {0};
	char version_str[32] = {0};
	char final_url[256] = {0};
	char serial_str[32] = {0};
	char sha256_user1[65] = {0};
	char sha256_user2[65] = {0};
	char params_copy[MQTT_MESSAGE_L];

	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L + AES_HMAC_OVERHEAD];
	int mqtt_message_l;
	
	char *str, *param_key, *param_val, *ctx1, *ctx2, *separator;
	uint8_t target_rom = 0;
	bool success = false;

	rboot_config rconf;
	uint8_t current_rom;
	uint8_t boot_mode = MODE_STANDARD;

	if (params == NULL) {
		return;
	}

	// Fetch actual running boot mode and running ROM slot from RTC memory
	current_rom = rboot_get_current_rom();
#ifdef BOOT_RTC_ENABLED
	rboot_get_last_boot_mode(&boot_mode);
	rboot_get_last_boot_rom(&current_rom);
#endif

	// 0. Handle confirmation command for pending temp boot self-test
	if (strstr(params, "action=confirm") != NULL) {
#ifdef EN61107
		tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/ota_update/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#else
		tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/ota_update/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif
		memset(mqtt_message, 0, sizeof(mqtt_message));
		memset(cleartext, 0, sizeof(cleartext));

		// Check if we booted via MODE_TEMP_ROM or if rconf is out of sync
		rconf = rboot_get_config();
		if (boot_mode == MODE_TEMP_ROM || rconf.current_rom != current_rom) {
#ifdef DEBUG
			os_printf("OTA Confirmation: Self-test passed! Committing ROM %d permanently.\n", current_rom);
#endif
			// 1. Commit permanent ROM slot to SPI flash
			rboot_set_current_rom(current_rom);

#ifdef BOOT_RTC_ENABLED
			// 2. Clear MODE_TEMP_ROM from RTC memory so subsequent OTAs are not blocked
			rboot_rtc_data rtc;
			if (rboot_get_rtc_data(&rtc)) {
				rtc.last_mode = MODE_STANDARD;
				rtc.next_mode = MODE_STANDARD;
				rtc.last_rom = current_rom;
				rboot_set_rtc_data(&rtc);
			}
#endif

			tfp_snprintf(cleartext, MQTT_MESSAGE_L, "status=self_test_passed_committed");
		} else {
			tfp_snprintf(cleartext, MQTT_MESSAGE_L, "status=already_committed");
		}

		mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
		MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0); // QoS 2
		return;
	}

	// 1. SAFETY GUARD: Reject new update if device is in unconfirmed TEMP BOOT mode
	if (boot_mode == MODE_TEMP_ROM) {
#ifdef DEBUG
		os_printf("OTA Error: Cannot re-flash while running in unconfirmed temp boot mode!\n");
#endif
#ifdef EN61107
		tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/ota_update/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#else
		tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/ota_update/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif
		memset(mqtt_message, 0, sizeof(mqtt_message));
		memset(cleartext, 0, sizeof(cleartext));
		tfp_snprintf(cleartext, MQTT_MESSAGE_L, "status=error_unconfirmed_temp_rom");

		mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
		MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0); // QoS 2
		return;
	}

	// 2. Check if an OTA update is already running
	if (ota_in_progress) {
#ifdef DEBUG
		os_printf("OTA: Update already in progress. Ignoring request.\r\n");
#endif
#ifdef EN61107
		tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/ota_update/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#else
		tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/ota_update/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
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

	// Invert current ROM to target the opposite slot (0 -> 1, 1 -> 0)
	target_rom = (current_rom == 0) ? 1 : 0;

	// 3. Parse key-value parameters (url=... & version=... & sha1=... & sha2=...)
	strncpy(params_copy, params, sizeof(params_copy) - 1);
	params_copy[sizeof(params_copy) - 1] = '\0';

	str = strtok_r(params_copy, "&", &ctx1);
	while (str != NULL) {
		param_key = strtok_r(str, "=", &ctx2);
		param_val = strtok_r(NULL, "=", &ctx2);
		if (param_key && param_val) {
			if (strcmp(param_key, "url") == 0) {
				query_string_unescape(param_val);
				strncpy(base_url, param_val, sizeof(base_url) - 1);
			} else if (strcmp(param_key, "version") == 0) {
				query_string_unescape(param_val);
				strncpy(version_str, param_val, sizeof(version_str) - 1);
			} else if (strcmp(param_key, "sha1") == 0 || strcmp(param_key, "sha256_user1") == 0) {
				query_string_unescape(param_val);
				strncpy(sha256_user1, param_val, sizeof(sha256_user1) - 1);
			} else if (strcmp(param_key, "sha2") == 0 || strcmp(param_key, "sha256_user2") == 0) {
				query_string_unescape(param_val);
				strncpy(sha256_user2, param_val, sizeof(sha256_user2) - 1);
			}
		}
		str = strtok_r(NULL, "&", &ctx1);
	}

	// Default to standard endpoint if url parameter was omitted
	if (strlen(base_url) == 0) {
		strncpy(base_url, "http://meterlogger.net/api/ota_firmware", sizeof(base_url) - 1);
	}

	// Retrieve serial number for request
#ifdef EN61107
	tfp_snprintf(serial_str, sizeof(serial_str), "%07u", en61107_get_received_serial());
#else
	tfp_snprintf(serial_str, sizeof(serial_str), "%07u", kmp_get_received_serial());
#endif

	// Safely append query parameters (? vs &)
	separator = (strchr(base_url, '?') == NULL) ? "?" : "&";

	if (strlen(version_str) > 0) {
		tfp_snprintf(final_url, sizeof(final_url), "%s%sserial=%s&slot=%d&version=%s", base_url, separator, serial_str, target_rom, version_str);
	} else {
		tfp_snprintf(final_url, sizeof(final_url), "%s%sserial=%s&slot=%d", base_url, separator, serial_str, target_rom);
	}

#ifdef DEBUG
	os_printf("[OTA] Current ROM: %d | Target ROM: %d\n", current_rom, target_rom);
	os_printf("[OTA] Constructed Request URL: %s\n", final_url);
#endif

	// 4. Start the OTA download with the constructed URL, passing BOTH hashes for validation
	if (strlen(final_url) > 0) {
		success = start_ota_update(client, final_url, sha256_user1[0] ? sha256_user1 : NULL, sha256_user2[0] ? sha256_user2 : NULL, &target_rom);
	}

	// 5. Send Encrypted MQTT Acknowledgment
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/ota_update/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/ota_update/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
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
void mqtt_rpc_ota_status(MQTT_Client *client, const char *status) {
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L + AES_HMAC_OVERHEAD];
	int mqtt_message_l;

	if (status == NULL) {
		return;
	}

#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/ota_update/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/ota_update/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif

	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));
	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "status=%s", status);

	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
	MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0); // QoS 2
}

ICACHE_FLASH_ATTR
void mqtt_rpc_set_key(MQTT_Client *client, char *params) {
	char new_key[64] = {0};

	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L + AES_HMAC_OVERHEAD];
	int mqtt_message_l;
	bool success = false;

	if (params == NULL) {
		return;
	}

	// The whole string is the key
	strncpy(new_key, params, sizeof(new_key) - 1);

	// Save to flash
	if (strlen(new_key) >= 32) {
		success = cfg_save_key(new_key);
	}

#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/set_key/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
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

	// Update the keys in RAM immediately after sending the ACK
	if (success) {
		init_aes_hmac_combined(sys_cfg.key);
	}
}

ICACHE_FLASH_ATTR
void mqtt_rpc_set_mqtt_host(MQTT_Client *client, char *params) {
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L + AES_HMAC_OVERHEAD];
	int mqtt_message_l;
	size_t host_len;
	bool success = false;

	if (params != NULL) {
		host_len = strlen(params);
		if (host_len > 0 && host_len < sizeof(sys_cfg.mqtt_host)) {
			memset(sys_cfg.mqtt_host, 0, sizeof(sys_cfg.mqtt_host));
			memcpy(sys_cfg.mqtt_host, params, host_len);
			success = cfg_save(NULL, NULL);
		}
	}

#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/set_mqtt_host/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/set_mqtt_host/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif

	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));

	if (success) {
		tfp_snprintf(cleartext, MQTT_MESSAGE_L, "status=ok");
	} else {
		tfp_snprintf(cleartext, MQTT_MESSAGE_L, "status=error_invalid_host");
	}

	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
	MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0); // QoS 2

	if (success) {
		// Delay the teardown and restart to let the QoS 2 ACK transmit
		os_timer_disarm(&mqtt_restart_ack_timer);
		os_timer_setfn(&mqtt_restart_ack_timer, (os_timer_func_t *)mqtt_restart_ack_timer_func, client);
		os_timer_arm(&mqtt_restart_ack_timer, MQTT_RESTART_DELAY_MS, 0);
	}
}

#ifdef DEBUG_STACK_TRACE
ICACHE_FLASH_ATTR
void mqtt_rpc_stack_trace(MQTT_Client *client) {
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L + AES_HMAC_OVERHEAD];
	int mqtt_message_l;
		
	exception_handler_init();
	
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/stack_trace/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
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

#ifdef DEBUG_STACK_TRACE
ICACHE_FLASH_ATTR
void mqtt_rpc_read_stack_trace(MQTT_Client *client, char *params) {
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L + AES_HMAC_OVERHEAD];
	int mqtt_message_l;
	
	uint32_t offset = 0;
	char *str, *key, *val, *ctx1, *ctx2;
	char params_copy[128]; 
	
	uint32_t read_len;
	uint32_t flash_addr;
	uint32_t flash_buf[(128 / 4) + 1]; /* 32 words + safety */
	uint8_t *raw_flash;
	int i;
	char hex_buf[3];

	/* 1. Parse the requested offset */
	if (params != NULL && strlen(params) > 0) {
		strncpy(params_copy, params, sizeof(params_copy) - 1);
		params_copy[sizeof(params_copy) - 1] = '\0';
		str = strtok_r(params_copy, "&", &ctx1);
		while (str != NULL) {
			key = strtok_r(str, "=", &ctx2);
			val = strtok_r(NULL, "=", &ctx2);
			if (key && val && strncmp(key, "offset", 6) == 0) {
				offset = atoi(val);
			}
			str = strtok_r(NULL, "&", &ctx1);
		}
	}

	/* Boundary wrap: modulo STACK_TRACE_N */
	offset %= STACK_TRACE_N;

	/* 2. Read exactly 128 bytes from SPI flash (Buffer must be 32-bit aligned) */
	read_len = 128;
	if (offset + read_len > STACK_TRACE_N) {
		read_len = STACK_TRACE_N - offset;
	}

	flash_addr = (STACK_TRACE_SEC * SPI_FLASH_SEC_SIZE) + offset;
	memset(flash_buf, 0, sizeof(flash_buf));
	
	spi_flash_read(flash_addr, flash_buf, read_len);

	raw_flash = (uint8_t *)flash_buf;

	/* 3. Build MQTT Response Topic */
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/stack_trace_result/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/stack_trace_result/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif

	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));
	
	/* Format base return string */
	tfp_snprintf((char *)cleartext, MQTT_MESSAGE_L, "offset=%u&data=", offset);
	
	/* 4. Hex-encode all read_len flash bytes without stopping at 0x00 or 0xFF */
	for (i = 0; i < read_len; i++) {
		tfp_snprintf(hex_buf, sizeof(hex_buf), "%02x", raw_flash[i]);
		strncat((char *)cleartext, hex_buf, MQTT_MESSAGE_L - strlen((char *)cleartext) - 1);
	}

	/* 5. Encrypt and Publish */
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen((char *)cleartext) + 1);
	MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);
}
#endif /* DEBUG_STACK_TRACE */

#ifndef NO_CRON
ICACHE_FLASH_ATTR
void mqtt_rpc_set_cron(MQTT_Client *client, char *query) {
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L + AES_HMAC_OVERHEAD];
	int mqtt_message_l;

	if (query == NULL) {
		return;
	}

	add_cron_job_from_query(query);

#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/set_cron/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
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
	char mqtt_message[MQTT_MESSAGE_L + AES_HMAC_OVERHEAD];
	int mqtt_message_l;

	clear_cron_jobs();

#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/clear_cron/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
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
	char mqtt_message[MQTT_MESSAGE_L + AES_HMAC_OVERHEAD];
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
	char mqtt_message[MQTT_MESSAGE_L + AES_HMAC_OVERHEAD];
	int mqtt_message_l;
	int int_value;
	uint16_t calculated_crc;
	uint16_t saved_crc;
#ifdef FLOW_METER
	char volume_string[32];
#endif

	if (value == NULL) {
		return;
	}

#ifdef FLOW_METER
	// use liters internally for FLOW_METER
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
	char mqtt_message[MQTT_MESSAGE_L + AES_HMAC_OVERHEAD];
	int mqtt_message_l;
	int int_value;
	uint16_t calculated_crc;
	uint16_t saved_crc;
#ifdef FLOW_METER
	char volume_string[32];
#endif
		
	if (value == NULL) {
		return;
	}

#ifdef FLOW_METER
	// use liters internally for FLOW_METER
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
	char mqtt_message[MQTT_MESSAGE_L + AES_HMAC_OVERHEAD];
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

#ifdef DEBUG_SERIAL
ICACHE_FLASH_ATTR
void mqtt_rpc_debug(MQTT_Client *client, const char *error_reason) {
	char topic[MQTT_TOPIC_L];
	char message[128];
	char current_unix_time_string[64];
	char cleartext[128];
	int message_l;

	if (!client || !client->pCon || !client->mqtt_state.work_buffer) {
		return;
	}

	tfp_snprintf(current_unix_time_string, 64, "%llu", get_unix_time());
#ifdef EN61107
	tfp_snprintf(topic, MQTT_TOPIC_L, "/debug/v2/%07u/%s", en61107_get_received_serial(), current_unix_time_string);
#else
	tfp_snprintf(topic, MQTT_TOPIC_L, "/debug/v2/%07u/%s", kmp_get_received_serial(), current_unix_time_string);
#endif

	tfp_snprintf(cleartext, sizeof(cleartext), "error=%s&", error_reason);

	memset(message, 0, sizeof(message));
	message_l = encrypt_aes_hmac_combined((uint8_t *)message, (uint8_t *)topic, strlen(topic), (uint8_t *)cleartext, strlen(cleartext) + 1);

	if (message_l > 1) {
		MQTT_Publish(client, topic, message, message_l, 0, 0); // Sent as QoS 0
	}
}
#endif // DEBUG_SERIAL

