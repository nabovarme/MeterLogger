#include <esp8266.h>
#include "driver/uart.h"
#include "mqtt.h"
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

#ifdef IMPULSE

uint32_t impulse_meter_energy;
//float impulse_meter_energy;
uint32_t impulses_per_kwh;

volatile uint32_t impulse_time;
volatile uint32_t last_impulse_time;
volatile uint32_t current_energy;	// in W

volatile uint32_t last_impulse_meter_count;

uint32_t impulse_falling_edge_time;
uint32_t impulse_rising_edge_time;
#endif // ENDIF IMPULSE

MQTT_Client mqtt_client;
static os_timer_t sample_timer;
static os_timer_t config_mode_timer;
static os_timer_t sample_mode_timer;
#ifdef EN61107
static os_timer_t en61107_request_send_timer;
static os_timer_t mqtt_connected_defer_timer;
#elif defined IMPULSE
//static os_timer_t kmp_request_send_timer;
#else
static os_timer_t kmp_request_send_timer;
#endif

#ifdef IMPULSE
static os_timer_t impulse_meter_calculate_timer;
//static os_timer_t spi_test_timer;	// DEBUG
#endif

struct rst_info *rtc_info;

ICACHE_FLASH_ATTR void static sample_mode_timer_func(void *arg) {
	static ip_addr_t dns_ip;
	unsigned char topic[MQTT_TOPIC_L];
#ifdef IMPULSE
	uint32_t impulse_meter_count_temp;
#endif // IMPULSE
	
	// stop http configuration server
	httpdStop();

	// stop captive dns
	captdnsStop();

	// set dhcp dns to the one supplied from uplink
	dns_ip.addr = dns_getserver(0);
	dhcps_set_DNS(&dns_ip);

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
	MQTT_OnData(&mqtt_client, mqtt_data_cb);
	MQTT_OnTimeout(&mqtt_client, mqtt_timeout_cb);

	wifi_connect(sys_cfg.sta_ssid, sys_cfg.sta_pwd, wifi_changed_cb);

	add_watchdog(MQTT_WATCHDOG_ID, NETWORK_RESTART, MQTT_WATCHDOG_TIMEOUT);
}

ICACHE_FLASH_ATTR void static config_mode_timer_func(void *arg) {
	struct softap_config ap_conf;

	struct ip_info info;
	struct dhcps_lease dhcp_lease;
	struct netif *nif;
	ip_addr_t ap_ip;
	ip_addr_t network_addr;
	
	wifi_softap_get_config(&ap_conf);
	memset(ap_conf.ssid, 0, sizeof(ap_conf.ssid));
	memset(ap_conf.password, 0, sizeof(ap_conf.password));
#ifdef EN61107
	tfp_snprintf(ap_conf.ssid, 32, AP_SSID, en61107_get_received_serial());
#elif defined IMPULSE
	tfp_snprintf(ap_conf.ssid, 32, AP_SSID, sys_cfg.impulse_meter_serial);
#else
	tfp_snprintf(ap_conf.ssid, 32, AP_SSID, kmp_get_received_serial());
#endif
	tfp_snprintf(ap_conf.password, 64, AP_PASSWORD);
	ap_conf.authmode = STA_TYPE;
	ap_conf.ssid_len = 0;
	ap_conf.beacon_interval = 100;
	ap_conf.channel = 1;
	ap_conf.max_connection = 8;
	ap_conf.ssid_hidden = 0;

	wifi_softap_set_config(&ap_conf);

	// find the netif of the AP (that with num != 0)
	for (nif = netif_list; nif != NULL && nif->num == 0; nif = nif->next) {
		// skip
	}
	if (nif == NULL) {
		return;
	}
	// if is not 1, set it to 1. 
	// kind of a hack, but the Espressif-internals expect it like this (hardcoded 1).
	nif->num = 1;


    IP4_ADDR(&network_addr, 10, 0, 5, 0);

	wifi_softap_dhcps_stop();

	info.ip = network_addr;
	ip4_addr4(&info.ip) = 1;
	info.gw = info.ip;
	IP4_ADDR(&info.netmask, 255, 255, 255, 0);

	wifi_set_ip_info(nif->num, &info);

	dhcp_lease.start_ip = network_addr;
	ip4_addr4(&dhcp_lease.start_ip) = 2;
	dhcp_lease.end_ip = network_addr;
	ip4_addr4(&dhcp_lease.end_ip) = 254;
	wifi_softap_set_dhcps_lease(&dhcp_lease);

	wifi_softap_dhcps_start();

	captdnsInit();		// start captive dns server
	httpd_user_init();	// start web server
}

