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
#include "utils.h"
#include "version.h"
#include "user_main.h"

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

ICACHE_FLASH_ATTR
void mqtt_rpc_ping(MQTT_Client *client) {
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L];
	int mqtt_message_l;

#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/ping/v2/%07u/%u", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/ping/v2/%s/%u", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/ping/v2/%07u/%u", kmp_get_received_serial(), get_unix_time());
#endif
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
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/version/v2/%07u/%u", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/version/v2/%s/%u", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/version/v2/%07u/%u", kmp_get_received_serial(), get_unix_time());
#endif
	memset(cleartext, 0, sizeof(cleartext));

	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%s-%s-%s", system_get_sdk_version(), VERSION, HW_MODEL);

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
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/uptime/v2/%07u/%u", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/uptime/v2/%s/%u", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/uptime/v2/%07u/%u", kmp_get_received_serial(), get_unix_time());
#endif
	memset(cleartext, 0, sizeof(cleartext));
	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%u", get_uptime());
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
	
	char decimal_str[8];	// temp var for divide_str_by_ functions

#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/vdd/v2/%07u/%u", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/vdd/v2/%s/%u", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/vdd/v2/%07u/%u", kmp_get_received_serial(), get_unix_time());
#endif
	memset(cleartext, 0, sizeof(cleartext));
	tfp_snprintf(decimal_str, 8, "%u", system_get_vdd33());
	divide_str_by_1000(decimal_str, cleartext);
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
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/rssi/v2/%07u/%u", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/rssi/v2/%s/%u", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/rssi/v2/%07u/%u", kmp_get_received_serial(), get_unix_time());
#endif
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
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/ssid/v2/%07u/%u", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/ssid/v2/%s/%u", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/ssid/v2/%07u/%u", kmp_get_received_serial(), get_unix_time());
#endif
	memset(cleartext, 0, sizeof(cleartext));
	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%s", sys_cfg.sta_ssid);
	// encrypt and send
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
	MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
}

ICACHE_FLASH_ATTR
void mqtt_rpc_scan(MQTT_Client *client) {
	// reguster wifi scan callback to handle scan results when we do normal scanning in wifi.c
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
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/set_ssid/v2/%07u/%u", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/set_ssid/v2/%s/%u", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/set_ssid/v2/%07u/%u", kmp_get_received_serial(), get_unix_time());
#endif
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
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/set_pwd/v2/%07u/%u", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/set_pwd/v2/%s/%u", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/set_pwd/v2/%07u/%u", kmp_get_received_serial(), get_unix_time());
#endif
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
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/set_ssid_pwd/v2/%07u/%u", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/set_ssid_pwd/v2/%s/%u", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/set_ssid_pwd/v2/%07u/%u", kmp_get_received_serial(), get_unix_time());
#endif
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
void mqtt_rpc_reconnect(MQTT_Client *client) {
	// reconnect with new password
	MQTT_Disconnect(client);
}

ICACHE_FLASH_ATTR
void mqtt_rpc_wifi_status(MQTT_Client *client) {
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L];
	int mqtt_message_l;
		
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/wifi_status/v2/%07u/%u", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/wifi_status/v2/%s/%u", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/wifi_status/v2/%07u/%u", kmp_get_received_serial(), get_unix_time());
#endif
	memset(cleartext, 0, sizeof(cleartext));
	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%s", wifi_get_status() ? "connected" : "disconnected");
	// encrypt and send
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
	MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
}

#ifdef AP
ICACHE_FLASH_ATTR
void mqtt_rpc_ap_status(MQTT_Client *client) {
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L];
	int mqtt_message_l;
		
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/ap_status/v2/%07u/%u", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/ap_status/v2/%s/%u", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/ap_status/v2/%07u/%u", kmp_get_received_serial(), get_unix_time());
#endif
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
	// start AP
	if (wifi_get_opmode() != STATIONAP_MODE) {
		wifi_set_opmode_current(STATIONAP_MODE);
		wifi_softap_config(mesh_ssid, AP_MESH_PASS, AP_MESH_TYPE);
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
#endif	// AP

ICACHE_FLASH_ATTR
void mqtt_rpc_mem(MQTT_Client *client) {
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L];
	int mqtt_message_l;
		
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/mem/v2/%07u/%u", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/mem/v2/%s/%u", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/mem/v2/%07u/%u", kmp_get_received_serial(), get_unix_time());
#endif
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
void mqtt_rpc_crypto(MQTT_Client *client) {
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L];
	int mqtt_message_l;
		
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/crypto/v2/%07u/%u", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/crypto/v2/%s/%u", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/crypto/v2/%07u/%u", kmp_get_received_serial(), get_unix_time());
#endif
	memset(cleartext, 0, sizeof(cleartext));

	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%s-%s-%s", system_get_sdk_version(), VERSION, HW_MODEL);

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
		
	struct rst_info *rtc_info;
	rtc_info = system_get_rst_info();

#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/reset_reason/v2/%07u/%u", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/reset_reason/v2/%s/%u", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/reset_reason/v2/%07u/%u", kmp_get_received_serial(), get_unix_time());
#endif
	memset(cleartext, 0, sizeof(cleartext));
	if (rtc_info != NULL) {
		tfp_snprintf(cleartext, MQTT_MESSAGE_L, "reason=%d&exccause=%d&epc1=0x%08x&epc2=0x%08x&epc3=0x%08x&excvaddr=0x%08x&depc=0x%08x", rtc_info->reason, rtc_info->exccause, rtc_info->epc1, rtc_info->epc2, rtc_info->epc3, rtc_info->excvaddr, rtc_info->depc);
	}
	else {
		tfp_snprintf(cleartext, MQTT_MESSAGE_L, "reason=%d", -1);
	}
	// encrypt and send
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
	MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
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
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/stack_trace/v2/%07u/%u", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/stack_trace/v2/%s/%u", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/stack_trace/v2/%07u/%u", kmp_get_received_serial(), get_unix_time());
#endif
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
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/set_cron/v2/%07u/%u", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/set_cron/v2/%s/%u", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/set_cron/v2/%07u/%u", kmp_get_received_serial(), get_unix_time());
#endif
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
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/clear_cron/v2/%07u/%u", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/clear_cron/v2/%s/%u", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/clear_cron/v2/%07u/%u", kmp_get_received_serial(), get_unix_time());
#endif
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
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/cron/v2/%07u/%u", en61107_get_received_serial(), get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/cron/v2/%07u/%u", kmp_get_received_serial(), get_unix_time());
#endif
	memset(cleartext, 0, sizeof(cleartext));
	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%d", sys_cfg.cron_jobs.n);
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
#ifdef FORCED_FLOW_METER
	// use liters internally for FORCED_FLOW_METER
	char volume_string[32];
	char offline_close_at_string[32];
	char offline_close_at_m3_string[32];
	
	multiply_str_by_1000(value, volume_string);
	int_value = atoi(volume_string);
