#include "ets_sys.h"
#include "driver/uart.h"
#include "osapi.h"
#include "mqtt.h"
#include "wifi.h"
#include "config.h"
#include "debug.h"
#include "gpio.h"
#include "user_interface.h"
#include "mem.h"
#include "httpd_user_init.h"
#include "user_config.h"
#include "unix_time.h"
#include "user_main.h"
#include "kmp_request.h"
#include "cron.h"
#include "led.h"

#define user_proc_task_prio			0
#define user_proc_task_queue_len	1

extern unsigned int kmp_serial;

os_event_t user_proc_task_queue[user_proc_task_queue_len];

MQTT_Client mqttClient;
static volatile os_timer_t sample_timer;
static volatile os_timer_t config_mode_timer;
static volatile os_timer_t sample_mode_timer;
static volatile os_timer_t kmp_request_send_timer;

static volatile os_timer_t ac_test_timer;
static volatile os_timer_t ac_out_off_timer;

uint16 counter = 0;

ICACHE_FLASH_ATTR void config_mode_func(os_event_t *events) {
    struct softap_config ap_conf;
	
	// make sure the device is in AP and STA combined mode
	INFO("\r\nAP mode\r\n");
	
	CFG_Load();
	os_memset(ap_conf.ssid, 0, sizeof(ap_conf.ssid));
	os_sprintf(ap_conf.ssid, AP_SSID, kmp_serial);
	os_memset(ap_conf.password, 0, sizeof(ap_conf.password));
	os_sprintf(ap_conf.password, AP_PASSWORD);
	ap_conf.authmode = AUTH_WPA_PSK;
	ap_conf.channel = 7;
	ap_conf.max_connection = 4;
	ap_conf.ssid_hidden = 0;

	wifi_set_opmode(STATIONAP_MODE);
	wifi_softap_set_config(&ap_conf);
	os_delay_us(10000);

	httpd_user_init();
}

ICACHE_FLASH_ATTR void config_mode_timer_func(void *arg) {
	system_os_task(config_mode_func, user_proc_task_prio, user_proc_task_queue, user_proc_task_queue_len);
	system_os_post(user_proc_task_prio, 0, 0 );
}

ICACHE_FLASH_ATTR void sample_mode_timer_func(void *arg) {
	unsigned char topic[128];
	int topic_l;

	CFG_Load();
	
	MQTT_InitConnection(&mqttClient, sysCfg.mqtt_host, sysCfg.mqtt_port, sysCfg.security);

	MQTT_InitClient(&mqttClient, sysCfg.device_id, sysCfg.mqtt_user, sysCfg.mqtt_pass, sysCfg.mqtt_keepalive, 1);

	// set MQTT LWP topic
	topic_l = os_sprintf(topic, "/offline/v1/%u", kmp_get_received_serial());
	MQTT_InitLWT(&mqttClient, topic, "", 0, 0);
	
	MQTT_OnConnected(&mqttClient, mqttConnectedCb);
	MQTT_OnDisconnected(&mqttClient, mqttDisconnectedCb);
	MQTT_OnPublished(&mqttClient, mqttPublishedCb);
	MQTT_OnData(&mqttClient, mqttDataCb);

	WIFI_Connect(sysCfg.sta_ssid, sysCfg.sta_pwd, wifiConnectCb);
}

ICACHE_FLASH_ATTR void sample_timer_func(void *arg) {
	kmp_request_send();
}

ICACHE_FLASH_ATTR void kmp_request_send_timer_func(void *arg) {
	kmp_request_send();
}

ICACHE_FLASH_ATTR void ac_test_timer_func(void *arg) {
	// do blinky stuff
	if (GPIO_REG_READ(GPIO_OUT_ADDRESS) & BIT14) {
		//Set GPI14 to LOW
		gpio_output_set(0, BIT14, BIT14, 0);
		led_pattern_b();
	}
	else {
		//Set GPI14 to HIGH
		gpio_output_set(BIT14, 0, BIT14, 0);
		led_pattern_a();
	}
	
	if (GPIO_REG_READ(GPIO_OUT_ADDRESS) & BIT15) {
		//Set GPI15 to LOW
		gpio_output_set(0, BIT15, BIT15, 0);
	}
	else {
		//Set GPI15 to HIGH
		gpio_output_set(BIT15, 0, BIT15, 0);
	}
}

ICACHE_FLASH_ATTR void ac_out_off_timer_func(void *arg) {
	//Set GPI14 to LOW
	gpio_output_set(0, BIT14, BIT14, 0);
	
	//Set GPI15 to LOW
	gpio_output_set(0, BIT15, BIT15, 0);

#ifdef DEBUG
	os_printf("\n\rac 1 and 2 off\n\r");
#endif
	
	led_stop_pattern();
	led_off();
}
	