ICACHE_FLASH_ATTR void static sample_timer_func(void *arg) {
#ifdef EN61107
	en61107_request_send();
#elif defined IMPULSE
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L];
	int mqtt_message_l;
	
	uint32_t acc_energy;
	
	// for pseudo float print
	char current_energy_kwh[32];
	char acc_energy_kwh[32];
	
	uint32_t result_int, result_frac;
	unsigned char leading_zeroes[16];
	unsigned int i;
	
	// vars for aes encryption
	uint8_t cleartext[MQTT_MESSAGE_L];

	// clear data
	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));

	if (impulse_time > (uptime() - 60)) {	// only send mqtt if impulse received last minute
		acc_energy = impulse_meter_energy + (sys_cfg.impulse_meter_count * (1000 / impulses_per_kwh));
	
		// for acc_energy...
		// ...divide by 1000 and prepare decimal string in kWh
		result_int = (int32_t)(acc_energy / 1000);
		result_frac = acc_energy - result_int * 1000;
	
		// prepare decimal string
		strcpy(leading_zeroes, "");
		for (i = 0; i < (3 - decimal_number_length(result_frac)); i++) {
			strcat(leading_zeroes, "0");
		}
		tfp_snprintf(acc_energy_kwh, 32, "%u.%s%u", result_int, leading_zeroes, result_frac);

		// for current_energy...
		// ...divide by 1000 and prepare decimal string in kWh
		result_int = (int32_t)(current_energy / 1000);
		result_frac = current_energy - result_int * 1000;
		
		// prepare decimal string
		strcpy(leading_zeroes, "");
		for (i = 0; i < (3 - decimal_number_length(result_frac)); i++) {
			strcat(leading_zeroes, "0");
		}
		tfp_snprintf(current_energy_kwh, 32, "%u.%s%u", result_int, leading_zeroes, result_frac);

		tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/sample/v2/%s/%u", sys_cfg.impulse_meter_serial, get_unix_time());

		tfp_snprintf(cleartext, MQTT_MESSAGE_L, "heap=%u&effect1=%s kW&e1=%s kWh&", system_get_free_heap_size(), current_energy_kwh, acc_energy_kwh);
		
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
			reset_watchdog(MQTT_WATCHDOG_ID);
		}
	}

#else
	kmp_request_send();
#endif	// EN61107
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

	cfg_save();
	
	impulse_time = uptime();
	impulse_time_diff = impulse_time - last_impulse_time;
	
	impulse_meter_count_diff = sys_cfg.impulse_meter_count - last_impulse_meter_count;
#ifdef DEBUG
	printf("count: %u\tl count: %u\timp time: %u\tlast imp time: %u\n", sys_cfg.impulse_meter_count, last_impulse_meter_count, impulse_time, last_impulse_time);
	printf("count diff: %u\timp time diff: %u\n", impulse_meter_count_diff, impulse_time_diff);
#endif

	if (impulse_time_diff && impulse_meter_count_diff) {	// only calculate if not zero interval or zero meter count diff - should not happen
		current_energy = 3600 * (1000 / impulses_per_kwh) * impulse_meter_count_diff / impulse_time_diff;
	}

#ifdef DEBUG
	printf("current_energy: %u\n", current_energy);
#endif // DEBUG
}
#endif // IMPULSE

ICACHE_FLASH_ATTR void wifi_changed_cb(uint8_t status) {
	if (status == STATION_GOT_IP) {
		MQTT_Connect(&mqtt_client);
#ifdef DEBUG
		os_printf("queue size(%d/%d)\n", mqtt_client.msgQueue.rb.fill_cnt, mqtt_client.msgQueue.rb.size);
#endif
	}
}

#ifdef EN61107
ICACHE_FLASH_ATTR void static mqtt_connected_defer_timer_func(void *arg) {
	mqtt_connected_cb((uint32_t*)&mqtt_client);
}
#endif

ICACHE_FLASH_ATTR void mqtt_connected_cb(uint32_t *args) {
	unsigned char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L];
	uint8_t cleartext[MQTT_MESSAGE_L];
	int mqtt_message_l;

