/* main.c -- MQTT client example
*
* Copyright (c) 2014-2015, Tuan PM <tuanpm at live dot com>
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* * Redistributions of source code must retain the above copyright notice,
* this list of conditions and the following disclaimer.
* * Redistributions in binary form must reproduce the above copyright
* notice, this list of conditions and the following disclaimer in the
* documentation and/or other materials provided with the distribution.
* * Neither the name of Redis nor the names of its contributors may be used
* to endorse or promote products derived from this software without
* specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
* LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
* CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
*/
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

#define user_procTaskPrio			0
#define user_proc_task_queue_len	1

os_event_t user_proc_task_queue[user_proc_task_queue_len];

MQTT_Client mqttClient;
static volatile os_timer_t sample_timer;
static volatile os_timer_t sample_mode_timer;

uint16 counter = 0;

ICACHE_FLASH_ATTR void config_mode_func(os_event_t *events) {
    struct softap_config ap_conf;
	uint8_t macaddr[6] = { 0, 0, 0, 0, 0, 0 };
	
	// make sure the device is in AP and STA combined mode
	INFO("\r\nAP mode\r\n");
	
	os_memset(ap_conf.ssid, 0, sizeof(ap_conf.ssid));
	os_sprintf(ap_conf.ssid, "CTRL_%02x%02x%02x%02x%02x%02x", MAC2STR(macaddr));
	os_memset(ap_conf.password, 0, sizeof(ap_conf.password));
	os_sprintf(ap_conf.password, "aabbccddeeffgg");
	ap_conf.authmode = AUTH_WPA_PSK;
	ap_conf.channel = 7;
	ap_conf.max_connection = 255; // 1?
	ap_conf.ssid_hidden = 0;

	wifi_softap_set_config(&ap_conf);
	wifi_set_opmode(STATIONAP_MODE);
	os_delay_us(10000);

	httpd_user_init();	//state 1 = config mode
	/*
	// setup the soft AP
	os_bzero(&ap_conf, sizeof(struct softap_config));
	wifi_softap_get_config(&ap_conf);
	wifi_get_macaddr(SOFTAP_IF, macaddr);
	os_strncpy(ap_conf.ssid, AP_SSID, sizeof(ap_conf.ssid));
	ap_conf.ssid_len = strlen(AP_SSID);
	os_strncpy(ap_conf.password, AP_PASSWORD, sizeof(ap_conf.password));
	//os_snprintf(&ap_conf.password[strlen(AP_PASSWORD)], sizeof(ap_conf.password) - strlen(AP_PASSWORD), "_%02X%02X%02X", macaddr[3], macaddr[4], macaddr[5]);
	os_sprintf(ap_conf.password[strlen(AP_PASSWORD)], "_%02X%02X%02X", macaddr[3], macaddr[4], macaddr[5]);
	ap_conf.authmode = AUTH_WPA_PSK;
	ap_conf.channel = 6;
	ETS_UART_INTR_DISABLE(); 
	wifi_softap_set_config(&ap_conf);
	ETS_UART_INTR_ENABLE();
	

	wifi_station_connect();
	*/
}

ICACHE_FLASH_ATTR void sample_mode_func(void *arg) {
	CFG_Load();

	MQTT_InitConnection(&mqttClient, sysCfg.mqtt_host, sysCfg.mqtt_port, sysCfg.security);
	//MQTT_InitConnection(&mqttClient, "192.168.11.122", 1880, 0);

	MQTT_InitClient(&mqttClient, sysCfg.device_id, sysCfg.mqtt_user, sysCfg.mqtt_pass, sysCfg.mqtt_keepalive, 1);
	//MQTT_InitClient(&mqttClient, "client_id", "user", "pass", 120, 1);

	MQTT_InitLWT(&mqttClient, "/sample", "offline", 0, 0);
	MQTT_OnConnected(&mqttClient, mqttConnectedCb);
	MQTT_OnDisconnected(&mqttClient, mqttDisconnectedCb);
	MQTT_OnPublished(&mqttClient, mqttPublishedCb);
	MQTT_OnData(&mqttClient, mqttDataCb);

	WIFI_Connect(sysCfg.sta_ssid, sysCfg.sta_pwd, wifiConnectCb);
}

