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
#include "unix_time.h"

MQTT_Client mqttClient;
static volatile os_timer_t sample_timer;

uint16 counter = 0;
ICACHE_FLASH_ATTR void sample_timerfunc(void *arg) {
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
	random_value = rand() % 100 + 0;
	message_l = os_sprintf(message, "%lu", random_value);
	MQTT_Publish(&mqttClient, topic, message, message_l, 0, 0);

	// publish return flow temperature
	topic_l = os_sprintf(topic, "/sample/%lu/return_flow_temperature", current_unix_time);
	random_value = rand() % 100 + 0;
	message_l = os_sprintf(message, "%lu", random_value);
	MQTT_Publish(&mqttClient, topic, message, message_l, 0, 0);

	// publish temperature difference
	topic_l = os_sprintf(topic, "/sample/%lu/temperature_difference", current_unix_time);
	random_value = rand() % 100 + 0;
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
	httpd_user_init();	//state 1 = config mode
	init_unix_time();   // state 2 = get ntp mode ( wait forever)
	if(status == STATION_GOT_IP){ 
		MQTT_Connect(&mqttClient);
	} else {
		MQTT_Disconnect(&mqttClient);
	}
}

ICACHE_FLASH_ATTR void mqttConnectedCb(uint32_t *args) {
	MQTT_Client* client = (MQTT_Client*)args;
	INFO("MQTT: Connected\r\n");

    // start sample timer
    os_timer_disarm(&sample_timer);
    os_timer_setfn(&sample_timer, (os_timer_func_t *)sample_timerfunc, NULL);
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

	INFO("\r\nSystem started ...\r\n");
}
