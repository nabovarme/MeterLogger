#include <esp8266.h>
#include "driver/uart.h"
#include "mqtt.h"
#include "wifi.h"
#include "config.h"
#include "debug.h"
#include "httpd_user_init.h"
#include "user_config.h"
#include "unix_time.h"
#include "user_main.h"
#include "kmp_request.h"
#include "en61107_request.h"
#include "cron.h"
#include "led.h"
#include "ac_out.h"

extern unsigned int kmp_serial;
#ifdef IMPULSE
unsigned int impulse_meter_serial;
volatile uint32_t impulse_meter_count;
volatile uint32_t last_impulse_meter_count;
#endif

MQTT_Client mqttClient;
static os_timer_t sample_timer;
static os_timer_t config_mode_timer;
static os_timer_t sample_mode_timer;
static os_timer_t en61107_request_send_timer;
#ifdef EN61107
static os_timer_t en61107_request_send_timer;
#elif defined IMPULSE
static os_timer_t kmp_request_send_timer;
#else
static os_timer_t kmp_request_send_timer;
#endif

#ifdef IMPULSE
static os_timer_t debounce_timer;
#endif

uint16 counter = 0;

ICACHE_FLASH_ATTR void sample_mode_timer_func(void *arg) {
	unsigned char topic[128];
	int topic_l;
	
	// stop http configuration server
	httpdStop();
	
	MQTT_InitConnection(&mqttClient, sys_cfg.mqtt_host, sys_cfg.mqtt_port, sys_cfg.security);

	MQTT_InitClient(&mqttClient, sys_cfg.device_id, sys_cfg.mqtt_user, sys_cfg.mqtt_pass, sys_cfg.mqtt_keepalive, 1);

	// set MQTT LWP topic
	topic_l = os_sprintf(topic, "/offline/v1/%u", kmp_get_received_serial());
	MQTT_InitLWT(&mqttClient, topic, "", 0, 0);
	
	MQTT_OnConnected(&mqttClient, mqttConnectedCb);
	MQTT_OnDisconnected(&mqttClient, mqttDisconnectedCb);
	MQTT_OnPublished(&mqttClient, mqttPublishedCb);
	MQTT_OnData(&mqttClient, mqttDataCb);

	wifi_connect(sys_cfg.sta_ssid, sys_cfg.sta_pwd, wifiConnectCb);
}

ICACHE_FLASH_ATTR void config_mode_timer_func(void *arg) {
	struct softap_config ap_conf;
	
	INFO("\r\nAP mode\r\n");
	
	wifi_softap_get_config(&ap_conf);
	os_memset(ap_conf.ssid, 0, sizeof(ap_conf.ssid));
	os_memset(ap_conf.password, 0, sizeof(ap_conf.password));
	os_sprintf(ap_conf.ssid, AP_SSID, kmp_serial);
	os_sprintf(ap_conf.password, AP_PASSWORD);
	ap_conf.authmode = STA_TYPE;
	ap_conf.ssid_len = 0;
	ap_conf.beacon_interval = 100;
	ap_conf.channel = 1;
	ap_conf.max_connection = 4;
	ap_conf.ssid_hidden = 0;

	wifi_softap_set_config_current(&ap_conf);

	httpd_user_init();
}

ICACHE_FLASH_ATTR void sample_timer_func(void *arg) {
#ifdef EN61107
	en61107_request_send();
#elif defined IMPULSE
	unsigned char mqtt_topic[128];
	unsigned char mqtt_message[8];
	int mqtt_topic_l;
	int mqtt_message_l;
	
	int current_energy;
	
	impulse_meter_serial = 9999;
	
	current_energy = (impulse_meter_count - last_impulse_meter_count) * 10 * 60;
	last_impulse_meter_count = impulse_meter_count;

	mqtt_topic_l = os_sprintf(mqtt_topic, "/sample/v1/%u/%u", impulse_meter_serial, get_unix_time());
	mqtt_message_l = os_sprintf(mqtt_message, "heap=%lu&effect1=%u W&e1=%lu kWh&", system_get_free_heap_size(), current_energy, impulse_meter_count * 10);

	if (&mqttClient) {
		// if mqtt_client is initialized
		MQTT_Publish(&mqttClient, mqtt_topic, mqtt_message, mqtt_message_l, 0, 0);
	}
#else
	kmp_request_send();
#endif
}

