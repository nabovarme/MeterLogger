#include <esp8266.h>
#include "driver/uart.h"
#include "mqtt.h"
#include "mqtt_rpc.h"
#include "crypto/crypto.h"
#include "crypto/aes.h"
#include "crypto/sha256.h"
#include "crypto/hmac-sha256.h"
#include "wifi.h"
#include "config.h"
#include "debug.h"
#include "httpd.h"
#include "httpd_user_init.h"
#include "user_config.h"
#include "unix_time.h"
#include "cron/cron.h"
#include "led.h"
#include "ac/ac_out.h"
#include "utils.h"
#include "user_main.h"
#include "captdns.h"
#include "tinyprintf.h"
#include "watchdog.h"
#include "rtc_mem.h"
#include "icmp_ping.h"
#include "version.h"
#include "ota.h"
#include "rboot-api.h"

#ifdef EN61107
#include "en61107_request.h"
#else
#include "kmp_request.h"
#endif

// Link tracking variables from cnx_csa_fn_patch.c
extern uint32_t cnx_csa_call_count;
extern bool cnx_csa_new_call_flag;

bool fast_boot;

static bool first_mqtt_connect_after_boot = true;

MQTT_Client mqtt_client;
static void ICACHE_FLASH_ATTR mqtt_fallback_retry_cb(void);
static os_timer_t sample_timer_first;
static os_timer_t sample_timer;
static os_timer_t config_mode_timer;
static os_timer_t sample_mode_timer;
static os_timer_t mqtt_connected_first_mqtt_rpc_timer;
static os_timer_t mqtt_connected_defer_timer;
static os_timer_t wifi_test_report_timer;
static os_timer_t csa_report_timer;
#ifdef EN61107
static os_timer_t en61107_request_send_timer;
#else
static os_timer_t kmp_request_send_timer;
#endif
uint8_t mqtt_connected_first_mqtt_rpc_state;	// state variable for mqtt_connected_first_mqtt_rpc_timer_func()
uint8_t mesh_ssid[AP_SSID_LENGTH + 1];

#ifdef MEMLEAK_DEBUG
ICACHE_FLASH_ATTR
bool check_memleak_debug_enable(void) {
	return MEMLEAK_DEBUG_ENABLE;
}
#endif

#if ESP_SDK_VERSION == 030300
#ifdef CONFIG_ENABLE_IRAM_MEMORY
//ICACHE_FLASH_ATTR
uint32 user_iram_memory_is_enabled(void) {
	return  CONFIG_ENABLE_IRAM_MEMORY;
}
#endif	// CONFIG_ENABLE_IRAM_MEMORY
#endif

ICACHE_FLASH_ATTR void static mqtt_connected_first_mqtt_rpc_timer_func(void *arg) {
	unsigned char mqtt_topic[MQTT_TOPIC_L];
	uint8_t current_rom = 0;
	uint8_t boot_mode = MODE_STANDARD;

	switch (mqtt_connected_first_mqtt_rpc_state) {
		case 0:
			// subscribe to /config/v2/[serial]/#
#ifdef EN61107
			tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/config/v2/%07u/#", en61107_get_received_serial());
#else
			tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/config/v2/%07u/#", kmp_get_received_serial());
#endif
			MQTT_Subscribe(&mqtt_client, mqtt_topic, 0);

			// 1. announce unconfirmed temp boot status if in MODE_TEMP_ROM
			current_rom = rboot_get_current_rom();
#ifdef BOOT_RTC_ENABLED
			rboot_get_last_boot_mode(&boot_mode);
			rboot_get_last_boot_rom(&current_rom);
#endif
			if (boot_mode == MODE_TEMP_ROM) {
				mqtt_rpc_ota_status(&mqtt_client, "pending_confirmation");
			}
			mqtt_connected_first_mqtt_rpc_state++;
			os_timer_arm(&mqtt_connected_first_mqtt_rpc_timer, 100, 0);
			break;

		case 1:
			// 2. send mqtt version
			mqtt_rpc_version(&mqtt_client);

			// 3. send mqtt status immediately in the same tick or next
			mqtt_rpc_status(&mqtt_client);
			mqtt_connected_first_mqtt_rpc_state++;
			os_timer_arm(&mqtt_connected_first_mqtt_rpc_timer, 100, 0);
			break;

		case 2:
			// 4. send currently connected ssid
			mqtt_rpc_ssid(&mqtt_client);
			mqtt_connected_first_mqtt_rpc_state++;
			os_timer_arm(&mqtt_connected_first_mqtt_rpc_timer, 100, 0);
			break;

		case 3:
			// 5. send mqtt reset_reason
			mqtt_rpc_reset_reason(&mqtt_client);
			mqtt_connected_first_mqtt_rpc_state++;
			os_timer_arm(&mqtt_connected_first_mqtt_rpc_timer, 100, 0);
			break;

		case 4:
			// 6. send fallback network status
			mqtt_rpc_fallback_status(&mqtt_client);
			mqtt_connected_first_mqtt_rpc_state++;
			os_timer_arm(&mqtt_connected_first_mqtt_rpc_timer, 100, 0);
			break;

		case 5:
			// 7. send mqtt uptime (last)
			mqtt_rpc_uptime(&mqtt_client);
			mqtt_connected_first_mqtt_rpc_state++;
			os_timer_arm(&mqtt_connected_first_mqtt_rpc_timer, 100, 0);
			break;

		default:
			mqtt_connected_first_mqtt_rpc_state = 0;
			os_timer_disarm(&mqtt_connected_first_mqtt_rpc_timer);
			break;
	}
}

