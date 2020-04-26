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
#include "driver/ext_spi_flash.h"
#include "watchdog.h"
#include "version.h"

#ifdef EN61107
#include "en61107_request.h"
#elif defined IMPULSE
// nothing
#else
#include "kmp_request.h"
#endif

bool fast_boot;

#ifdef IMPULSE
uint32_t impulse_meter_units;
//float impulse_meter_units;
uint32_t impulses_per_unit;

volatile uint32_t impulse_time;
volatile uint32_t last_impulse_time;
volatile uint32_t current_units;	// in W/m3

volatile uint32_t last_impulse_meter_count;

uint32_t impulse_falling_edge_time;
uint32_t impulse_rising_edge_time;

uint32_t last_uptime;
#endif // ENDIF IMPULSE

MQTT_Client mqtt_client;
static os_timer_t sample_timer_first;
static os_timer_t sample_timer;
static os_timer_t config_mode_timer;
static os_timer_t sample_mode_timer;
static os_timer_t mqtt_connected_first_mqtt_rpc_timer;
static os_timer_t mqtt_connected_defer_timer;
#ifdef EN61107
static os_timer_t en61107_request_send_timer;
#elif defined IMPULSE
//static os_timer_t kmp_request_send_timer;
#else
static os_timer_t kmp_request_send_timer;
#endif
uint8_t mqtt_connected_first_mqtt_rpc_state;	// state variable for mqtt_connected_first_mqtt_rpc_timer_func()

#ifdef IMPULSE
static os_timer_t impulse_meter_calculate_timer;
//static os_timer_t spi_test_timer;	// DEBUG
#endif

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

	switch (mqtt_connected_first_mqtt_rpc_state) {
		case 0:
		// subscribe to /config/v2/[serial]/#
#ifdef EN61107
			tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/config/v2/%07u/#", en61107_get_received_serial());
#elif defined IMPULSE
			tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/config/v2/%s/#", sys_cfg.impulse_meter_serial);
#else
			tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/config/v2/%07u/#", kmp_get_received_serial());
#endif
			MQTT_Subscribe(&mqtt_client, mqtt_topic, 0);
			mqtt_connected_first_mqtt_rpc_state++;
			os_timer_arm(&mqtt_connected_first_mqtt_rpc_timer, 100, 0);
			break;
		case 1:
			// send mqtt version
			mqtt_rpc_version(&mqtt_client);
			mqtt_connected_first_mqtt_rpc_state++;
			os_timer_arm(&mqtt_connected_first_mqtt_rpc_timer, 100, 0);
			break;
		case 2:
			// send mqtt uptime
			mqtt_rpc_uptime(&mqtt_client);
			mqtt_connected_first_mqtt_rpc_state++;
			os_timer_arm(&mqtt_connected_first_mqtt_rpc_timer, 100, 0);
			break;
		case 3:
			// send mqtt reset_reason
			mqtt_rpc_reset_reason(&mqtt_client);
			mqtt_connected_first_mqtt_rpc_state++;
			os_timer_arm(&mqtt_connected_first_mqtt_rpc_timer, 100, 0);
			break;
#ifndef IMPULSE
		case 4:
			// send mqtt status
			mqtt_rpc_status(&mqtt_client);
			mqtt_connected_first_mqtt_rpc_state++;
			os_timer_arm(&mqtt_connected_first_mqtt_rpc_timer, 100, 0);
			break;
#endif
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
#ifdef IMPULSE
	uint32_t impulse_meter_count_temp;
#endif // IMPULSE
	uint16_t calculated_crc;
	uint16_t saved_crc;
	
	if (fast_boot == false) {
		led_stop_pattern();	// stop indicating config mode mode with led

		// stop http configuration server
		httpdStop();

		// stop captive dns
		captdnsStop();
	}

#ifdef IMPULSE
	// save sys_cfg.impulse_meter_count - in case it has been incremented since cfg_load() at boot
	impulse_meter_count_temp = sys_cfg.impulse_meter_count;
#endif // IMPULSE
	// reload save configuration
	cfg_load();
#ifdef IMPULSE
	// ...and restore sys_cfg.impulse_meter_count
	sys_cfg.impulse_meter_count = impulse_meter_count_temp;
#endif // IMPULSE
	
	// from here we are supposed to have a valid meter serial - write it to flash as mqtt id if it has changed
#ifdef EN61107
	tfp_snprintf(meter_serial_temp, METER_SERIAL_LEN, "%07u", en61107_get_received_serial());
#elif defined IMPULSE
	tfp_snprintf(meter_serial_temp, METER_SERIAL_LEN, "%s", sys_cfg.impulse_meter_serial);
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
#elif defined IMPULSE
	tfp_snprintf(topic, MQTT_TOPIC_L, "/offline/v1/%s", sys_cfg.impulse_meter_serial);
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

	wifi_connect(wifi_changed_cb);
#ifdef EN61107
	tfp_snprintf(mesh_ssid, AP_SSID_LENGTH, AP_MESH_SSID, meter_serial_temp);
#elif defined IMPULSE
	tfp_snprintf(mesh_ssid, AP_SSID_LENGTH, AP_MESH_SSID, sys_cfg.impulse_meter_serial);
#else
	tfp_snprintf(mesh_ssid, AP_SSID_LENGTH, AP_MESH_SSID, meter_serial_temp);
#endif

	wifi_softap_config(mesh_ssid, AP_MESH_PASS, AP_MESH_TYPE);
	wifi_softap_ip_config();

	add_watchdog(MQTT_WATCHDOG_ID, NETWORK_RESTART, MQTT_WATCHDOG_TIMEOUT);
}