#ifdef EN61107
	if (en61107_get_received_serial() == 0) {
		// dont subscribe before we have a non zero serial - reschedule 60 seconds later
		os_timer_disarm(&mqtt_connected_defer_timer);
		os_timer_setfn(&mqtt_connected_defer_timer, (os_timer_func_t *)mqtt_connected_defer_timer_func, NULL);
		os_timer_arm(&mqtt_connected_defer_timer, 60000, 0);
		return;
	}
#endif

	// show led status when mqtt is connected via fallback wifi
	if (wifi_fallback_is_present()) {
		led_pattern_b();
	}
	
	// subscribe to /config/v2/[serial]/#
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/config/v2/%07u/#", en61107_get_received_serial());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/config/v2/%s/#", sys_cfg.impulse_meter_serial);
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/config/v2/%07u/#", kmp_get_received_serial());
#endif
	MQTT_Subscribe(&mqtt_client, mqtt_topic, 0);
	
	// send mqtt version
	// clear data
	memset(mqtt_topic, 0, sizeof(mqtt_topic));
	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/version/v2/%07u/%u", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/version/v2/%s/%u", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/version/v2/%07u/%u", kmp_get_received_serial(), get_unix_time());
#endif
	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%s-%s-%s", system_get_sdk_version(), VERSION, HW_MODEL);

	// encrypt and send
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
	MQTT_Publish(&mqtt_client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2

	// send mqtt uptime
	// clear data
	memset(mqtt_topic, 0, sizeof(mqtt_topic));
	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));
	