ICACHE_FLASH_ATTR void kmp_request_send_timer_func(void *arg) {
	kmp_request_send();
}

ICACHE_FLASH_ATTR void en61107_request_send_timer_func(void *arg) {
	en61107_request_send();
}

#ifdef IMPULSE
ICACHE_FLASH_ATTR void debounce_timer_func(void *arg) {
	gpio_pin_intr_state_set(GPIO_ID_PIN(0), GPIO_PIN_INTR_POSEDGE);	// Interrupt on falling GPIO0 edge
	ETS_GPIO_INTR_ENABLE();		// Enable gpio interrupts
	os_printf("\n\rimpulse_meter_count: %lu\n\r", impulse_meter_count);
}
#endif

ICACHE_FLASH_ATTR void wifiConnectCb(uint8_t status) {
	if(status == STATION_GOT_IP){ 
		init_unix_time();
		MQTT_Connect(&mqttClient);
	} else {
		MQTT_Disconnect(&mqttClient);
	}
}

ICACHE_FLASH_ATTR void mqttConnectedCb(uint32_t *args) {
	MQTT_Client* client = (MQTT_Client*)args;
	unsigned char topic[128];
	int topic_l;

#ifdef DEBUG
	os_printf("\n\rMQTT: Connected\n\r");
#endif

	// set MQTT LWP topic and subscribe to /config/v1/serial/#
	topic_l = os_sprintf(topic, "/config/v1/%u/#", kmp_get_received_serial());
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
	MQTT_Client* client = (MQTT_Client*)args;
	INFO("MQTT: Disconnected\r\n");
}

ICACHE_FLASH_ATTR void mqttPublishedCb(uint32_t *args) {
	MQTT_Client* client = (MQTT_Client*)args;
	INFO("MQTT: Published\r\n");
}