ICACHE_FLASH_ATTR void wifiConnectCb(uint8_t status) {
//	httpd_user_init();	//state 1 = config mode
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
	kmp_set_mqtt_client(client);
	
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
	char *topicBuf = (char*)os_zalloc(topic_len + 1);
	char *dataBuf = (char*)os_zalloc(data_len + 1);
	MQTT_Client* client = (MQTT_Client*)args;
	
	char *str;
	char function_name[FUNCTIONNAME_L];

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
	else if (strncmp(function_name, "open", FUNCTIONNAME_L) == 0) {
		// found open
#ifdef DEBUG
		os_printf("\n\rac 1 on\n\r");
#endif
		led_pattern_a();
		
		//Set GPI14 to HIGH
		gpio_output_set(BIT14, 0, BIT14, 0);
				
		// wait 10 seconds and turn ac output off
		os_timer_disarm(&ac_out_off_timer);
		os_timer_setfn(&ac_out_off_timer, (os_timer_func_t *)ac_out_off_timer_func, NULL);
		os_timer_arm(&ac_out_off_timer, 10000, 0);
	}
	else if (strncmp(function_name, "close", FUNCTIONNAME_L) == 0) {
		// found close
#ifdef DEBUG
		os_printf("\n\rac 2 on\n\r");
#endif
		led_pattern_b();
		
		//Set GPI15 to HIGH
		gpio_output_set(BIT15, 0, BIT15, 0);
		
		// wait 10 seconds and turn ac output off
		os_timer_disarm(&ac_out_off_timer);
		os_timer_setfn(&ac_out_off_timer, (os_timer_func_t *)ac_out_off_timer_func, NULL);
		os_timer_arm(&ac_out_off_timer, 10000, 0);
	}
	else if (strncmp(function_name, "off", FUNCTIONNAME_L) == 0) {
		// found off
		// turn ac output off
		os_timer_disarm(&ac_test_timer);
		os_timer_disarm(&ac_out_off_timer);
		os_timer_setfn(&ac_out_off_timer, (os_timer_func_t *)ac_out_off_timer_func, NULL);
		os_timer_arm(&ac_out_off_timer, 0, 0);
	}
	else if (strncmp(function_name, "test", FUNCTIONNAME_L) == 0) {
		// found test
#ifdef DEBUG
		os_printf("\n\rac test on\n\r");
#endif
		led_pattern_a();
		
		// set GPIO14 high and GPIO15 low
		gpio_output_set(BIT14, 0, BIT14, 0);
		gpio_output_set(0, BIT15, BIT15, 0);
	
		os_timer_disarm(&ac_test_timer);
		os_timer_setfn(&ac_test_timer, (os_timer_func_t *)ac_test_timer_func, NULL);
		os_timer_arm(&ac_test_timer, 120000, 1);
	}
	
	os_free(topicBuf);
	os_free(dataBuf);
}

ICACHE_FLASH_ATTR void user_init(void) {
	uart_init(BIT_RATE_1200, BIT_RATE_1200);
	os_printf("\n\r");
	os_printf("SDK version: %s\n\r", system_get_sdk_version());
	os_printf("Software version: %s\n\r", VERSION);
#ifdef DEBUG
	os_printf("\t(DEBUG)\n\r");
#endif
#ifdef DEBUG_NO_METER
	os_printf("\t(DEBUG_NO_METER)\n\r");
#endif
#ifdef DEBUG_SHORT_WEB_CONFIG_TIME
	os_printf("\t(DEBUG_SHORT_WEB_CONFIG_TIME)\n\r");
#endif

#ifndef DEBUG_NO_METER
	// disable serial debug
	system_set_os_print(0);
#endif

	// start kmp_request
	kmp_request_init();
	
	// initialize the GPIO subsystem
	gpio_init();
	
	//Set GPIO14 and GPIO15 to output mode
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTMS_U, FUNC_GPIO14);
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDO_U, FUNC_GPIO15);

	led_init();
	cron_init();

	// wait 10 seconds before starting wifi and let the meter boot
	// and send serial number request
	os_timer_disarm(&kmp_request_send_timer);
	os_timer_setfn(&kmp_request_send_timer, (os_timer_func_t *)kmp_request_send_timer_func, NULL);
	os_timer_arm(&kmp_request_send_timer, 10000, 0);
		
	// start waiting for serial number after 16 seconds
	// and start ap mode in a task wrapped in timer otherwise ssid cant be connected to
	os_timer_disarm(&config_mode_timer);
	os_timer_setfn(&config_mode_timer, (os_timer_func_t *)config_mode_timer_func, NULL);
	os_timer_arm(&config_mode_timer, 16000, 0);
		
	// wait for 70 seconds from boot and go to station mode
	os_timer_disarm(&sample_mode_timer);
	os_timer_setfn(&sample_mode_timer, (os_timer_func_t *)sample_mode_timer_func, NULL);
#ifndef DEBUG_SHORT_WEB_CONFIG_TIME
	os_timer_arm(&sample_mode_timer, 70000, 0);
#else
	os_timer_arm(&sample_mode_timer, 18000, 0);
#endif
	
	INFO("\r\nSystem started ...\r\n");
}