ICACHE_FLASH_ATTR void static sample_mode_timer_func(void *arg) {
	unsigned char topic[MQTT_TOPIC_L];
	// temp var for serial string
	char meter_serial_temp[METER_SERIAL_LEN];	// DEBUG: 16 should be a #define...
	uint16_t calculated_crc;
	uint16_t saved_crc;
	
	if (fast_boot == false) {
		led_stop_pattern();	// stop indicating config mode mode with led

		// stop http configuration server
		httpdStop();

		// stop captive dns
		captdnsStop();
	}

	// reload save configuration
	cfg_load();

	// from here we are supposed to have a valid meter serial - write it to flash as mqtt id if it has changed
#ifdef EN61107
	tfp_snprintf(meter_serial_temp, METER_SERIAL_LEN, "%07u", en61107_get_received_serial());
#else
	tfp_snprintf(meter_serial_temp, METER_SERIAL_LEN, "%07u", kmp_get_received_serial());
#endif
	if (strncmp(sys_cfg.device_id, meter_serial_temp, 16 - 1) != 0) {
		memset(sys_cfg.device_id, 0, sizeof(sys_cfg.device_id));
		tfp_snprintf(sys_cfg.device_id, 16, "%s", meter_serial_temp);
		if (!cfg_save(&calculated_crc, &saved_crc)) {
			mqtt_flash_error(calculated_crc, saved_crc);
		}
	}
	
	MQTT_InitConnection(&mqtt_client, sys_cfg.mqtt_host, sys_cfg.mqtt_port, sys_cfg.security);
	if (!MQTT_InitClient(&mqtt_client, sys_cfg.device_id, sys_cfg.mqtt_user, sys_cfg.mqtt_pass, sys_cfg.mqtt_keepalive, 0)) {	// keep state
		INFO("Failed to initialize properly. Check MQTT version.\r\n");
		led_on();	// show error with LED
	}

	// set MQTT LWP topic
#ifdef EN61107
	tfp_snprintf(topic, MQTT_TOPIC_L, "/offline/v1/%07u", en61107_get_received_serial());
#else
	tfp_snprintf(topic, MQTT_TOPIC_L, "/offline/v1/%07u", kmp_get_received_serial());
#endif
	MQTT_InitLWT(&mqtt_client, topic, "", 0, 0);
	

	MQTT_OnConnected(&mqtt_client, mqtt_connected_cb);
	MQTT_OnDisconnected(&mqtt_client, mqtt_disconnected_cb);
	MQTT_OnPublished(&mqtt_client, mqtt_published_cb);
	MQTT_OnPingResp(&mqtt_client, mqtt_ping_response_cb);
	MQTT_OnData(&mqtt_client, mqtt_data_cb);
	MQTT_OnTimeout(&mqtt_client, mqtt_timeout_cb);

	wifi_set_fallback_retry_cb(mqtt_fallback_retry_cb);
	wifi_connect(wifi_changed_cb);
#ifdef EN61107
	tfp_snprintf(mesh_ssid, AP_SSID_LENGTH, AP_MESH_SSID, meter_serial_temp);
#else
	tfp_snprintf(mesh_ssid, AP_SSID_LENGTH, AP_MESH_SSID, meter_serial_temp);
#endif

	wifi_softap_config(mesh_ssid, sys_cfg.ap_mesh_pwd, AP_MESH_TYPE);
	wifi_softap_ip_config();

	add_watchdog(MQTT_WATCHDOG_ID, NETWORK_RESTART, MQTT_WATCHDOG_TIMEOUT);
	add_watchdog(MQTT_WATCHDOG_REBOOT_ID, REBOOT, MQTT_WATCHDOG_REBOOT_TIMEOUT);
}

ICACHE_FLASH_ATTR void static config_mode_timer_func(void *arg) {
	uint8_t ap_ssid[64];
	uint8_t ap_password[64];
#ifdef OTA_FW
	size_t i;
#endif

	led_pattern_c();	// indicate config mode mode with led
	// make sure the device is in AP and STA combined mode; otherwise we cant scan
	wifi_set_opmode_current(STATIONAP_MODE);
#ifdef EN61107
	tfp_snprintf((char *)ap_ssid, 32, AP_SSID, en61107_get_received_serial());
#else
	tfp_snprintf((char *)ap_ssid, 32, AP_SSID, kmp_get_received_serial());
#endif

#ifdef OTA_FW
	// Dynamically format the AP password from the first 8 raw bytes of sys_cfg.key
	for (i = 0; i < 8; i++) {
		tfp_snprintf((char *)&ap_password[i * 2], 3, "%02x", sys_cfg.key[i]);
	}
	ap_password[16] = '\0';
#else
	tfp_snprintf((char *)ap_password, 64, AP_PASSWORD);
#endif

	wifi_softap_config((char *)ap_ssid, (char *)ap_password, AP_TYPE);	// start AP with default configuration
	captdnsInit();										// start captive dns server
	httpd_user_init();									// start web server
}

ICACHE_FLASH_ATTR void static sample_timer_func(void *arg) {
#ifdef EN61107
	en61107_request_send();
#else
	kmp_request_send();
#endif	// EN61107
}

ICACHE_FLASH_ATTR void static sample_timer_first_func(void *arg) {
	// helper function to sample once when mqtt_connected_cb() is called
	sample_timer_func(NULL);
	os_timer_disarm(&sample_timer_first);	// done using this timer
}

#ifdef EN61107
ICACHE_FLASH_ATTR void static en61107_request_send_timer_func(void *arg) {
	en61107_request_send();
}
#else
ICACHE_FLASH_ATTR void static kmp_request_send_timer_func(void *arg) {
	kmp_request_send();
}
#endif