ICACHE_FLASH_ATTR void mqttDataCb(uint32_t *args, const char* topic, uint32_t topic_len, const char *data, uint32_t data_len) {
//	uint32_t t1, t2;
//	t1 = system_get_rtc_time();

	char *topicBuf = (char*)os_zalloc(topic_len + 1);	// DEBUG: could we avoid malloc here?
	char *dataBuf = (char*)os_zalloc(data_len + 1);
	MQTT_Client* client = (MQTT_Client*)args;
	
	char *str;
	char function_name[FUNCTIONNAME_L];

	unsigned char reply_topic[128];
	unsigned char reply_message[8];
	int reply_topic_l;
	int reply_message_l;

	os_memcpy(topicBuf, topic, topic_len);
	topicBuf[topic_len] = 0;

	os_memcpy(dataBuf, data, data_len);
	dataBuf[data_len] = 0;

#ifdef DEBUG
	//os_printf("\n\rReceive topic: %s, data: %s \n\r", topicBuf, dataBuf);
#endif

	// parse mqtt topic for function call name
	str = strtok(topicBuf, "/");
	while (str != NULL) {
		strncpy(function_name, str, FUNCTIONNAME_L);   // save last parameter as function_name
		str = strtok(NULL, "/");
	}
	
	// mqtt rpc dispatcher goes here
	if (strncmp(function_name, "set_cron", FUNCTIONNAME_L) == 0) {
		// found set_cron
		add_cron_job_from_query(dataBuf);
	}
	else if (strncmp(function_name, "clear_cron", FUNCTIONNAME_L) == 0) {
		// found clear_cron
		clear_cron_jobs();
	}
	else if (strncmp(function_name, "cron", FUNCTIONNAME_L) == 0) {
		// found cron
		reply_topic_l = os_sprintf(reply_topic, "/cron/v1/%u/%u", kmp_serial, get_unix_time());
		reply_message_l = os_sprintf(reply_message, "%d", sys_cfg.cron_jobs.n);

		if (&mqttClient) {
			// if mqtt_client is initialized
			MQTT_Publish(&mqttClient, reply_topic, reply_message, reply_message_l, 0, 0);
		}
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
		reply_topic_l = os_sprintf(reply_topic, "/status/v1/%u/%u", kmp_serial, get_unix_time());
		reply_message_l = os_sprintf(reply_message, "%s", sys_cfg.ac_thermo_state ? "open" : "close");

		if (&mqttClient) {
			// if mqtt_client is initialized
			MQTT_Publish(&mqttClient, reply_topic, reply_message, reply_message_l, 0, 0);
		}
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
	else if (strncmp(function_name, "ping", FUNCTIONNAME_L) == 0) {
		// found ping
		reply_topic_l = os_sprintf(reply_topic, "/ping/v1/%u/%u", kmp_serial, get_unix_time());
		reply_message_l = os_sprintf(reply_message, "");

		if (&mqttClient) {
			// if mqtt_client is initialized
			MQTT_Publish(&mqttClient, reply_topic, reply_message, reply_message_l, 0, 0);
		}
	}
	else if (strncmp(function_name, "version", FUNCTIONNAME_L) == 0) {
		// found version
		reply_topic_l = os_sprintf(reply_topic, "/version/v1/%u/%u", kmp_serial, get_unix_time());
		reply_message_l = os_sprintf(reply_message, "%s-%s", system_get_sdk_version(), VERSION);

		if (&mqttClient) {
			// if mqtt_client is initialized
			MQTT_Publish(&mqttClient, reply_topic, reply_message, reply_message_l, 0, 0);
		}
	}
	else if (strncmp(function_name, "uptime", FUNCTIONNAME_L) == 0) {
		// found uptime
		reply_topic_l = os_sprintf(reply_topic, "/uptime/v1/%u/%u", kmp_serial, get_unix_time());
		reply_message_l = os_sprintf(reply_message, "%u", uptime());

		if (&mqttClient) {
			// if mqtt_client is initialized
			MQTT_Publish(&mqttClient, reply_topic, reply_message, reply_message_l, 0, 0);
		}
	}
	
	os_free(topicBuf);
	os_free(dataBuf);
//	t2 = system_get_rtc_time();
//	os_printf("\n\rtdiff: %u\n\r", t2 - t1);
}

#ifdef IMPULSE
ICACHE_FLASH_ATTR void gpio_int_init() {
	os_printf("gpio_int_init()\n");
	impulse_meter_count = 0;
	last_impulse_meter_count = 0;
	ETS_GPIO_INTR_DISABLE();										// Disable gpio interrupts
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDI_U, FUNC_GPIO0);				// Set GPIO0 function
	gpio_output_set(0, 0, 0, GPIO_ID_PIN(0));						// Set GPIO0 as input
	//ETS_GPIO_INTR_ATTACH(gpio_int_handler, 0);					// GPIO0 interrupt handler
	gpio_intr_handler_register(gpio_int_handler, NULL);
	//PIN_PULLDOWN_DIS(PERIPHS_IO_MUX_GPIO0_U);						// disable pullodwn
	PIN_PULLUP_EN(PERIPHS_IO_MUX_GPIO0_U);							// pull - up pin
	GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, BIT(0));				// Clear GPIO0 status
	gpio_pin_intr_state_set(GPIO_ID_PIN(0), GPIO_PIN_INTR_POSEDGE);	// Interrupt on falling GPIO0 edge
	ETS_GPIO_INTR_ENABLE();											// Enable gpio interrupts
	//wdt_feed();
}
#endif

#ifdef IMPULSE
void gpio_int_handler(uint32_t interrupt_mask, void *arg) {
	uint32_t gpio_status;

	gpio_intr_ack(interrupt_mask);

	ETS_GPIO_INTR_DISABLE(); // Disable gpio interrupts
	gpio_pin_intr_state_set(GPIO_ID_PIN(0), GPIO_PIN_INTR_DISABLE);
	//wdt_feed();
	impulse_meter_count++;
	
	gpio_status = GPIO_REG_READ(GPIO_STATUS_ADDRESS);
	//clear interrupt status
	GPIO_REG_WRITE(GPIO_STATUS_W1TC_ADDRESS, gpio_status);
	
	ets_uart_printf("GPIO Interrupt!\r\n");
	
	// arm the debounce timer to enable GPIO interrupt again
	os_timer_disarm(&debounce_timer);
	os_timer_setfn(&debounce_timer, (os_timer_func_t *)debounce_timer_func, NULL);
	os_timer_arm(&debounce_timer, 1000, 0);	
}
#endif