#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/uptime/v2/%07u/%u", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/uptime/v2/%s/%u", sys_cfg.impulse_meter_serial, get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/uptime/v2/%07u/%u", kmp_get_received_serial(), get_unix_time());
#endif
	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%u", uptime());
	// encrypt and send
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
	MQTT_Publish(&mqtt_client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2

#ifndef IMPULSE
	// send mqtt status
	// clear data
	memset(mqtt_topic, 0, sizeof(mqtt_topic));
	memset(mqtt_message, 0, sizeof(mqtt_message));
	memset(cleartext, 0, sizeof(cleartext));

#ifdef EN61107
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/status/v2/%07u/%u", en61107_get_received_serial(), get_unix_time());
#else
	tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/status/v2/%07u/%u", kmp_get_received_serial(), get_unix_time());
#endif
	tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%s", sys_cfg.ac_thermo_state ? "open" : "close");
	// encrypt and send
	mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
	MQTT_Publish(&mqtt_client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
#endif	

	// set mqtt_client kmp_request should use to return data
#ifdef EN61107
	en61107_set_mqtt_client(&mqtt_client);
#elif defined IMPULSE
	//kmp_set_mqtt_client(&mqtt_client);
#else
	kmp_set_mqtt_client(&mqtt_client);
#endif
	
	// sample once and start sample timer
	sample_timer_func(NULL);
	os_timer_disarm(&sample_timer);
	os_timer_setfn(&sample_timer, (os_timer_func_t *)sample_timer_func, NULL);
	os_timer_arm(&sample_timer, 60000, 1);		// every 60 seconds
}

ICACHE_FLASH_ATTR void mqtt_disconnected_cb(uint32_t *args) {
}

ICACHE_FLASH_ATTR void mqtt_published_cb(uint32_t *args) {
	reset_watchdog(MQTT_WATCHDOG_ID);
}

ICACHE_FLASH_ATTR void mqtt_timeout_cb(uint32_t *args) {
}
	
ICACHE_FLASH_ATTR void mqtt_data_cb(uint32_t *args, const char* topic, uint32_t topic_len, const char *data, uint32_t data_len) {
	uint8_t cleartext[MQTT_MESSAGE_L];
	char mqtt_topic[MQTT_TOPIC_L];
	char mqtt_message[MQTT_MESSAGE_L];
	int mqtt_message_l;

	char *str;
	char function_name[FUNCTIONNAME_L];

	uint32_t received_unix_time = 0;

    uint8_t i;
		
	// copy and null terminate
	memset(mqtt_topic, 0, sizeof(mqtt_topic));
	memcpy(mqtt_topic, topic, topic_len);
	mqtt_topic[topic_len] = 0;
	memcpy(mqtt_message, data, data_len);
	mqtt_message[data_len] = 0;
	
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
	// ..and clear for further use
	memset(mqtt_topic, 0, sizeof(mqtt_topic));
	
	// mqtt rpc dispatcher goes here
	if (strncmp(function_name, "ping", FUNCTIONNAME_L) == 0) {
		// found ping
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
		MQTT_Publish(&mqtt_client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
	}
	else if (strncmp(function_name, "version", FUNCTIONNAME_L) == 0) {
		// found version
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
		MQTT_Publish(&mqtt_client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
	}
	else if (strncmp(function_name, "uptime", FUNCTIONNAME_L) == 0) {
		// found uptime
#ifdef EN61107
		tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/uptime/v2/%07u/%u", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
		tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/uptime/v2/%s/%u", sys_cfg.impulse_meter_serial, get_unix_time());
#else
		tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/uptime/v2/%07u/%u", kmp_get_received_serial(), get_unix_time());
#endif
		memset(cleartext, 0, sizeof(cleartext));
		tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%u", uptime());
		// encrypt and send
		mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
		MQTT_Publish(&mqtt_client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
	}
	else if (strncmp(function_name, "rssi", FUNCTIONNAME_L) == 0) {
		// found rssi
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
		MQTT_Publish(&mqtt_client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
	}
	else if (strncmp(function_name, "ssid", FUNCTIONNAME_L) == 0) {
		// found uptime
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
		MQTT_Publish(&mqtt_client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
	}
	else if (strncmp(function_name, "set_ssid", FUNCTIONNAME_L) == 0) {
		if ((received_unix_time > (get_unix_time() - 1800)) && (received_unix_time < (get_unix_time() + 1800))) {
			// replay attack countermeasure - 1 hour time window

			// change sta_ssid and save if different
			if (strncmp(sys_cfg.sta_ssid, cleartext, 32 - 1) != 0) {
				memset(sys_cfg.sta_ssid, 0, sizeof(sys_cfg.sta_ssid));
				strncpy(sys_cfg.sta_ssid, cleartext, 32 - 1);
				cfg_save();
			}
		}
	}
	else if (strncmp(function_name, "set_pwd", FUNCTIONNAME_L) == 0) {
		if ((received_unix_time > (get_unix_time() - 1800)) && (received_unix_time < (get_unix_time() + 1800))) {
			// replay attack countermeasure - 1 hour time window

			// change sta_pwd and save if different
			if (strncmp(sys_cfg.sta_pwd, cleartext, 64 - 1) != 0) {
				memset(sys_cfg.sta_pwd, 0, sizeof(sys_cfg.sta_pwd));
				strncpy(sys_cfg.sta_pwd, cleartext, 64 - 1);
				cfg_save();
			}
		}
	}
	else if (strncmp(function_name, "wifi_status", FUNCTIONNAME_L) == 0) {
		// found uptime
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
		MQTT_Publish(&mqtt_client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
	}
	else if (strncmp(function_name, "mem", FUNCTIONNAME_L) == 0) {
		// found mem
#ifdef EN61107
		tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/mem/v2/%07u/%u", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
		tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/mem/v2/%s/%u", sys_cfg.impulse_meter_serial, get_unix_time());
#else
		tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/mem/v2/%07u/%u", kmp_get_received_serial(), get_unix_time());
#endif
		tfp_snprintf(cleartext, MQTT_MESSAGE_L, "heap=%u&", system_get_free_heap_size());
		memset(cleartext, 0, sizeof(cleartext));
		// encrypt and send
		mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
#ifdef DEBUG
		system_print_meminfo();
#endif
		MQTT_Publish(&mqtt_client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
	}
	else if (strncmp(function_name, "crypto", FUNCTIONNAME_L) == 0) {
		// found aes
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
		MQTT_Publish(&mqtt_client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
	}
	else if (strncmp(function_name, "reset_reason", FUNCTIONNAME_L) == 0) {
		// found mem
#ifdef EN61107
		tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/reset_reason/v2/%07u/%u", en61107_get_received_serial(), get_unix_time());
#elif defined IMPULSE
		tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/reset_reason/v2/%s/%u", sys_cfg.impulse_meter_serial, get_unix_time());
#else
		tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/reset_reason/v2/%07u/%u", kmp_get_received_serial(), get_unix_time());
#endif
		memset(cleartext, 0, sizeof(cleartext));
		tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%d", (rtc_info != NULL) ? rtc_info->reason : -1);
		// encrypt and send
		mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
		MQTT_Publish(&mqtt_client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
	}
#ifndef IMPULSE
	else if (strncmp(function_name, "set_cron", FUNCTIONNAME_L) == 0) {
		// found set_cron
		if ((received_unix_time > (get_unix_time() - 1800)) && (received_unix_time < (get_unix_time() + 1800))) {
			// replay attack countermeasure - 1 hour time window
			add_cron_job_from_query(cleartext);
		}
	}
	else if (strncmp(function_name, "clear_cron", FUNCTIONNAME_L) == 0) {
		// found clear_cron
		if ((received_unix_time > (get_unix_time() - 1800)) && (received_unix_time < (get_unix_time() + 1800))) {
			// replay attack countermeasure - 1 hour time window
			clear_cron_jobs();
		}
	}
	else if (strncmp(function_name, "cron", FUNCTIONNAME_L) == 0) {
		// found cron
#ifdef EN61107
		tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/cron/v2/%07u/%u", en61107_get_received_serial(), get_unix_time());
#else
		tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/cron/v2/%07u/%u", kmp_get_received_serial(), get_unix_time());
#endif
		memset(cleartext, 0, sizeof(cleartext));
		tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%d", sys_cfg.cron_jobs.n);
		// encrypt and send
		mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
		MQTT_Publish(&mqtt_client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
	}
	else if (strncmp(function_name, "open", FUNCTIONNAME_L) == 0) {
		// found open
		if ((received_unix_time > (get_unix_time() - 1800)) && (received_unix_time < (get_unix_time() + 1800))) {
			// replay attack countermeasure - 1 hour time window
			//ac_motor_valve_open();
			sys_cfg.ac_thermo_state = 1;
			ac_thermo_open();
		}
	}
	else if (strncmp(function_name, "close", FUNCTIONNAME_L) == 0) {
		// found close
		if ((received_unix_time > (get_unix_time() - 1800)) && (received_unix_time < (get_unix_time() + 1800))) {
			//ac_motor_valve_close();
			sys_cfg.ac_thermo_state = 0;
			ac_thermo_close();
		}
	}
	else if (strncmp(function_name, "status", FUNCTIONNAME_L) == 0) {
		// found status
#ifdef EN61107
		tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/status/v2/%07u/%u", en61107_get_received_serial(), get_unix_time());
#else
		tfp_snprintf(mqtt_topic, MQTT_TOPIC_L, "/status/v2/%07u/%u", kmp_get_received_serial(), get_unix_time());
#endif
		memset(cleartext, 0, sizeof(cleartext));
		tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%s", sys_cfg.ac_thermo_state ? "open" : "close");
		// encrypt and send
		mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, mqtt_topic, strlen(mqtt_topic), cleartext, strlen(cleartext) + 1);
		MQTT_Publish(&mqtt_client, mqtt_topic, mqtt_message, mqtt_message_l, 2, 0);	// QoS level 2
	}
	else if (strncmp(function_name, "off", FUNCTIONNAME_L) == 0) {
		// found off
		// turn ac output off
		ac_off();
	}
	else if (strncmp(function_name, "pwm", FUNCTIONNAME_L) == 0) {
		// found pwm
		// start ac 1 pwm
		ac_thermo_pwm(atoi(data));
	}
	else if (strncmp(function_name, "test", FUNCTIONNAME_L) == 0) {
		// found test
		ac_test();
	}
#endif
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
		if ((impulse_edge_to_edge_time > 80 * 1000) && (impulse_edge_to_edge_time < 120 * 1000)) {
			// arm the debounce timer to enable GPIO interrupt again
			sys_cfg.impulse_meter_count++;
			os_timer_disarm(&impulse_meter_calculate_timer);
			os_timer_setfn(&impulse_meter_calculate_timer, (os_timer_func_t *)impulse_meter_calculate_timer_func, NULL);
			os_timer_arm(&impulse_meter_calculate_timer, 100, 0);
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
	impulse_meter_energy = atoi(sys_cfg.impulse_meter_energy);
	
	impulses_per_kwh = atoi(sys_cfg.impulses_per_kwh);
	if (impulses_per_kwh == 0) {
		impulses_per_kwh = 100;		// if not set set to some default != 0
	}
	
	impulse_time = uptime();
	last_impulse_time = 0;
	
	last_impulse_meter_count = sys_cfg.impulse_meter_count;
#ifdef DEBUG
	printf("t: %u\n", impulse_time);
#endif // DEBUG
}
#endif // IMPULSE

ICACHE_FLASH_ATTR void user_init(void) {
	uart_init(BIT_RATE_115200, BIT_RATE_115200);

	printf("\n\r");
	printf("SDK version: %s\n\r", system_get_sdk_version());
	printf("Software version: %s\n\r", VERSION);
	printf("Hardware model: %s\n\r", HW_MODEL);

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

#ifdef THERMO_NO
	printf("\t(THERMO_NO)\n\r");
#else
	printf("\t(THERMO_NC)\n\r");
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
	ac_out_init();
	cron_init();
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
	
	// make sure the device is in AP and STA combined mode
	wifi_set_opmode_current(STATIONAP_MODE);
	// do everything else in system_init_done
	system_init_done_cb(&system_init_done);
}

ICACHE_FLASH_ATTR void system_init_done(void) {
	rtc_info = system_get_rst_info();
#ifdef DEBUG
	printf("rst: %d\n", (rtc_info != NULL) ? rtc_info->reason : -1);
#endif	// DEBUG

#ifdef IMPULSE
//	os_timer_disarm(&spi_test_timer);
//	os_timer_setfn(&spi_test_timer, (os_timer_func_t *)spi_test_timer_func, NULL);
//	os_timer_arm(&spi_test_timer, 2000, 1);
#endif	
	
	init_unix_time();

	// wait 10 seconds before starting wifi and let the meter boot
	// and send serial number request
#ifdef EN61107
	os_timer_disarm(&en61107_request_send_timer);
	os_timer_setfn(&en61107_request_send_timer, (os_timer_func_t *)en61107_request_send_timer_func, NULL);
	os_timer_arm(&en61107_request_send_timer, 1000, 0);
#elif defined IMPULSE
	// do nothing here
#else
	os_timer_disarm(&kmp_request_send_timer);
	os_timer_setfn(&kmp_request_send_timer, (os_timer_func_t *)kmp_request_send_timer_func, NULL);
	os_timer_arm(&kmp_request_send_timer, 10000, 0);
#endif

	if ((rtc_info != NULL) && (rtc_info->reason != REASON_DEFAULT_RST) && (rtc_info->reason != REASON_EXT_SYS_RST)) {
		// fast boot if reset, go in sample/station mode
#ifdef DEBUG
		printf("fast boot\n");
#endif
		os_timer_disarm(&sample_mode_timer);
		os_timer_setfn(&sample_mode_timer, (os_timer_func_t *)sample_mode_timer_func, NULL);
#ifdef EN61107
		os_timer_arm(&sample_mode_timer, 12000, 0);
#elif defined IMPULSE
		os_timer_arm(&sample_mode_timer, 100, 0);
#else
		os_timer_arm(&sample_mode_timer, 12000, 0);
#endif
	}
	else {
#ifdef DEBUG
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
		// start waiting for serial number after 30 seconds
		// and start ap mode
		os_timer_disarm(&config_mode_timer);
		os_timer_setfn(&config_mode_timer, (os_timer_func_t *)config_mode_timer_func, NULL);
		os_timer_arm(&config_mode_timer, 30000, 0);
#else
		// start waiting for serial number after 16 seconds
		// and start ap mode
		os_timer_disarm(&config_mode_timer);
		os_timer_setfn(&config_mode_timer, (os_timer_func_t *)config_mode_timer_func, NULL);
		os_timer_arm(&config_mode_timer, 16000, 0);
#endif

		// wait for 120 seconds from boot and go to station mode
		os_timer_disarm(&sample_mode_timer);
		os_timer_setfn(&sample_mode_timer, (os_timer_func_t *)sample_mode_timer_func, NULL);
#ifndef DEBUG_SHORT_WEB_CONFIG_TIME
#ifdef IMPULSE
		os_timer_arm(&sample_mode_timer, 120000 + 100, 0);
#elif defined EN61107
		os_timer_arm(&sample_mode_timer, 120000 + 30000, 0);
#else
		os_timer_arm(&sample_mode_timer, 120000 + 16000, 0);
#endif
#else
		os_timer_arm(&sample_mode_timer, 22000, 0);
#endif
	}
}

