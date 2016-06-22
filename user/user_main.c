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
#include "driver/gpio16.h"
#include "driver/ext_spi_flash.h"

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

MQTT_Client mqttClient;
static os_timer_t sample_timer;
static os_timer_t config_mode_timer;
static os_timer_t sample_mode_timer;
#ifdef EN61107
static os_timer_t en61107_request_send_timer;
#elif defined IMPULSE
//static os_timer_t kmp_request_send_timer;
#else
static os_timer_t kmp_request_send_timer;
#endif

#ifdef IMPULSE
static os_timer_t impulse_meter_calculate_timer;
static os_timer_t ext_wd_timer;
//static os_timer_t spi_test_timer;	// DEBUG
#endif

uint16_t counter = 0;

struct rst_info *rtc_info;

ICACHE_FLASH_ATTR void static sample_mode_timer_func(void *arg) {
	unsigned char topic[MQTT_TOPIC_L];
#ifdef IMPULSE
	uint32_t impulse_meter_count_temp;
#endif // IMPULSE
	
	// stop http configuration server
	httpdStop();

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
	
	memset(&mqttClient, 0, sizeof(MQTT_Client));
	MQTT_InitConnection(&mqttClient, sys_cfg.mqtt_host, sys_cfg.mqtt_port, sys_cfg.security);

	MQTT_InitClient(&mqttClient, sys_cfg.device_id, sys_cfg.mqtt_user, sys_cfg.mqtt_pass, sys_cfg.mqtt_keepalive, 1);

	// set MQTT LWP topic
#ifdef IMPULSE
	tfp_snprintf(topic, MQTT_TOPIC_L, "/offline/v1/%s", sys_cfg.impulse_meter_serial);
#else
	tfp_snprintf(topic, MQTT_TOPIC_L, "/offline/v1/%07u", kmp_get_received_serial());
#endif
	MQTT_InitLWT(&mqttClient, topic, "", 0, 0);
	
	MQTT_OnConnected(&mqttClient, mqttConnectedCb);
	MQTT_OnDisconnected(&mqttClient, mqttDisconnectedCb);
	MQTT_OnPublished(&mqttClient, mqttPublishedCb);
	MQTT_OnData(&mqttClient, mqttDataCb);

	wifi_set_opmode_current(STATION_MODE);
	wifi_connect(sys_cfg.sta_ssid, sys_cfg.sta_pwd, wifi_changed_cb);
}