ICACHE_FLASH_ATTR void meter_is_ready(void) {
	if (fast_boot) {
		// fast boot, go in sample/station mode
#ifdef DEBUG
		printf("fast boot\n");
#endif
		os_timer_disarm(&sample_mode_timer);
		os_timer_setfn(&sample_mode_timer, (os_timer_func_t *)sample_mode_timer_func, NULL);
		os_timer_arm(&sample_mode_timer, WIFI_START_FAST_BOOT_DELAY_MS, 0);
	}
	else {
#ifdef DEBUG
		printf("normal boot\n");
#endif
#ifdef EN61107
		// and start ap mode
		os_timer_disarm(&config_mode_timer);
		os_timer_setfn(&config_mode_timer, (os_timer_func_t *)config_mode_timer_func, NULL);
		os_timer_arm(&config_mode_timer, 1000, 0);
#else
		// and start ap mode
		os_timer_disarm(&config_mode_timer);
		os_timer_setfn(&config_mode_timer, (os_timer_func_t *)config_mode_timer_func, NULL);
		os_timer_arm(&config_mode_timer, 1000, 0);
#endif

		// wait for 120 seconds from boot and go to station mode
		os_timer_disarm(&sample_mode_timer);
		os_timer_setfn(&sample_mode_timer, (os_timer_func_t *)sample_mode_timer_func, NULL);
#ifndef DEBUG_SHORT_WEB_CONFIG_TIME
#ifdef EN61107
		os_timer_arm(&sample_mode_timer, 120000 + 1000, 0);
#else
		os_timer_arm(&sample_mode_timer, 120000 + 1000, 0);
#endif
#else	// DEBUG_SHORT_WEB_CONFIG_TIME
		os_timer_arm(&sample_mode_timer, 2000, 0);
#endif
	}
}

ICACHE_FLASH_ATTR void meter_sent_data(void) {
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L + AES_HMAC_OVERHEAD];
	int mqtt_message_l;	
	// vars for aes encryption
	uint8_t cleartext[MQTT_MESSAGE_L];

	// compare last received energy to offline_close_at and close if needed
#ifdef EN61107
#ifdef FLOW_METER
	if (en61107_get_received_volume_l() >= sys_cfg.offline_close_at) {
		if (sys_cfg.ac_thermo_state) {
			ac_thermo_close();

			// send mqtt status
			// clear data
			memset(mqtt_topic, 0, sizeof(mqtt_topic));
			memset(mqtt_message, 0, sizeof(mqtt_message));
			memset(cleartext, 0, sizeof(cleartext));

			tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/status/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
			tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%s", sys_cfg.ac_thermo_state ? "open" : "close");

			// encrypt and send
			mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);

			if (mqtt_client.pCon != NULL) {
				// if mqtt_client is initialized
				MQTT_Publish(&mqtt_client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
#ifdef DEBUG
				os_printf("closed because en61107_get_received_volume_l >= offline_close_at (%u >= %u)\n", en61107_get_received_volume_l(), sys_cfg.offline_close_at);
#endif	// DEBUG
			}
		}
	}
#else
	if (en61107_get_received_energy_kwh() >= sys_cfg.offline_close_at) {
		if (sys_cfg.ac_thermo_state) {
			ac_thermo_close();

			// send mqtt status
			// clear data
			memset(mqtt_topic, 0, sizeof(mqtt_topic));
			memset(mqtt_message, 0, sizeof(mqtt_message));
			memset(cleartext, 0, sizeof(cleartext));

			tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/status/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
			tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%s", sys_cfg.ac_thermo_state ? "open" : "close");

			// encrypt and send
			mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);

			if (mqtt_client.pCon != NULL) {
				// if mqtt_client is initialized
				MQTT_Publish(&mqtt_client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
#ifdef DEBUG
				os_printf("closed because en61107_get_received_energy_kwh >= offline_close_at (%u >= %u)\n", en61107_get_received_energy_kwh(), sys_cfg.offline_close_at);
#endif	// DEBUG
			}
		}
	}
#endif	// FLOW_METER
#else	// KMP
#ifdef FLOW_METER
	if (kmp_get_received_volume_l() >= sys_cfg.offline_close_at) {
		if (sys_cfg.ac_thermo_state) {
			ac_thermo_close();

			// send mqtt status
			// clear data
			memset(mqtt_topic, 0, sizeof(mqtt_topic));
			memset(mqtt_message, 0, sizeof(mqtt_message));
			memset(cleartext, 0, sizeof(cleartext));

			tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/status/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
			tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%s", sys_cfg.ac_thermo_state ? "open" : "close");

			// encrypt and send
			mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);

			if (mqtt_client.pCon != NULL) {
				// if mqtt_client is initialized
				MQTT_Publish(&mqtt_client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
#ifdef DEBUG
				os_printf("closed because kmp_get_received_volume_l >= offline_close_at (%u >= %u)\n", kmp_get_received_volume_l(), sys_cfg.offline_close_at);
#endif	// DEBUG
			}
		}
	}
#else
	if (kmp_get_received_energy_kwh() >= sys_cfg.offline_close_at) {
		if (sys_cfg.ac_thermo_state) {
			ac_thermo_close();

			// send mqtt status
			// clear data
			memset(mqtt_topic, 0, sizeof(mqtt_topic));
			memset(mqtt_message, 0, sizeof(mqtt_message));
			memset(cleartext, 0, sizeof(cleartext));

			tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/status/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
			tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%s", sys_cfg.ac_thermo_state ? "open" : "close");

			// encrypt and send
			mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);

			if (mqtt_client.pCon != NULL) {
				// if mqtt_client is initialized
				MQTT_Publish(&mqtt_client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
#ifdef DEBUG
				os_printf("closed because kmp_get_received_energy_kwh >= offline_close_at (%u >= %u)\n", kmp_get_received_energy_kwh(), sys_cfg.offline_close_at);
#endif	// DEBUG
			}
		}
	}
#endif	// FLOW_METER
#endif	// EN61107
}

ICACHE_FLASH_ATTR static void mqtt_fallback_retry_cb(void) {
	MQTT_Connect(&mqtt_client);
}

ICACHE_FLASH_ATTR void wifi_changed_cb(uint8_t status) {
	if (status == STATION_GOT_IP) {
		MQTT_Connect(&mqtt_client);
#ifdef DEBUG
		os_printf("queue size(%ld/%ld)\n", mqtt_client.msgQueue.rb.fill_cnt, mqtt_client.msgQueue.rb.size);
#endif
	}
	else if (status == STATION_IDLE) {
		// Force drop the stale MQTT TCP socket so it reconnects instantly on the new network
		MQTT_Disconnect(&mqtt_client);
	}
}

ICACHE_FLASH_ATTR void static mqtt_connected_defer_timer_func(void *arg) {
	mqtt_connected_cb((uint32_t*)&mqtt_client);
}