ICACHE_FLASH_ATTR void sample_timer_func(void *arg) {
	uint64 current_unix_time;
	char message[128];
	int message_l;
	char topic[128];
	int topic_l;
	uint32 random_value;								// DEBUG: for meter test data generation
		
	current_unix_time = (uint32)(get_unix_time());		// TODO before 2038 ,-)
	
	// publish counter
	topic_l = os_sprintf(topic, "/sample/%lu/counter", current_unix_time);
	message_l = os_sprintf(message, "%lu", counter);
	counter++;
	MQTT_Publish(&mqttClient, topic, message, message_l, 0, 0);

	// publish heap size
	topic_l = os_sprintf(topic, "/sample/%lu/heap", current_unix_time);
	message_l = os_sprintf(message, "%lu", system_get_free_heap_size());
	MQTT_Publish(&mqttClient, topic, message, message_l, 0, 0);


	// heating meter specific
	
	// publish flow temperature
	topic_l = os_sprintf(topic, "/sample/%lu/flow_temperature", current_unix_time);
	random_value = rand() % 20 + 70;
	message_l = os_sprintf(message, "%lu", random_value);
	MQTT_Publish(&mqttClient, topic, message, message_l, 0, 0);

	// publish return flow temperature
	topic_l = os_sprintf(topic, "/sample/%lu/return_flow_temperature", current_unix_time);
	random_value = rand() % 30 + 40;
	message_l = os_sprintf(message, "%lu", random_value);
	MQTT_Publish(&mqttClient, topic, message, message_l, 0, 0);

	// publish temperature difference
	topic_l = os_sprintf(topic, "/sample/%lu/temperature_difference", current_unix_time);
	random_value = rand() % 10 + 20;
	message_l = os_sprintf(message, "%lu", random_value);
	MQTT_Publish(&mqttClient, topic, message, message_l, 0, 0);

	// publish flow
	topic_l = os_sprintf(topic, "/sample/%lu/flow", current_unix_time);
	random_value = rand() % 100 + 0;
	message_l = os_sprintf(message, "%lu", random_value);
	MQTT_Publish(&mqttClient, topic, message, message_l, 0, 0);

	// publish current power
	topic_l = os_sprintf(topic, "/sample/%lu/current_power", current_unix_time);
	random_value = rand() % 100 + 0;
	message_l = os_sprintf(message, "%lu", random_value);
	MQTT_Publish(&mqttClient, topic, message, message_l, 0, 0);

	// publish hours
	topic_l = os_sprintf(topic, "/sample/%lu/hours", current_unix_time);
	random_value = rand() % 100 + 0;
	message_l = os_sprintf(message, "%lu", random_value);
	MQTT_Publish(&mqttClient, topic, message, message_l, 0, 0);

	// publish volume
	topic_l = os_sprintf(topic, "/sample/%lu/volume", current_unix_time);
	random_value = rand() % 100 + 0;
	message_l = os_sprintf(message, "%lu", random_value);
	MQTT_Publish(&mqttClient, topic, message, message_l, 0, 0);

	// publish power
	topic_l = os_sprintf(topic, "/sample/%lu/power", current_unix_time);
	random_value = rand() % 100 + 0;
	message_l = os_sprintf(message, "%lu", random_value);
	MQTT_Publish(&mqttClient, topic, message, message_l, 0, 0);
}

ICACHE_FLASH_ATTR void wifiConnectCb(uint8_t status) {
//	httpd_user_init();	//state 1 = config mode
	if(status == STATION_GOT_IP){ 
		init_unix_time();   // state 2 = get ntp mode ( wait forever)
		MQTT_Connect(&mqttClient);
	} else {
		MQTT_Disconnect(&mqttClient);
	}
}

ICACHE_FLASH_ATTR void mqttConnectedCb(uint32_t *args) {
	MQTT_Client* client = (MQTT_Client*)args;
	INFO("MQTT: Connected\r\n");

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
	char *topicBuf = (char*)os_zalloc(topic_len+1),
			*dataBuf = (char*)os_zalloc(data_len+1);

	MQTT_Client* client = (MQTT_Client*)args;

	os_memcpy(topicBuf, topic, topic_len);
	topicBuf[topic_len] = 0;

	os_memcpy(dataBuf, data, data_len);
	dataBuf[data_len] = 0;

	INFO("Receive topic: %s, data: %s \r\n", topicBuf, dataBuf);
	
	os_free(topicBuf);
	os_free(dataBuf);
}

ICACHE_FLASH_ATTR void user_init(void) {
	uart_init(BIT_RATE_115200, BIT_RATE_115200);
	os_delay_us(1000000);
	
	// boot in ap mode
	system_os_task(config_mode_func, user_procTaskPrio, user_proc_task_queue, user_proc_task_queue_len);
	system_os_post(user_procTaskPrio, 0, 0 );
	
	// wait for 30 seconds and go to station mode
    os_timer_disarm(&sample_mode_timer);
    os_timer_setfn(&sample_mode_timer, (os_timer_func_t *)sample_mode_func, NULL);
    os_timer_arm(&sample_mode_timer, 60000, 0);
	
	INFO("\r\nSystem started ...\r\n");
}