ICACHE_FLASH_ATTR void static config_mode_timer_func(void *arg) {
	struct softap_config ap_conf;
	
	INFO("\r\nAP mode\r\n");
	
	wifi_softap_get_config(&ap_conf);
	memset(ap_conf.ssid, 0, sizeof(ap_conf.ssid));
	memset(ap_conf.password, 0, sizeof(ap_conf.password));
#ifdef IMPULSE
	tfp_snprintf(ap_conf.ssid, 32, AP_SSID, sys_cfg.impulse_meter_serial);
#else
	tfp_snprintf(ap_conf.ssid, 32, AP_SSID, kmp_get_received_serial());
#endif
	tfp_snprintf(ap_conf.password, 64, AP_PASSWORD);
	ap_conf.authmode = STA_TYPE;
	ap_conf.ssid_len = 0;
	ap_conf.beacon_interval = 100;
	ap_conf.channel = 1;
	ap_conf.max_connection = 4;
	ap_conf.ssid_hidden = 0;

	wifi_softap_set_config_current(&ap_conf);

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
	_align_32_bit uint8_t cleartext[MQTT_MESSAGE_L];	// is casted in crypto lib

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

		if (mqttClient.pCon != NULL) {
			// if mqtt_client is initialized
			MQTT_Publish(&mqttClient, mqtt_topic, mqtt_message, mqtt_message_l, 0, 0);
		}

		// set offset for next calculation
		last_impulse_meter_count = sys_cfg.impulse_meter_count;
		last_impulse_time = impulse_time;
#ifdef DEBUG
		printf("current_energy: %u\n", current_energy);
#endif
	}
	else {
		// send ping to keep mqtt alive
		if (mqttClient.pCon != NULL) {
			// if mqtt_client is initialized
			MQTT_Ping(&mqttClient);
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

#ifdef IMPULSE
ICACHE_FLASH_ATTR void static ext_wd_timer_func(void *arg) {
	if (gpio16_input_get()) {
		gpio16_output_set(0);
	}
	else {
		gpio16_output_set(1);
	}
}
#endif // IMPULSE

ICACHE_FLASH_ATTR void wifi_changed_cb(uint8_t status) {
	if (status == STATION_GOT_IP) {
		MQTT_Connect(&mqttClient);
	}
}

ICACHE_FLASH_ATTR void mqttConnectedCb(uint32_t *args) {
	MQTT_Client *client = (MQTT_Client*)args;
	unsigned char topic[MQTT_TOPIC_L];

#ifdef DEBUG
	printf("\n\rMQTT: Connected\n\r");
#endif

	// set MQTT LWP topic and subscribe to /config/v1/serial/#
#ifdef IMPULSE
	tfp_snprintf(topic, MQTT_TOPIC_L, "/config/v1/%s/#", sys_cfg.impulse_meter_serial);
#else
	tfp_snprintf(topic, MQTT_TOPIC_L, "/config/v1/%07u/#", kmp_get_received_serial());
#endif
	MQTT_Subscribe(client, topic, 0);

	// set mqtt_client kmp_request should use to return data
#ifdef EN61107
	en61107_set_mqtt_client(client);
#elif defined IMPULSE
	//kmp_set_mqtt_client(client);
#else
	kmp_set_mqtt_client(client);
#endif
	
	// sample once and start sample timer
	sample_timer_func(NULL);
	os_timer_disarm(&sample_timer);
	os_timer_setfn(&sample_timer, (os_timer_func_t *)sample_timer_func, NULL);
	os_timer_arm(&sample_timer, 60000, 1);		// every 60 seconds
}

ICACHE_FLASH_ATTR void mqttDisconnectedCb(uint32_t *args) {
	MQTT_Client *client = (MQTT_Client*)args;
	INFO("MQTT: Disconnected - reconnect\r\n");
	MQTT_Connect(client);
}

ICACHE_FLASH_ATTR void mqttPublishedCb(uint32_t *args) {
	//MQTT_Client *client = (MQTT_Client*)args;
	INFO("MQTT: Published\r\n");
}

ICACHE_FLASH_ATTR void mqttDataCb(uint32_t *args, const char* topic, uint32_t topic_len, const char *data, uint32_t data_len) {
	char *topicBuf = (char*)os_zalloc(topic_len + 1);	// DEBUG: could we avoid malloc here?
	char *dataBuf = (char*)os_zalloc(data_len + 1);
	MQTT_Client *client = (MQTT_Client*)args;
	
	char *str;
	char function_name[FUNCTIONNAME_L];

	_align_32_bit unsigned char reply_topic[MQTT_TOPIC_L];
	unsigned char reply_message[MQTT_MESSAGE_L];
	int reply_message_l;
	
	// vars for aes encryption
	_align_32_bit uint8_t cleartext[MQTT_MESSAGE_L];	// is casted in crypto lib
	uint8_t i;

	memcpy(topicBuf, topic, topic_len);
	topicBuf[topic_len] = 0;

	memcpy(dataBuf, data, data_len);
	dataBuf[data_len] = 0;
	
	// clear data
	memset(reply_message, 0, sizeof(reply_message));
	memset(cleartext, 0, sizeof(cleartext));
	
	// check v2 aes decrypted messages is same as topic
	

	// parse mqtt topic for function call name
	str = strtok(topicBuf, "/");
	while (str != NULL) {
		strncpy(function_name, str, FUNCTIONNAME_L);   // save last parameter as function_name
		str = strtok(NULL, "/");
	}
	
	// mqtt rpc dispatcher goes here
	if (strncmp(function_name, "ping", FUNCTIONNAME_L) == 0) {
		// found ping
#ifdef IMPULSE
		tfp_snprintf(reply_topic, MQTT_TOPIC_L, "/ping/v2/%s/%u", sys_cfg.impulse_meter_serial, get_unix_time());
#else
		tfp_snprintf(reply_topic, MQTT_TOPIC_L, "/ping/v2/%07u/%u", kmp_get_received_serial(), get_unix_time());
#endif
		// empty message
		strcpy(cleartext, "");
		// encrypt and send
		reply_message_l = encrypt_aes_hmac_combined(reply_message, reply_topic, strlen(reply_topic), cleartext, strlen(cleartext) + 1);
		MQTT_Publish(client, reply_topic, reply_message, reply_message_l, 0, 0);
	}
	else if (strncmp(function_name, "version", FUNCTIONNAME_L) == 0) {
		// found version
#ifdef IMPULSE
		tfp_snprintf(reply_topic, MQTT_TOPIC_L, "/version/v2/%s/%u", sys_cfg.impulse_meter_serial, get_unix_time());
#else
		tfp_snprintf(reply_topic, MQTT_TOPIC_L, "/version/v2/%07u/%u", kmp_get_received_serial(), get_unix_time());
#endif
#ifdef IMPULSE
		tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%s-%s", system_get_sdk_version(), VERSION);
#else
#	ifdef THERMO_NO
		tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%s-%s-THERMO_NO", system_get_sdk_version(), VERSION);
#	else	// THERMO_NC
		tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%s-%s-THERMO_NC", system_get_sdk_version(), VERSION);
#	endif
#endif
		// encrypt and send
		reply_message_l = encrypt_aes_hmac_combined(reply_message, reply_topic, strlen(reply_topic), cleartext, strlen(cleartext) + 1);
		MQTT_Publish(client, reply_topic, reply_message, reply_message_l, 0, 0);
	}
	else if (strncmp(function_name, "uptime", FUNCTIONNAME_L) == 0) {
		// found uptime
#ifdef IMPULSE
		tfp_snprintf(reply_topic, MQTT_TOPIC_L, "/uptime/v2/%s/%u", sys_cfg.impulse_meter_serial, get_unix_time());
#else
		tfp_snprintf(reply_topic, MQTT_TOPIC_L, "/uptime/v2/%07u/%u", kmp_get_received_serial(), get_unix_time());
#endif
		tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%u", uptime());
		// encrypt and send
		reply_message_l = encrypt_aes_hmac_combined(reply_message, reply_topic, strlen(reply_topic), cleartext, strlen(cleartext) + 1);
		MQTT_Publish(client, reply_topic, reply_message, reply_message_l, 0, 0);
	}
	else if (strncmp(function_name, "mem", FUNCTIONNAME_L) == 0) {
		// found mem
#ifdef IMPULSE
		tfp_snprintf(reply_topic, MQTT_TOPIC_L, "/mem/v2/%s/%u", sys_cfg.impulse_meter_serial, get_unix_time());
#else
		tfp_snprintf(reply_topic, MQTT_TOPIC_L, "/mem/v2/%07u/%u", kmp_get_received_serial(), get_unix_time());
#endif
		tfp_snprintf(cleartext, MQTT_MESSAGE_L, "heap=%u&", system_get_free_heap_size());
		// encrypt and send
		reply_message_l = encrypt_aes_hmac_combined(reply_message, reply_topic, strlen(reply_topic), cleartext, strlen(cleartext) + 1);
#ifdef DEBUG
		system_print_meminfo();
#endif
		MQTT_Publish(client, reply_topic, reply_message, reply_message_l, 0, 0);
	}
	else if (strncmp(function_name, "crypto", FUNCTIONNAME_L) == 0) {
		// found aes
#ifdef IMPULSE
		tfp_snprintf(reply_topic, MQTT_TOPIC_L, "/crypto/v2/%s/%u", sys_cfg.impulse_meter_serial, get_unix_time());
#else
		tfp_snprintf(reply_topic, MQTT_TOPIC_L, "/crypto/v2/%07u/%u", kmp_get_received_serial(), get_unix_time());
#endif
#ifdef IMPULSE
		tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%s-%s", system_get_sdk_version(), VERSION);
#else
#	ifdef THERMO_NO
		tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%s-%s-THERMO_NO", system_get_sdk_version(), VERSION);
#	else	// THERMO_NC
		tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%s-%s-THERMO_NC", system_get_sdk_version(), VERSION);
#	endif
#endif
		// encrypt and send
		reply_message_l = encrypt_aes_hmac_combined(reply_message, reply_topic, strlen(reply_topic), cleartext, strlen(cleartext) + 1);
		MQTT_Publish(client, reply_topic, reply_message, reply_message_l, 0, 0);
	}
	else if (strncmp(function_name, "reset_reason", FUNCTIONNAME_L) == 0) {
		// found mem
#ifdef IMPULSE
		tfp_snprintf(reply_topic, MQTT_TOPIC_L, "/reset_reason/v2/%s/%u", sys_cfg.impulse_meter_serial, get_unix_time());
#else
		tfp_snprintf(reply_topic, MQTT_TOPIC_L, "/reset_reason/v2/%07u/%u", kmp_get_received_serial(), get_unix_time());
#endif
		tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%d", (rtc_info != NULL) ? rtc_info->reason : -1);
		// encrypt and send
		reply_message_l = encrypt_aes_hmac_combined(reply_message, reply_topic, strlen(reply_topic), cleartext, strlen(cleartext) + 1);
		MQTT_Publish(client, reply_topic, reply_message, reply_message_l, 0, 0);
	}
#ifndef IMPULSE
	else if (strncmp(function_name, "set_cron", FUNCTIONNAME_L) == 0) {
		// found set_cron
		add_cron_job_from_query(dataBuf);
	}
	else if (strncmp(function_name, "clear_cron", FUNCTIONNAME_L) == 0) {
		// found clear_cron
		clear_cron_jobs();
	}
	else if (strncmp(function_name, "cron", FUNCTIONNAME_L) == 0) {
		// found cron
		tfp_snprintf(reply_topic, MQTT_TOPIC_L, "/cron/v2/%07u/%u", kmp_get_received_serial(), get_unix_time());
		tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%d", sys_cfg.cron_jobs.n);
		// encrypt and send
		reply_message_l = encrypt_aes_hmac_combined(reply_message, reply_topic, strlen(reply_topic), cleartext, strlen(cleartext) + 1);
		MQTT_Publish(client, reply_topic, reply_message, reply_message_l, 0, 0);
	}
	else if (strncmp(function_name, "open", FUNCTIONNAME_L) == 0) {
		// found open
		//ac_motor_valve_open();
		sys_cfg.ac_thermo_state = 1;
		ac_thermo_open();
	}
	else if (strncmp(function_name, "close", FUNCTIONNAME_L) == 0) {
		// found close
		//ac_motor_valve_close();
		sys_cfg.ac_thermo_state = 0;
		ac_thermo_close();
	}
	else if (strncmp(function_name, "status", FUNCTIONNAME_L) == 0) {
		// found status
		tfp_snprintf(reply_topic, MQTT_TOPIC_L, "/status/v2/%07u/%u", kmp_get_received_serial(), get_unix_time());
		tfp_snprintf(cleartext, MQTT_MESSAGE_L, "%s", sys_cfg.ac_thermo_state ? "open" : "close");
		// encrypt and send
		reply_message_l = encrypt_aes_hmac_combined(reply_message, reply_topic, strlen(reply_topic), cleartext, strlen(cleartext) + 1);
		MQTT_Publish(client, reply_topic, reply_message, reply_message_l, 0, 0);
	}
	else if (strncmp(function_name, "off", FUNCTIONNAME_L) == 0) {
		// found off
		// turn ac output off
		ac_off();
	}
	else if (strncmp(function_name, "pwm", FUNCTIONNAME_L) == 0) {
		// found pwm
		// start ac 1 pwm
		ac_thermo_pwm(atoi(dataBuf));
	}
	else if (strncmp(function_name, "test", FUNCTIONNAME_L) == 0) {
		// found test
		ac_test();
	}
#endif
	
	os_free(topicBuf);
	os_free(dataBuf);
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

	// start extern watchdog timer	(MCP1316)
	os_timer_disarm(&ext_wd_timer);
	os_timer_setfn(&ext_wd_timer, (os_timer_func_t *)ext_wd_timer_func, NULL);
	os_timer_arm(&ext_wd_timer, 1000, 1);
	//Set GPIO16 to output mode
	gpio16_output_conf();
	gpio16_output_set(1);
#ifdef DEBUG
	printf("t: %u\n", impulse_time);
#endif // DEBUG
}
#endif // IMPULSE

ICACHE_FLASH_ATTR void user_init(void) {
	uart_init(BIT_RATE_1200, BIT_RATE_1200);
	printf("\n\r");
	printf("SDK version: %s\n\r", system_get_sdk_version());
	printf("Software version: %s\n\r", VERSION);
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

#ifndef DEBUG
	// disable serial debug
	system_set_os_print(0);
#endif

#ifdef IMPULSE
	ext_spi_init();
#endif
	cfg_load();

	// init crypto
	init_aes_hmac_combined(sys_cfg.key, 16);
	
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
	os_timer_arm(&en61107_request_send_timer, 10000, 0);
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
		os_timer_arm(&sample_mode_timer, 120000, 0);
#else
		os_timer_arm(&sample_mode_timer, 18000, 0);
#endif
	}
		
	INFO("\r\nSystem started ...\r\n");
}