ICACHE_FLASH_ATTR void static csa_report_timer_func(void *arg) {
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L + AES_HMAC_OVERHEAD];
	char cleartext[MQTT_MESSAGE_L];
	int mqtt_message_l;

	if (cnx_csa_new_call_flag) {
		cnx_csa_new_call_flag = false; // Reset flag

#ifdef DEBUG
		os_printf("CSA drop intercepted! Total cnx_csa_fn calls: %u\n", cnx_csa_call_count);
#endif

		if (mqtt_client.pCon != NULL) {
			memset(mqtt_message, 0, sizeof(mqtt_message));
			memset(cleartext, 0, sizeof(cleartext));

#ifdef EN61107
			tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/cnx_csa_fn_called/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#else
			tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/cnx_csa_fn_called/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif

			tfp_snprintf(cleartext, MQTT_MESSAGE_L, "count=%u", cnx_csa_call_count);

			mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
			MQTT_Publish(&mqtt_client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);
		}
	}
}

ICACHE_FLASH_ATTR void static wifi_test_report_timer_func(void *arg) {
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L + AES_HMAC_OVERHEAD];
	char cleartext[MQTT_MESSAGE_L];
	int mqtt_message_l;

#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/test_ssid_pwd_result/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/test_ssid_pwd_result/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif

	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));

	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "status=%s&ssid=%s&rssi=%d&reason=%s&time_ms=%u",
		wifi_test_ctx.test_result_status ? "ok" : "failed",
		wifi_test_ctx.target_ssid,
		wifi_test_ctx.tested_rssi,
		wifi_get_reason_desc(wifi_test_ctx.fail_reason),
		wifi_test_ctx.attempt_time_ms);

#ifdef DEBUG
	os_printf("Wi-Fi test report: topic=%s payload=%s\n", mqtt_topic, cleartext);
#endif

	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
	MQTT_Publish(&mqtt_client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);
}

ICACHE_FLASH_ATTR void mqtt_connected_cb(uint32_t *args) {
	// Mark MQTT as established when the active station network is the fallback network.
	if (wifi_fallback_active) {
		wifi_fallback_mqtt_connected = true;
		led_pattern_b();
	}
	else {
		wifi_fallback_mqtt_connected = false;
	}

#ifdef EN61107
	en61107_set_mqtt_client(&mqtt_client);
#else
	kmp_set_mqtt_client(&mqtt_client);
#endif

#ifdef EN61107
	if (en61107_get_received_serial() == 0) {
		en61107_request_send();
	}
#else
	if (kmp_get_received_serial() == 0) {
		kmp_request_send();
		// Trigger meter request immediately to get serial if not set yet
		// We MUST return here so we don't subscribe to /config/v2/0000000/#
		// Reschedule later to check if the serial UART read finished
		os_timer_disarm(&mqtt_connected_defer_timer);
		os_timer_setfn(&mqtt_connected_defer_timer, (os_timer_func_t *)mqtt_connected_defer_timer_func, NULL);
		os_timer_arm(&mqtt_connected_defer_timer, MQTT_CONNECTED_DEFER_DELAY_MS, 0);
		return;
	}
#endif

	// Report Wi-Fi test probe results if we just finished one
	if (wifi_test_ctx.pending_report) {
		wifi_test_ctx.pending_report = false;
		
		// Defer the publish by 3.5 seconds to dodge the QoS 2 queue collision 
		// caused by sample_timer_first (1.1s) and the RPC boot burst (2.0s)
		os_timer_disarm(&wifi_test_report_timer);
		os_timer_setfn(&wifi_test_report_timer, (os_timer_func_t *)wifi_test_report_timer_func, NULL);
		os_timer_arm(&wifi_test_report_timer, 3500, 0);
	}

	// Check for CSA drops periodically while MQTT is connected
	os_timer_disarm(&csa_report_timer);
	os_timer_setfn(&csa_report_timer, (os_timer_func_t *)csa_report_timer_func, NULL);
	os_timer_arm(&csa_report_timer, 5000, 1);       // check every 5 seconds

	// send initial mqtt rpc commands defered, so mqtt_tcpclient_recv() will not block for too long time
	mqtt_connected_first_mqtt_rpc_state = 0;
	os_timer_disarm(&mqtt_connected_first_mqtt_rpc_timer);
	os_timer_setfn(&mqtt_connected_first_mqtt_rpc_timer, (os_timer_func_t *)mqtt_connected_first_mqtt_rpc_timer_func, NULL);
	os_timer_arm(&mqtt_connected_first_mqtt_rpc_timer, 2000, 0);
	
	if (first_mqtt_connect_after_boot) {
		first_mqtt_connect_after_boot = false;
		// sample once outside this function...	
		os_timer_disarm(&sample_timer_first);
		os_timer_setfn(&sample_timer_first, (os_timer_func_t *)sample_timer_first_func, NULL);
		os_timer_arm(&sample_timer_first, MQTT_CONNECTED_FIRST_SAMPLE_DELAY_MS, 0);		// once after 1.1 second
	}

	// ...and start sample timer repeatedly
	os_timer_disarm(&sample_timer);
	os_timer_setfn(&sample_timer, (os_timer_func_t *)sample_timer_func, NULL);
	os_timer_arm(&sample_timer, 60000, 1);		// every 60 seconds
}

ICACHE_FLASH_ATTR void mqtt_disconnected_cb(uint32_t *args) {
#ifdef DEBUG
	printf("mqtt_disconnected_cb\n");
#endif
	if (wifi_fallback_active) {
		wifi_fallback_mqtt_connected = false;
	}
	// Stop the fast polling timer when MQTT connection drops
	os_timer_disarm(&csa_report_timer);

	MQTT_Connect(&mqtt_client);
//	wifi_default();
}

ICACHE_FLASH_ATTR void mqtt_published_cb(uint32_t *args) {
	reset_watchdog(MQTT_WATCHDOG_ID);
	reset_watchdog(MQTT_WATCHDOG_REBOOT_ID);
}

ICACHE_FLASH_ATTR void mqtt_ping_response_cb(uint32_t *args) {
	reset_watchdog(MQTT_WATCHDOG_ID);
	reset_watchdog(MQTT_WATCHDOG_REBOOT_ID);
}