ICACHE_FLASH_ATTR void static config_mode_timer_func(void *arg) {
	uint8_t ap_ssid[64];
	uint8_t ap_password[64];

	led_pattern_c();	// indicate config mode mode with led
	// make sure the device is in AP and STA combined mode; otherwise we cant scan
	wifi_set_opmode_current(STATIONAP_MODE);
#ifdef EN61107
	tfp_snprintf(ap_ssid, 32, AP_SSID, en61107_get_received_serial());
#elif defined IMPULSE
	tfp_snprintf(ap_ssid, 32, AP_SSID, sys_cfg.impulse_meter_serial);
#else
	tfp_snprintf(ap_ssid, 32, AP_SSID, kmp_get_received_serial());
#endif
	tfp_snprintf(ap_password, 64, AP_PASSWORD);

	wifi_softap_config(ap_ssid, ap_password, AP_TYPE);	// start AP with default configuration
	captdnsInit();										// start captive dns server
	httpd_user_init();									// start web server
}

ICACHE_FLASH_ATTR void static sample_timer_func(void *arg) {
#ifdef IMPULSE
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L];
	int mqtt_message_l;	
	// vars for aes encryption
	uint8_t cleartext[MQTT_MESSAGE_L];

	// for saving operating time
	uint16_t calculated_crc;
	uint16_t saved_crc;
	uint32_t uptime;
	uint32_t uptime_diff;
#endif

#ifdef EN61107
	en61107_request_send();