ICACHE_FLASH_ATTR void user_init(void) {
	uart_init(BIT_RATE_1200, BIT_RATE_1200);
	os_printf("\n\r");
	os_printf("SDK version: %s\n\r", system_get_sdk_version());
	os_printf("Software version: %s\n\r", VERSION);
#ifdef DEBUG
	os_printf("\t(DEBUG)\n\r");
#endif
#ifdef IMPULSE
	os_printf("\t(IMPULSE)\n\r");
#endif
#ifdef DEBUG_NO_METER
	os_printf("\t(DEBUG_NO_METER)\n\r");
#endif
#ifdef DEBUG_MQTT_PING
	os_printf("\t(DEBUG_MQTT_PING)\n\r");
#endif
#ifdef DEBUG_SHORT_WEB_CONFIG_TIME
	os_printf("\t(DEBUG_SHORT_WEB_CONFIG_TIME)\n\r");
#endif
#ifdef THERMO_NO
	os_printf("\t(THERMO_NO)\n\r");
#else
	os_printf("\t(THERMO_NC)\n\r");
#endif

#ifndef DEBUG
	// disable serial debug
	system_set_os_print(0);
#endif

	cfg_load();

	// start kmp_request
#ifdef EN61107
	en61107_request_init();
#elif defined IMPULSE
	//kmp_request_init();
#else
	kmp_request_init();
#endif
	
	// initialize the GPIO subsystem
	gpio_init();
	// enable gpio interrupt for impulse meters
#ifdef IMPULSE
	gpio_int_init();
#endif
	
	ac_out_init();

	led_init();
	cron_init();
	
	// load thermo motor state from flash(AC OUT 1)
	if (sys_cfg.ac_thermo_state) {
		ac_thermo_open();
	}
	else {
		ac_thermo_close();
	}
	
	// make sure the device is in AP and STA combined mode
    wifi_set_opmode_current(STATIONAP_MODE);
	
	// do everything else in system_init_done
	system_init_done_cb(&system_init_done);
}

ICACHE_FLASH_ATTR void system_init_done(void) {
	// wait 10 seconds before starting wifi and let the meter boot
	// and send serial number request
#ifdef EN61107
	os_timer_disarm(&en61107_request_send_timer);
	os_timer_setfn(&en61107_request_send_timer, (os_timer_func_t *)en61107_request_send_timer_func, NULL);
	os_timer_arm(&en61107_request_send_timer, 10000, 0);
#elif defined IMPULSE
	//os_timer_disarm(&kmp_request_send_timer);
	//os_timer_setfn(&kmp_request_send_timer, (os_timer_func_t *)kmp_request_send_timer_func, NULL);
	//os_timer_arm(&kmp_request_send_timer, 10000, 0);
#else
	os_timer_disarm(&kmp_request_send_timer);
	os_timer_setfn(&kmp_request_send_timer, (os_timer_func_t *)kmp_request_send_timer_func, NULL);
	os_timer_arm(&kmp_request_send_timer, 10000, 0);
#endif
			
	// start waiting for serial number after 16 seconds
	// and start ap mode
	os_timer_disarm(&config_mode_timer);
	os_timer_setfn(&config_mode_timer, (os_timer_func_t *)config_mode_timer_func, NULL);
	os_timer_arm(&config_mode_timer, 16000, 0);

	// wait for 120 seconds from boot and go to station mode
	os_timer_disarm(&sample_mode_timer);
	os_timer_setfn(&sample_mode_timer, (os_timer_func_t *)sample_mode_timer_func, NULL);
#ifndef DEBUG_SHORT_WEB_CONFIG_TIME
	os_timer_arm(&sample_mode_timer, 120000, 0);
#else
	os_timer_arm(&sample_mode_timer, 18000, 0);
#endif
		
	INFO("\r\nSystem started ...\r\n");
}