ICACHE_FLASH_ATTR void mqtt_timeout_cb(uint32_t *args) {
#ifdef DEBUG
	printf("mqtt_timeout_cb\n");
#endif
//	wifi_default();
}
	
ICACHE_FLASH_ATTR void mqtt_data_cb(uint32_t *args, const char* topic, uint32_t topic_len, const char *data, uint32_t data_len) {
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L + AES_HMAC_OVERHEAD];

	char *str;
	char function_name[FUNCTIONNAME_L];

	uint64_t received_unix_time = 0;
	
	uint8_t i;

#ifdef DEBUG
	os_printf("topic_len: %d, data_len: %d\n", topic_len, data_len);
#endif
	if (topic_len == 0 || topic_len >= MQTT_TOPIC_L || data_len == 0 || data_len >= (MQTT_MESSAGE_L + AES_HMAC_OVERHEAD)) {
#ifdef DEBUG
		os_printf("data length error\n");
#endif
		return;
	}
	
	// copy topic and null terminate
	memset(mqtt_topic, 0, sizeof(mqtt_topic));
	if (topic_len < MQTT_TOPIC_L && topic_len > 0) {	// don't memcpy 0 bytes or if too large to fit
		memcpy(mqtt_topic, topic, topic_len);
		mqtt_topic[topic_len] = 0;
	}

	// copy message and null terminate
	if (data_len < MQTT_MESSAGE_L && data_len > 0) {	// don't memcpy 0 bytes or if too large to fit
		memcpy(mqtt_message, data, data_len);
		mqtt_message[data_len] = 0;
	}
	
	memset(cleartext, 0, MQTT_MESSAGE_L);	// ensure cleartext buffer is null terminated

	// decrypt message and verify HMAC
	if (decrypt_aes_hmac_combined(cleartext, mqtt_topic, topic_len, mqtt_message, data_len) == 0) {
#ifdef DEBUG
		printf("hmac error\n");
#endif
		return;
	}
	
	// clear encrypted message for security
	memset(mqtt_message, 0, sizeof(mqtt_message));
	
	// parse mqtt topic for function call name
	i = 0;
	str = strtok(mqtt_topic, "/");
	while (str != NULL) {
		if (i == 3) {	// get unix_time sent via mqtt topic
			received_unix_time = atoi(str);
		}
		strncpy(function_name, str, FUNCTIONNAME_L);   // save last parameter as function_name
		str = strtok(NULL, "/");
		i++;
	}
	
	// replay attack countermeasure - 1 hour time window
	if ((received_unix_time < (get_unix_time() - 1800)) || (received_unix_time > (get_unix_time() + 1800))) {
		return;
	}
	
	// clear topic buffer for further use
	memset(mqtt_topic, 0, sizeof(mqtt_topic));
	
	// mqtt rpc dispatcher goes here
	if (strncmp(function_name, "ping", FUNCTIONNAME_L) == 0) {
		// found ping
		mqtt_rpc_ping(&mqtt_client);
	}
	else if (strncmp(function_name, "version", FUNCTIONNAME_L) == 0) {
		// found version
		mqtt_rpc_version(&mqtt_client);
	}
	else if (strncmp(function_name, "uptime", FUNCTIONNAME_L) == 0) {
		// found uptime
		mqtt_rpc_uptime(&mqtt_client);
	}
	else if (strncmp(function_name, "vdd", FUNCTIONNAME_L) == 0) {
		// found vdd - get voltage level
		mqtt_rpc_vdd(&mqtt_client);
	}
	else if (strncmp(function_name, "rssi", FUNCTIONNAME_L) == 0) {
		// found rssi
		mqtt_rpc_rssi(&mqtt_client);
	}
	else if (strncmp(function_name, "ssid", FUNCTIONNAME_L) == 0) {
		// found ssid
		mqtt_rpc_ssid(&mqtt_client);
	}
	else if (strncmp(function_name, "scan", FUNCTIONNAME_L) == 0) {
		// found scan
		mqtt_rpc_scan(&mqtt_client);
	}
	else if (strncmp(function_name, "set_ssid", FUNCTIONNAME_L) == 0) {
		// found set_ssid
		mqtt_rpc_set_ssid(&mqtt_client, cleartext);
	}
	else if (strncmp(function_name, "set_pwd", FUNCTIONNAME_L) == 0) {
		// found set_pwd
		mqtt_rpc_set_pwd(&mqtt_client, cleartext);
	}
	else if (strncmp(function_name, "set_ssid_pwd", FUNCTIONNAME_L) == 0) {
		// found set_ssid_pwd
		mqtt_rpc_set_ssid_pwd(&mqtt_client, cleartext);
	}
	else if (strncmp(function_name, "set_ap_mesh_pwd", FUNCTIONNAME_L) == 0) {
		// found set_ap_mesh_pwd
		mqtt_rpc_set_ap_mesh_pwd(&mqtt_client, cleartext);
	}
	else if (strncmp(function_name, "test_ssid_pwd", FUNCTIONNAME_L) == 0) {
		// found test_ssid_pwd
		mqtt_rpc_test_ssid_pwd(&mqtt_client, cleartext);
	}
	else if (strncmp(function_name, "reconnect", FUNCTIONNAME_L) == 0) {
		// found reconnect
		mqtt_rpc_reconnect(&mqtt_client);
	}
	else if (strncmp(function_name, "disconnect_count", FUNCTIONNAME_L) == 0) {
		// found disconnect_count
		mqtt_rpc_disconnect_count(&mqtt_client);
	}
	else if (strncmp(function_name, "network_quality", FUNCTIONNAME_L) == 0) {
		// found network_quality
		mqtt_rpc_network_quality(&mqtt_client);
	}
	else if (strncmp(function_name, "wifi_status", FUNCTIONNAME_L) == 0) {
		// found wifi_status
		mqtt_rpc_wifi_status(&mqtt_client);
	}
	else if (strncmp(function_name, "ap_status", FUNCTIONNAME_L) == 0) {
		// found ap_status
		mqtt_rpc_ap_status(&mqtt_client);
	}
	else if (strncmp(function_name, "start_ap", FUNCTIONNAME_L) == 0) {
		// found start_ap
		mqtt_rpc_start_ap(&mqtt_client, mesh_ssid);
	}
	else if (strncmp(function_name, "stop_ap", FUNCTIONNAME_L) == 0) {
		// found stop_ap
		mqtt_rpc_stop_ap(&mqtt_client);
	}
	else if (strncmp(function_name, "start_fallback_ap", FUNCTIONNAME_L) == 0) {
		// found start_fallback_ap
		mqtt_rpc_start_fallback_ap(&mqtt_client, cleartext, mesh_ssid);
	}
	else if (strncmp(function_name, "fallback_status", FUNCTIONNAME_L) == 0) {
		// found fallback_status
		mqtt_rpc_fallback_status(&mqtt_client);
	}
	else if (strncmp(function_name, "mem", FUNCTIONNAME_L) == 0) {
		// found mem
		mqtt_rpc_mem(&mqtt_client);
	}
	else if (strncmp(function_name, "chip_id", FUNCTIONNAME_L) == 0) {
		// found chip_id
		mqtt_rpc_chip_id(&mqtt_client);
	}
	else if (strncmp(function_name, "flash_id", FUNCTIONNAME_L) == 0) {
		// found flash_id
		mqtt_rpc_flash_id(&mqtt_client);
	}
	else if (strncmp(function_name, "flash_size", FUNCTIONNAME_L) == 0) {
		// found flash_size
		mqtt_rpc_flash_size(&mqtt_client);
	}
	else if (strncmp(function_name, "reset_reason", FUNCTIONNAME_L) == 0) {
		// found reset_reason
		mqtt_rpc_reset_reason(&mqtt_client);
	}
	else if (strncmp(function_name, "restart", FUNCTIONNAME_L) == 0) {
		// found restart
		mqtt_rpc_restart(&mqtt_client);
	}
	else if (strncmp(function_name, "ota_update", FUNCTIONNAME_L) == 0) {
		// pass the full decrypted payload text string to be parsed
		mqtt_rpc_ota_update(&mqtt_client, cleartext);
	}
	else if (strncmp(function_name, "set_key", FUNCTIONNAME_L) == 0) {
		// handle standalone master key injection
		mqtt_rpc_set_key(&mqtt_client, cleartext);
	}
	else if (strncmp(function_name, "set_mqtt_host", FUNCTIONNAME_L) == 0) {
		// handle MQTT host change
		mqtt_rpc_set_mqtt_host(&mqtt_client, cleartext);
	}