#elif defined IMPULSE
	uint32_t acc_units;
	
	// for pseudo float print
	char current_units_string[32];
	char acc_units_string[32];
	
	uint32_t result_int, result_frac;
	unsigned char leading_zeroes[16];
	unsigned int i;
	
	// save operating time in seconds
	uptime = get_uptime();
	uptime_diff = uptime - last_uptime;
	sys_cfg.operating_time += uptime_diff;
	last_uptime = uptime;
	if (!cfg_save(&calculated_crc, &saved_crc)) {
		mqtt_flash_error(calculated_crc, saved_crc);
	}

	// clear data
	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));

	if (impulse_time > (get_uptime() - 60)) {	// only send mqtt if impulse received last minute
		acc_units = impulse_meter_units + (sys_cfg.impulse_meter_count * (1000 / impulses_per_unit));
	
		// for acc_units...
		// ...divide by 1000 and prepare decimal string in kWh
		result_int = (int32_t)(acc_units / 1000);
		result_frac = acc_units - result_int * 1000;
	
		// prepare decimal string
		strcpy(leading_zeroes, "");
		for (i = 0; i < (3 - decimal_number_length(result_frac)); i++) {
			strcat(leading_zeroes, "0");
		}
		tfp_snprintf(acc_units_string, 32, "%u.%s%u", result_int, leading_zeroes, result_frac);

#ifndef FLOW_METER
		// for current_units...
		// ...divide by 1000 and prepare decimal string in kWh
		result_int = (int32_t)(current_units / 1000);
		result_frac = current_units - result_int * 1000;
		
		// prepare decimal string
		strcpy(leading_zeroes, "");
		for (i = 0; i < (3 - decimal_number_length(result_frac)); i++) {
			strcat(leading_zeroes, "0");
		}
		tfp_snprintf(current_units_string, 32, "%u.%s%u", result_int, leading_zeroes, result_frac);
#else
		tfp_snprintf(current_units_string, 32, "%u", current_units);
#endif	// FLOW_METER

		tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/sample/v2/%s/%u", sys_cfg.impulse_meter_serial, get_unix_time());

#ifndef FLOW_METER
		tfp_snprintf(cleartext, MQTT_MESSAGE_L, "heap=%u&effect1=%s kW&e1=%s kWh&hr=%u&", system_get_free_heap_size(), current_units_string, acc_units_string, (uint32_t)(sys_cfg.operating_time / 3600));
#else
		tfp_snprintf(cleartext, MQTT_MESSAGE_L, "heap=%u&flow1=%s l/h&v1=%s m3&hr=%u&", system_get_free_heap_size(), current_units_string, acc_units_string, (uint32_t)(sys_cfg.operating_time / 3600));
#endif	// FLOW_METER
		
		// encrypt and send
		mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);

		if (mqtt_client.pCon != NULL) {
			// if mqtt_client is initialized
			MQTT_Publish(&mqtt_client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
#ifdef DEBUG
			printf("sample_timer_func publish\n");
#endif	// DEBUG
		}

		// set offset for next calculation
		last_impulse_meter_count = sys_cfg.impulse_meter_count;
		last_impulse_time = impulse_time;
	}
	else {
		// send ping to keep mqtt alive
		if (mqtt_client.pCon != NULL) {
			// if mqtt_client is initialized
			MQTT_Ping(&mqtt_client);
		}
	}

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
#elif defined IMPULSE
	// nothing
#else
ICACHE_FLASH_ATTR void static kmp_request_send_timer_func(void *arg) {
	kmp_request_send();
}
#endif

#ifdef IMPULSE
ICACHE_FLASH_ATTR void static impulse_meter_calculate_timer_func(void *arg) {
	uint32_t impulse_time_diff;
	uint32_t impulse_meter_count_diff;
	uint16_t calculated_crc;
	uint16_t saved_crc;

	if (!cfg_save(&calculated_crc, &saved_crc)) {
		mqtt_flash_error(calculated_crc, saved_crc);
	}
	
	impulse_time = get_uptime();
	impulse_time_diff = impulse_time - last_impulse_time;
	
	impulse_meter_count_diff = sys_cfg.impulse_meter_count - last_impulse_meter_count;
#ifdef DEBUG
	printf("count: %u\tl count: %u\timp time: %u\tlast imp time: %u\n", sys_cfg.impulse_meter_count, last_impulse_meter_count, impulse_time, last_impulse_time);
	printf("count diff: %u\timp time diff: %u\n", impulse_meter_count_diff, impulse_time_diff);
#endif

	if (impulse_time_diff && impulse_meter_count_diff) {	// only calculate if not zero interval or zero meter count diff - should not happen
		current_units = 3600 * (1000 / impulses_per_unit) * impulse_meter_count_diff / impulse_time_diff;
	}

#ifdef DEBUG
	printf("current_units: %u\n", current_units);
#endif // DEBUG
}
#endif // IMPULSE