#else
	int_value = atoi(value);
#endif	// FORCED_FLOW_METER
		
	if (int_value >= 0) {	// only open valve if not negative value
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
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/open_until/v2/%07u/%u", en61107_get_received_serial(), get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/open_until/v2/%07u/%u", kmp_get_received_serial(), get_unix_time());
#endif
	memset(cleartext, 0, sizeof(cleartext));
#ifdef FORCED_FLOW_METER
	// use liters internally for FORCED_FLOW_METER
	tfp_snprintf(offline_close_at_string, 32, "%d", sys_cfg.offline_close_at);
	divide_str_by_1000(offline_close_at_string, offline_close_at_m3_string);
	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%s", offline_close_at_m3_string);
#else
	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%d", sys_cfg.offline_close_at);
#endif	// FORCED_FLOW_METER
	// encrypt and send
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
	MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2

	// send status
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/status/v2/%07u/%u", en61107_get_received_serial(), get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/status/v2/%07u/%u", kmp_get_received_serial(), get_unix_time());
#endif
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
		
#ifdef FORCED_FLOW_METER
	// use liters internally for FORCED_FLOW_METER
	char volume_string[32];
	char offline_close_at_string[32];
	char offline_close_at_m3_string[32];
	
	multiply_str_by_1000(value, volume_string);
	int_value = atoi(volume_string);
#else
	int_value = atoi(value);
#endif	// FORCED_FLOW_METER
	if (int_value >= 0) {	// only open valve if not negative value
		ac_thermo_open();
		if (sys_cfg.offline_close_at != int_value) {	// only write to flash if changed
			// save if changed
#ifdef EN61107
#ifdef FORCED_FLOW_METER
			sys_cfg.offline_close_at = en61107_get_received_volume_l() + int_value;
#else
			sys_cfg.offline_close_at = en61107_get_received_energy_kwh() + int_value;
#endif	// FORCED_FLOW_METER
#else
#ifdef FORCED_FLOW_METER
			sys_cfg.offline_close_at = kmp_get_received_volume_l() + int_value;
#else
			sys_cfg.offline_close_at = kmp_get_received_energy_kwh() + int_value;
#endif	// FORCED_FLOW_METER
#endif
			if (!cfg_save(&calculated_crc, &saved_crc)) {
				mqtt_flash_error(calculated_crc, saved_crc);
			}
		}
	}
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/open_until_delta/v2/%07u/%u", en61107_get_received_serial(), get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/open_until_delta/v2/%07u/%u", kmp_get_received_serial(), get_unix_time());
#endif
	memset(cleartext, 0, sizeof(cleartext));
#ifdef EN61107
#ifdef FORCED_FLOW_METER
	// use liters internally for FORCED_FLOW_METER
	tfp_snprintf(offline_close_at_string, 32, "%d", sys_cfg.offline_close_at - en61107_get_received_volume_l());
	divide_str_by_1000(offline_close_at_string, offline_close_at_m3_string);
	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%s", offline_close_at_m3_string);
#else
	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%d", sys_cfg.offline_close_at - en61107_get_received_energy_kwh());
#endif	// FORCED_FLOW_METER
#else
#ifdef FORCED_FLOW_METER
	// use liters internally for FORCED_FLOW_METER
	tfp_snprintf(offline_close_at_string, 32, "%d", sys_cfg.offline_close_at - kmp_get_received_volume_l());
	divide_str_by_1000(offline_close_at_string, offline_close_at_m3_string);
	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%s", offline_close_at_m3_string);
#else
	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%d", sys_cfg.offline_close_at - kmp_get_received_energy_kwh());
#endif	// FORCED_FLOW_METER
#endif
	// encrypt and send
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
	MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2

	// send status
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/status/v2/%07u/%u", en61107_get_received_serial(), get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/status/v2/%07u/%u", kmp_get_received_serial(), get_unix_time());
#endif
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
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/status/v2/%07u/%u", en61107_get_received_serial(), get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/status/v2/%07u/%u", kmp_get_received_serial(), get_unix_time());
#endif
	memset(cleartext, 0, sizeof(cleartext));
	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%s", sys_cfg.ac_thermo_state ? "open" : "close");
	// encrypt and send
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
	MQTT_Publish(client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
}
#endif