#ifdef DEBUG_STACK_TRACE
	else if (strncmp(function_name, "stack_trace", FUNCTIONNAME_L) == 0) {
		// found stack_trace
		mqtt_rpc_stack_trace(&mqtt_client);
	}
	else if (strncmp(function_name, "read_stack_trace", FUNCTIONNAME_L) == 0) {
		// found stack_trace
		mqtt_rpc_read_stack_trace(&mqtt_client, cleartext);
	}
#endif	// DEBUG_STACK_TRACE
#ifndef NO_CRON
	else if (strncmp(function_name, "set_cron", FUNCTIONNAME_L) == 0) {
		// found set_cron
		mqtt_rpc_set_cron(&mqtt_client, cleartext);
	}
	else if (strncmp(function_name, "clear_cron", FUNCTIONNAME_L) == 0) {
		// found clear_cron
		mqtt_rpc_clear_cron(&mqtt_client);
	}
	else if (strncmp(function_name, "cron", FUNCTIONNAME_L) == 0) {
		// found cron
		mqtt_rpc_cron(&mqtt_client);
	}
#endif	// NO_CRON
	else if (strncmp(function_name, "open", FUNCTIONNAME_L) == 0) {
		// found open
		mqtt_rpc_open(&mqtt_client);
	}
	else if (strncmp(function_name, "open_until", FUNCTIONNAME_L) == 0) {
		// found open_until
		mqtt_rpc_open_until(&mqtt_client, cleartext);
	}
	else if (strncmp(function_name, "open_until_delta", FUNCTIONNAME_L) == 0) {
		// found open_until_delta
		mqtt_rpc_open_until_delta(&mqtt_client, cleartext);
	}
	else if (strncmp(function_name, "close", FUNCTIONNAME_L) == 0) {
		// found close
		mqtt_rpc_close(&mqtt_client);
	}
	else if (strncmp(function_name, "status", FUNCTIONNAME_L) == 0) {
		// found status
		mqtt_rpc_status(&mqtt_client);
	}
}