ICACHE_FLASH_ATTR void meter_is_ready(void) {
	struct rst_info *rtc_info;
	rtc_info = system_get_rst_info();
	if ((rtc_info != NULL) && (rtc_info->reason != REASON_DEFAULT_RST) && (rtc_info->reason != REASON_EXT_SYS_RST)) {
		// fast boot if reset, go in sample/station mode
		fast_boot = true;
#ifdef DEBUG
		printf("fast boot\n");
#endif
		os_timer_disarm(&sample_mode_timer);
		os_timer_setfn(&sample_mode_timer, (os_timer_func_t *)sample_mode_timer_func, NULL);
#ifdef EN61107
		os_timer_arm(&sample_mode_timer, 30000, 0);
#elif defined IMPULSE
		os_timer_arm(&sample_mode_timer, 100, 0);
#else
		os_timer_arm(&sample_mode_timer, 16000, 0);
#endif
	}
	else {
#ifdef DEBUG
		fast_boot = false;
		printf("normal boot\n");
//		ext_spi_flash_erase_sector(0x0);
//		ext_spi_flash_erase_sector(0x1000);
#endif
#ifdef IMPULSE
		// start config mode at boot - dont wait for impulse based meters		
		os_timer_disarm(&config_mode_timer);
		os_timer_setfn(&config_mode_timer, (os_timer_func_t *)config_mode_timer_func, NULL);
		os_timer_arm(&config_mode_timer, 100, 0);
#elif defined EN61107
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
#ifdef IMPULSE
		os_timer_arm(&sample_mode_timer, 120000 + 100, 0);
#elif defined EN61107
		os_timer_arm(&sample_mode_timer, 120000 + 1000, 0);
#else
		os_timer_arm(&sample_mode_timer, 120000 + 1000, 0);
#endif
#else
		os_timer_arm(&sample_mode_timer, 2000, 0);
#endif
	}
}

#ifndef IMPULSE
ICACHE_FLASH_ATTR void meter_sent_data(void) {
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L];
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

			tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/status/v2/%07u/%u", en61107_get_received_serial(), get_unix_time());
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

			tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/status/v2/%07u/%u", en61107_get_received_serial(), get_unix_time());
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

			tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/status/v2/%07u/%u", kmp_get_received_serial(), get_unix_time());
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

			tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/status/v2/%07u/%u", kmp_get_received_serial(), get_unix_time());
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
#endif	// EN61107
#endif	// FLOW_METER
}
#endif	// IMPULSE

ICACHE_FLASH_ATTR void wifi_changed_cb(uint8_t status) {
	if (status == STATION_GOT_IP) {
		MQTT_Connect(&mqtt_client);
#ifdef DEBUG
		os_printf("queue size(%ld/%ld)\n", mqtt_client.msgQueue.rb.fill_cnt, mqtt_client.msgQueue.rb.size);
#endif
	}
}

#ifdef EN61107
ICACHE_FLASH_ATTR void static mqtt_connected_defer_timer_func(void *arg) {
	mqtt_connected_cb((uint32_t*)&mqtt_client);
}
#endif

ICACHE_FLASH_ATTR void mqtt_connected_cb(uint32_t *args) {
	// show led status when mqtt is connected via fallback wifi
	if (wifi_fallback_is_present()) {
		led_pattern_b();
	}
	
#ifdef EN61107
	if (en61107_get_received_serial() == 0) {
		// dont subscribe before we have a non zero serial - reschedule 60 seconds later
		os_timer_disarm(&mqtt_connected_defer_timer);
		os_timer_setfn(&mqtt_connected_defer_timer, (os_timer_func_t *)mqtt_connected_defer_timer_func, NULL);
		os_timer_arm(&mqtt_connected_defer_timer, 60000, 0);
		return;
	}
#endif

	// send initial mqtt rpc commands defered, so mqtt_tcpclient_recv() will not block for too long time
	mqtt_connected_first_mqtt_rpc_state = 0;
	os_timer_disarm(&mqtt_connected_first_mqtt_rpc_timer);
	os_timer_setfn(&mqtt_connected_first_mqtt_rpc_timer, (os_timer_func_t *)mqtt_connected_first_mqtt_rpc_timer_func, NULL);
	os_timer_arm(&mqtt_connected_first_mqtt_rpc_timer, 2000, 0);

	// set mqtt_client kmp_request should use to return data
#ifdef EN61107
	en61107_set_mqtt_client(&mqtt_client);
#elif defined IMPULSE
	//kmp_set_mqtt_client(&mqtt_client);
#else
	kmp_set_mqtt_client(&mqtt_client);
#endif
	
	// sample once outside this function...	
	os_timer_disarm(&sample_timer_first);
	os_timer_setfn(&sample_timer_first, (os_timer_func_t *)sample_timer_first_func, NULL);
	os_timer_arm(&sample_timer_first, 1100, 0);		// once after 1.1 second

	// ...and start sample timer repeatedly
	os_timer_disarm(&sample_timer);
	os_timer_setfn(&sample_timer, (os_timer_func_t *)sample_timer_func, NULL);
	os_timer_arm(&sample_timer, 60000, 1);		// every 60 seconds
}