ICACHE_FLASH_ATTR void mqtt_send_wifi_scan_results_cb(const struct bss_info *info) {
	char cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L + AES_HMAC_OVERHEAD];
	int mqtt_message_l;

	char ssid_escaped[SSID_ESCAPED_LENGTH + 1];
	size_t i, j;
	
	char auth_mode_string[13];
	char pairwise_cipher_string[10];
	char group_cipher_string[10];

	switch (info->authmode) {
		case AUTH_OPEN:
			strcpy(auth_mode_string, "OPEN");
			break;
		case AUTH_WEP:
			strcpy(auth_mode_string, "WEP");
			break;
		case AUTH_WPA_PSK:
			strcpy(auth_mode_string, "WPA_PSK");
			break;
		case AUTH_WPA2_PSK:
			strcpy(auth_mode_string, "WPA2_PSK");
			break;
		case AUTH_WPA_WPA2_PSK:
			strcpy(auth_mode_string, "WPA_WPA2_PSK");
			break;
		case AUTH_MAX:
			strcpy(auth_mode_string, "MAX");
			break;
		default:
			strcpy(auth_mode_string, "");
			break;
	}

	switch (info->pairwise_cipher) {
		case CIPHER_NONE:
			strcpy(pairwise_cipher_string, "NONE");
			break;
		case CIPHER_WEP40:
			strcpy(pairwise_cipher_string, "WEP40");
			break;
		case CIPHER_WEP104:
			strcpy(pairwise_cipher_string, "WEP104");
			break;
		case CIPHER_TKIP:
			strcpy(pairwise_cipher_string, "TKIP");
			break;
		case CIPHER_CCMP:
			strcpy(pairwise_cipher_string, "CCMP");
			break;
		case CIPHER_TKIP_CCMP:
			strcpy(pairwise_cipher_string, "TKIP_CCMP");
			break;
		case CIPHER_UNKNOWN:
			strcpy(pairwise_cipher_string, "UNKNOWN");
			break;
		default:
			strcpy(pairwise_cipher_string, "");
			break;
	}

	switch (info->group_cipher) {
		case CIPHER_NONE:
			strcpy(group_cipher_string, "NONE");
			break;
		case CIPHER_WEP40:
			strcpy(group_cipher_string, "WEP40");
			break;
		case CIPHER_WEP104:
			strcpy(group_cipher_string, "WEP104");
			break;
		case CIPHER_TKIP:
			strcpy(group_cipher_string, "TKIP");
			break;
		case CIPHER_CCMP:
			strcpy(group_cipher_string, "CCMP");
			break;
		case CIPHER_TKIP_CCMP:
			strcpy(group_cipher_string, "TKIP_CCMP");
			break;
		case CIPHER_UNKNOWN:
			strcpy(group_cipher_string, "UNKNOWN");
			break;
		default:
			strcpy(group_cipher_string, "");
			break;
	}

	// escape '&' since we are using it as separator
	memset(ssid_escaped, 0, SSID_ESCAPED_LENGTH + 1);
	for (i = 0, j = 0; i < strlen(info->ssid); i++) {
		if (*(info->ssid + i) == '&') {
			strncpy(ssid_escaped + j, "%26", 3);
			j += 3;
		}
		else {
			strncpy(ssid_escaped + j, info->ssid + i, 1);
			j++;
		}
	}

#ifdef DEBUG
	os_printf("ssid=%s&bssid=%02x:%02x:%02x:%02x:%02x:%02x&rssi=%d&channel=%d&auth_mode=%s&pairwise_cipher=%s&group_cipher=%s&phy_11b=%d&phy_11g=%d&phy_11n=%d&wps=%d\n", 
		ssid_escaped, 
		info->bssid[0], 
		info->bssid[1], 
		info->bssid[2], 
		info->bssid[3], 
		info->bssid[4], 
		info->bssid[5], 
		info->rssi, 
		info->channel,
		auth_mode_string,
		pairwise_cipher_string,
		group_cipher_string,
		info->phy_11b,
		info->phy_11g,
		info->phy_11n,
		info->wps
	);
#endif

#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/scan_result/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/scan_result/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif
	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));
	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "ssid=%s&bssid=%02x:%02x:%02x:%02x:%02x:%02x&rssi=%d&channel=%d&auth_mode=%s&pairwise_cipher=%s&group_cipher=%s&phy_11b=%d&phy_11g=%d&phy_11n=%d&wps=%d", 
		ssid_escaped, 
		info->bssid[0], 
		info->bssid[1], 
		info->bssid[2], 
		info->bssid[3], 
		info->bssid[4], 
		info->bssid[5], 
		info->rssi, 
		info->channel,
		auth_mode_string,
		pairwise_cipher_string,
		group_cipher_string,
		info->phy_11b,
		info->phy_11g,
		info->phy_11n,
		info->wps
	);
	// encrypt and send
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
	MQTT_Publish(&mqtt_client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
}

ICACHE_FLASH_ATTR void user_gpio_init() {
	// gpio0 as input and enable pull up
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO0_U, FUNC_GPIO0);
	GPIO_DIS_OUTPUT(GPIO_ID_PIN(0));
	PIN_PULLUP_EN(PERIPHS_IO_MUX_GPIO0_U);

	// gpio2 as input and enable pull up
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO2_U, FUNC_GPIO2);
	GPIO_DIS_OUTPUT(GPIO_ID_PIN(2));
	PIN_PULLUP_EN(PERIPHS_IO_MUX_GPIO2_U);

	// gpio15 as input, pulled down by external resistor
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDO_U, FUNC_GPIO15);
	GPIO_DIS_OUTPUT(GPIO_ID_PIN(15));
}

ICACHE_FLASH_ATTR void user_init(void) {
	size_t flash_size;
	
	system_update_cpu_freq(160);
	uart_init(BIT_RATE_115200, BIT_RATE_115200);
	flash_size = spi_flash_size();

	printf("\n\r");
	printf("SDK version: %s\n\r", system_get_sdk_version());
	printf("Software version: %s\n\r", VERSION);
	printf("LWIP version: %s\n\r", LWIP_VERSION);
	printf("Hardware model: %s\n\r", HW_MODEL);
	printf("Chip ID: 0x%06x\n\r", system_get_chip_id());
	printf("Flash ID: 0x%x%s, size: %u kB\n\r", spi_flash_get_id(), flash_size ? "" : " (unknown manufacturer)", flash_size / 1024);

#ifdef DEBUG
	printf("\t(DEBUG)\n\r");
#endif

#ifdef DEBUG_NO_METER
	printf("\t(DEBUG_NO_METER)\n\r");
#endif

#ifdef DEBUG_SHORT_WEB_CONFIG_TIME
	printf("\t(DEBUG_SHORT_WEB_CONFIG_TIME)\n\r");
#endif

#ifdef NO_CRON
	printf("\t(NO_CRON)\n\r");
#endif
	
#ifdef DEBUG_STACK_TRACE
	printf("\t(DEBUG_STACK_TRACE)\n\r");
#endif

#ifdef FLOW_METER
	printf("\t(FLOW_METER)\n\r");
#endif

#ifndef DEBUG_NO_METER
#ifdef THERMO_NO
	printf("\t(THERMO_NO)\n\r");
#else
	printf("\t(THERMO_NC)\n\r");
#endif
#endif	// not DEBUG_NO_METER

#ifndef DEBUG_NO_METER
#ifdef THERMO_ON_AC_2
	printf("\t(THERMO_ON_AC_2)\n\r");
#endif
#endif	// not DEBUG_NO_METER

#ifndef AUTO_CLOSE	// reversed!
	printf("\t(NO_AUTO_CLOSE)\n\r");
#endif
	
#ifdef AC_TEST
	printf("\t(AC_TEST)\n\r");
#endif

#if !(defined(DEBUG) || defined(DEBUG_NO_METER))
#ifdef EN61107
	uart_init(BIT_RATE_300, BIT_RATE_300);
#else
	uart_init(BIT_RATE_1200, BIT_RATE_1200);
#endif
#endif	// DEBUG_NO_METER

	// clear mqtt_client
	memset(&mqtt_client, 0, sizeof(MQTT_Client));

#if !(defined(DEBUG) || defined(DEBUG_NO_METER))
	// disable serial debug
	system_set_os_print(0);
#endif
	// start watchdog
	init_watchdog();
	start_watchdog();

	cfg_load();

	// init crypto
	init_aes_hmac_combined(sys_cfg.key);
	
	// start kmp_request
#ifdef EN61107
	en61107_request_init();
#else
	kmp_request_init();
#endif
	
	// initialize the GPIO subsystem
	gpio_init();
	user_gpio_init();
	ac_out_init();
#ifndef NO_CRON
	cron_init();
#endif	// NO_CRON

	led_init();
	
	// load thermo motor state from flash(AC OUT 1)
	if (sys_cfg.ac_thermo_state) {
		ac_thermo_open();
	}
	else {
		ac_thermo_close();
	}

	// Initialize NAT mapping tables before manipulating network interfaces
	wifi_init_napt();

	// dont enable wireless before we have configured ssid
	wifi_set_opmode_current(NULL_MODE);
	wifi_station_disconnect();
	// disale auto connect, we handle reconnect with this event handler
	wifi_station_set_auto_connect(0);
	wifi_station_set_reconnect_policy(1);

	// do everything else in system_init_done
	system_init_done_cb(&system_init_done);
}

ICACHE_FLASH_ATTR void system_init_done(void) {
#ifdef DEBUG
	uint32_t watchdog_rebooted;
#endif
	struct rst_info *rtc_info;
	rtc_info = system_get_rst_info();

	// Evaluate boot type immediately so timers can use it
	if ((rtc_info != NULL) && (rtc_info->reason != REASON_DEFAULT_RST) && (rtc_info->reason != REASON_EXT_SYS_RST)) {
		fast_boot = true;
	} else {
		fast_boot = false;
	}

#ifdef DEBUG
	if (load_rtc_data(&watchdog_rebooted)) {
		printf("rst: %d\n", 7);
	}
	else {
		printf("rst: %d\n", (rtc_info != NULL) ? rtc_info->reason : -1);
	}
	if (rtc_info->reason == REASON_WDT_RST || rtc_info->reason == REASON_EXCEPTION_RST || rtc_info->reason == REASON_SOFT_WDT_RST) {
		if (rtc_info->reason == REASON_EXCEPTION_RST) {
			os_printf("Fatal exception (%d):\n", rtc_info->exccause);
		}
		os_printf("epc1=0x%08x, epc2=0x%08x, epc3=0x%08x, excvaddr=0x%08x, depc=0x%08x\n", rtc_info->epc1, rtc_info->epc2, rtc_info->epc3, rtc_info->excvaddr, rtc_info->depc);	//The address of the last crash is printed, which is used to debug garbled output.
	}
#endif	// DEBUG

	init_unix_time();
	
	init_icmp_ping();

#ifdef AC_TEST
	ac_test();
#endif	// AC_TEST
	
	// start config mode/sample mode in meter_is_ready() via callback
#ifdef EN61107
	en61107_register_meter_is_ready_cb(meter_is_ready);
#ifdef AUTO_CLOSE
	en61107_register_meter_sent_data_cb(meter_sent_data);
#endif	// AUTO_CLOSE
#else
	kmp_register_meter_is_ready_cb(meter_is_ready);
#ifdef AUTO_CLOSE
	kmp_register_meter_sent_data_cb(meter_sent_data);
#endif	// AUTO_CLOSE
#endif	// EN61107

	// wait 10 seconds before starting wifi and let the meter boot
	// and send serial number request
#ifdef EN61107
	os_timer_disarm(&en61107_request_send_timer);
	os_timer_setfn(&en61107_request_send_timer, (os_timer_func_t *)en61107_request_send_timer_func, NULL);
	os_timer_arm(&en61107_request_send_timer, fast_boot ? METER_POLL_FAST_BOOT_DELAY_MS : METER_POLL_COLD_BOOT_DELAY_MS, 0);
#else
	os_timer_disarm(&kmp_request_send_timer);
	os_timer_setfn(&kmp_request_send_timer, (os_timer_func_t *)kmp_request_send_timer_func, NULL);
	os_timer_arm(&kmp_request_send_timer, fast_boot ? METER_POLL_FAST_BOOT_DELAY_MS : METER_POLL_COLD_BOOT_DELAY_MS, 0);
#endif	// EN61107
}

ICACHE_FLASH_ATTR void mqtt_flash_error(uint16_t calculated_crc, uint16_t saved_crc) {
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L + AES_HMAC_OVERHEAD];
	int mqtt_message_l;	
	// vars for aes encryption
	char cleartext[MQTT_MESSAGE_L];

#ifdef DEBUG
	printf("config crc error when saving, possible flash memory error calculated_crc=0x%x&saved_crc=0x%x\n\r", calculated_crc, saved_crc);
#endif // DEBUG

	// clear data
	memset(mqtt_topic, 0, sizeof(mqtt_topic));
	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));

#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/flash_error/v2/%07u/%llu", en61107_get_received_serial(), get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/flash_error/v2/%07u/%llu", kmp_get_received_serial(), get_unix_time());
#endif
	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "calculated_crc=0x%x&saved_crc=0x%x", calculated_crc, saved_crc);

	// encrypt and send
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);

	if (mqtt_client.pCon != NULL) {
		// if mqtt_client is initialized
		MQTT_Publish(&mqtt_client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
	}
}