ICACHE_FLASH_ATTR void mqtt_disconnected_cb(uint32_t *args) {
#ifdef DEBUG
	printf("mqtt_disconnected_cb\n");
#endif
	MQTT_Connect(&mqtt_client);
//	wifi_default();
}

ICACHE_FLASH_ATTR void mqtt_published_cb(uint32_t *args) {
	reset_watchdog(MQTT_WATCHDOG_ID);
}

ICACHE_FLASH_ATTR void mqtt_ping_response_cb(uint32_t *args) {
	reset_watchdog(MQTT_WATCHDOG_ID);
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
	char mqtt_message[MQTT_MESSAGE_L];

	char *str;
	char function_name[FUNCTIONNAME_L];

	uint32_t received_unix_time = 0;
	
	uint8_t i;

#ifdef DEBUG
	os_printf("topic_len: %d, data_len: %d\n", topic_len, data_len);
#endif
	if (topic_len == 0 || topic_len >= MQTT_TOPIC_L || data_len == 0 || data_len >= MQTT_MESSAGE_L) {
#ifdef DEBUG
		os_printf("data length error\n");
#endif
		return;
	}
	
	// copy and null terminate
	memset(mqtt_topic, 0, sizeof(mqtt_topic));
	if (topic_len < MQTT_TOPIC_L && topic_len > 0) {	// dont memcpy 0 bytes or if too large to fit
		memcpy(mqtt_topic, topic, topic_len);
		mqtt_topic[topic_len] = 0;
	}

	if (data_len < MQTT_MESSAGE_L && data_len > 0) {	// dont memcpy 0 bytes or if too large to fit
		memcpy(mqtt_message, data, data_len);
		mqtt_message[data_len] = 0;
	}
	
	memset(cleartext, 0, MQTT_MESSAGE_L);	// make sure its null terminated

	if (decrypt_aes_hmac_combined(cleartext, mqtt_topic, topic_len, mqtt_message, data_len) == 0) {
#ifdef DEBUG
		printf("hmac error\n");
#endif
		return;
	}
	
	// clear data
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
	
	// ..and clear for further use
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
		// found set_ssid
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
		// found reconnect
		mqtt_rpc_set_ssid_pwd(&mqtt_client, cleartext);
	}
	else if (strncmp(function_name, "reconnect", FUNCTIONNAME_L) == 0) {
		// found reconnect
		mqtt_rpc_reconnect(&mqtt_client);
	}
	else if (strncmp(function_name, "wifi_status", FUNCTIONNAME_L) == 0) {
		// found uptime
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
	else if (strncmp(function_name, "mem", FUNCTIONNAME_L) == 0) {
		// found mem
		mqtt_rpc_mem(&mqtt_client);
	}
	else if (strncmp(function_name, "profiler", FUNCTIONNAME_L) == 0) {
		// found profile
		mqtt_rpc_profiler(&mqtt_client);
	}
	else if (strncmp(function_name, "crypto", FUNCTIONNAME_L) == 0) {
		// found aes
		mqtt_rpc_crypto(&mqtt_client);
	}
	else if (strncmp(function_name, "reset_reason", FUNCTIONNAME_L) == 0) {
		// found reset_reason
		mqtt_rpc_reset_reason(&mqtt_client);
	}
	else if (strncmp(function_name, "restart", FUNCTIONNAME_L) == 0) {
		// found restart
		// stop all timers started from user_main.c
		os_timer_disarm(&sample_timer);
		os_timer_disarm(&mqtt_connected_defer_timer);
		
		MQTT_DeleteClient(&mqtt_client);
#ifndef IMPULSE
#ifdef EN61107
		en61107_request_destroy();
#else
		kmp_request_destroy();
#endif
#endif	// IMPULSE
		mqtt_rpc_restart(&mqtt_client);
	}
#ifdef DEBUG_STACK_TRACE
	else if (strncmp(function_name, "stack_trace", FUNCTIONNAME_L) == 0) {
		// found stack_trace
		mqtt_rpc_stack_trace(&mqtt_client);
	}
#endif	// DEBUG_STACK_TRACE
#ifndef IMPULSE
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
#endif
}

ICACHE_FLASH_ATTR void mqtt_send_wifi_scan_results_cb(const struct bss_info *info) {
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L];
	int mqtt_message_l;

	char ssid_escaped[SSID_ESCAPED_LENGTH + 1];
	size_t i, j;

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
	os_printf("ssid=%s&bssid=%02x:%02x:%02x:%02x:%02x:%02x&rssi=%d&channel=%d\n", 
		ssid_escaped, 
		info->bssid[0], 
		info->bssid[1], 
		info->bssid[2], 
		info->bssid[3], 
		info->bssid[4], 
		info->bssid[5], 
		info->rssi, 
		info->channel
	);
#endif

#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/scan_result/v2/%07u/%u", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/scan_result/v2/%s/%u", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/scan_result/v2/%07u/%u", kmp_get_received_serial(), get_unix_time());
#endif
	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));
	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "ssid=%s&bssid=%02x:%02x:%02x:%02x:%02x:%02x&rssi=%d&channel=%d", 
		ssid_escaped, 
		info->bssid[0], 
		info->bssid[1], 
		info->bssid[2], 
		info->bssid[3], 
		info->bssid[4], 
		info->bssid[5], 
		info->rssi, 
		info->channel
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

#ifdef IMPULSE
ICACHE_FLASH_ATTR void gpio_int_init() {
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO5_U, FUNC_GPIO5);			// Set GPIO4 function
	GPIO_DIS_OUTPUT(GPIO_ID_PIN(5));								// Set GPIO4 as input
	ETS_GPIO_INTR_DISABLE();										// Disable gpio interrupts
	ETS_GPIO_INTR_ATTACH(gpio_int_handler, NULL);
	PIN_PULLUP_EN(PERIPHS_IO_MUX_GPIO5_U);							// pull - up pin
	GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, BIT(5));				// Clear GPIO4 status
	gpio_pin_intr_state_set(GPIO_ID_PIN(5), GPIO_PIN_INTR_ANYEDGE);	// Interrupt on falling GPIO4 edge
	ETS_GPIO_INTR_ENABLE();											// Enable gpio interrupts
}
#endif

#ifdef IMPULSE
void gpio_int_handler(uint32_t interrupt_mask, void *arg) {
	uint32_t gpio_status;
	bool impulse_pin_state;
	
	uint32_t impulse_edge_to_edge_time;

	gpio_intr_ack(interrupt_mask);

	ETS_GPIO_INTR_DISABLE(); // Disable gpio interrupts
	gpio_pin_intr_state_set(GPIO_ID_PIN(5), GPIO_PIN_INTR_DISABLE);
	//wdt_feed();
	
	gpio_status = GPIO_REG_READ(GPIO_STATUS_ADDRESS);
	//clear interrupt status
	GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, gpio_status);
	
	os_delay_us(1000);	// wait 1 mS to avoid reading on slope
	impulse_pin_state = GPIO_REG_READ(GPIO_IN_ADDRESS) & BIT5;
	if (impulse_pin_state) {	// rising edge
		impulse_rising_edge_time = system_get_time();
		
		if (impulse_rising_edge_time > impulse_falling_edge_time) {
			impulse_edge_to_edge_time = impulse_rising_edge_time - impulse_falling_edge_time;
		}
		else {
			// system time wrapped
#ifdef DEBUG
		printf("wrapped\n");
#endif
			impulse_edge_to_edge_time = UINT32_MAX - impulse_falling_edge_time + impulse_rising_edge_time;
		}
		
		// check if impulse period is 100 mS...
#ifdef DEBUG
		printf("imp: %uuS\n", impulse_rising_edge_time - impulse_falling_edge_time);
#endif	// DEBUG
		if (((IMPULSE_EDGE_TO_EDGE_TIME_MIN == 0) || (IMPULSE_EDGE_TO_EDGE_TIME_MAX == 0)) || ((impulse_edge_to_edge_time > IMPULSE_EDGE_TO_EDGE_TIME_MIN * 1000) && (impulse_edge_to_edge_time < IMPULSE_EDGE_TO_EDGE_TIME_MAX * 1000))) {
			// arm the debounce timer to enable GPIO interrupt again
			sys_cfg.impulse_meter_count++;
			os_timer_disarm(&impulse_meter_calculate_timer);
			os_timer_setfn(&impulse_meter_calculate_timer, (os_timer_func_t *)impulse_meter_calculate_timer_func, NULL);
			os_timer_arm(&impulse_meter_calculate_timer, 100, 0);
			// blink led to show impulse received
			led_blink_short();	// DEBUG
		}
	}
	else {						// falling edge
		impulse_falling_edge_time = system_get_time();
	}

	// enable gpio interrupt again
	gpio_pin_intr_state_set(GPIO_ID_PIN(5), GPIO_PIN_INTR_ANYEDGE);	// Interrupt on falling GPIO4 edge
	ETS_GPIO_INTR_ENABLE();
}

ICACHE_FLASH_ATTR
void impulse_meter_init(void) {
	impulse_meter_units = atoi(sys_cfg.impulse_meter_units);
	
	impulses_per_unit = atoi(sys_cfg.impulses_per_unit);
	if (impulses_per_unit == 0) {
		impulses_per_unit = 100;		// if not set set to some default != 0
	}
	
	impulse_time = get_uptime();
	last_impulse_time = 0;
	
	last_impulse_meter_count = sys_cfg.impulse_meter_count;
#ifdef DEBUG
	printf("t: %u\n", impulse_time);
#endif // DEBUG
}
#endif // IMPULSE

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
	printf("Flash id: 0x%x%s, size: %u kB\n\r", spi_flash_get_id(), flash_size ? "" : " (unknown manufacturer)", flash_size / 1024 / 8);

#ifdef DEBUG
	printf("\t(DEBUG)\n\r");
#endif

#ifdef IMPULSE
	printf("\t(IMPULSE)\n\r");
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

#if !(defined(IMPULSE) || defined(DEBUG_NO_METER))
#ifdef THERMO_NO
	printf("\t(THERMO_NO)\n\r");
#else
	printf("\t(THERMO_NC)\n\r");
#endif
#endif	// not IMPULSE or DEBUG_NO_METER

#if !defined(IMPULSE) || !defined(DEBUG_NO_METER)
#ifdef THERMO_ON_AC_2
	printf("\t(THERMO_ON_AC_2)\n\r");
#endif
#endif	// not IMPULSE or DEBUG_NO_METER

#ifndef AUTO_CLOSE	// reversed!
	printf("\t(NO_AUTO_CLOSE)\n\r");
#endif

#ifndef DEBUG_NO_METER
#ifdef EN61107
	uart_init(BIT_RATE_300, BIT_RATE_300);
#else
	uart_init(BIT_RATE_1200, BIT_RATE_1200);
#endif
#endif	// DEBUG_NO_METER

	// clear mqtt_client
	memset(&mqtt_client, 0, sizeof(MQTT_Client));

#if !defined(DEBUG) || !defined(DEBUG_NO_METER)
	// disable serial debug
	system_set_os_print(0);
#endif
	// start watchdog
	init_watchdog();
	start_watchdog();

#ifdef IMPULSE
	ext_spi_init();
#endif
	cfg_load();

	// init crypto
	init_aes_hmac_combined(sys_cfg.key);
	
	// start kmp_request
#ifdef EN61107
	en61107_request_init();
#elif defined IMPULSE
	impulse_meter_init();
#else
	kmp_request_init();
#endif
	
	// initialize the GPIO subsystem
	gpio_init();
	// enable gpio interrupt for impulse meters
#ifdef IMPULSE
	gpio_int_init();
#else	
	user_gpio_init();
	ac_out_init();
#ifndef NO_CRON
	cron_init();
#endif	// NO_CRON
#endif // IMPULSE

	led_init();
	
#ifndef IMPULSE
	// load thermo motor state from flash(AC OUT 1)
	if (sys_cfg.ac_thermo_state) {
		ac_thermo_open();
	}
	else {
		ac_thermo_close();
	}
#endif // IMPULSE
	
	// dont enable wireless before we have configured ssid
//	wifi_set_opmode_current(NULL_MODE);
	wifi_station_disconnect();
	// disale auto connect, we handle reconnect with this event handler
	wifi_station_set_auto_connect(0);
	wifi_station_set_reconnect_policy(0);

	// do everything else in system_init_done
	system_init_done_cb(&system_init_done);
}

ICACHE_FLASH_ATTR void system_init_done(void) {
#ifdef DEBUG
	struct rst_info *rtc_info;
	rtc_info = system_get_rst_info();
	printf("rst: %d\n", (rtc_info != NULL) ? rtc_info->reason : -1);
	if (rtc_info->reason == REASON_WDT_RST || rtc_info->reason == REASON_EXCEPTION_RST || rtc_info->reason == REASON_SOFT_WDT_RST) {
		if (rtc_info->reason == REASON_EXCEPTION_RST) {
			os_printf("Fatal exception (%d):\n", rtc_info->exccause);
		}
		os_printf("epc1=0x%08x, epc2=0x%08x, epc3=0x%08x, excvaddr=0x%08x, depc=0x%08x\n", rtc_info->epc1, rtc_info->epc2, rtc_info->epc3, rtc_info->excvaddr, rtc_info->depc);	//The address of the last crash is printed, which is used to debug garbled output.
	}
#endif	// DEBUG

#ifdef IMPULSE
//	os_timer_disarm(&spi_test_timer);
//	os_timer_setfn(&spi_test_timer, (os_timer_func_t *)spi_test_timer_func, NULL);
//	os_timer_arm(&spi_test_timer, 2000, 1);
#endif	
	
	init_unix_time();

	// start config mode/sample mode in meter_is_ready() via callback
#ifdef EN61107
	en61107_register_meter_is_ready_cb(meter_is_ready);
#ifdef AUTO_CLOSE
	en61107_register_meter_sent_data_cb(meter_sent_data);
#endif	// AUTO_CLOSE
#elif defined IMPULSE
	meter_is_ready();
#else
	kmp_register_meter_is_ready_cb(meter_is_ready);
#ifdef AUTO_CLOSE
	kmp_register_meter_sent_data_cb(meter_sent_data);
#endif	// AUTO_CLOSE
#endif

	// wait 10 seconds before starting wifi and let the meter boot
	// and send serial number request
#ifdef EN61107
	os_timer_disarm(&en61107_request_send_timer);
	os_timer_setfn(&en61107_request_send_timer, (os_timer_func_t *)en61107_request_send_timer_func, NULL);
	os_timer_arm(&en61107_request_send_timer, 10000, 0);
#elif defined IMPULSE
	// do nothing here
#else
	os_timer_disarm(&kmp_request_send_timer);
	os_timer_setfn(&kmp_request_send_timer, (os_timer_func_t *)kmp_request_send_timer_func, NULL);
	os_timer_arm(&kmp_request_send_timer, 10000, 0);
#endif
}

ICACHE_FLASH_ATTR void mqtt_flash_error(uint16_t calculated_crc, uint16_t saved_crc) {
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L];
	int mqtt_message_l;	
	// vars for aes encryption
	uint8_t cleartext[MQTT_MESSAGE_L];

#ifdef DEBUG
	printf("config crc error when saving, possible flash memory error calculated_crc=0x%x&saved_crc=0x%x\n\r", calculated_crc, saved_crc);
#endif // DEBUG

	// clear data
	memset(mqtt_topic, 0, sizeof(mqtt_topic));
	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));

#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/flash_error/v2/%07u/%u", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/flash_error/v2/%s/%u", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/flash_error/v2/%07u/%u", kmp_get_received_serial(), get_unix_time());
#endif
	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "calculated_crc=0x%x&saved_crc=0x%x", calculated_crc, saved_crc);

	// encrypt and send
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);

	if (mqtt_client.pCon != NULL) {
		// if mqtt_client is initialized
		MQTT_Publish(&mqtt_client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
	}
}
