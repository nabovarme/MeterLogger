/* mqtt.c
*  Protocol: http://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html
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

#include "esp8266.h"
#include "mqtt_msg.h"
#include "debug.h"
#include "user_config.h"
#include "mqtt.h"
#include "queue.h"

#define MQTT_TASK_PRIO        		USER_TASK_PRIO_1
#define MQTT_TASK_QUEUE_SIZE    	1
#define MQTT_SEND_TIMOUT			5

#ifndef QUEUE_BUFFER_SIZE
#define QUEUE_BUFFER_SIZE		 	2048
#endif

unsigned char *default_certificate;
unsigned int default_certificate_len = 0;
unsigned char *default_private_key;
unsigned int default_private_key_len = 0;




/**
  * @brief  MQTT publish function.
  * @param  client: 	MQTT_Client reference
  * @param  topic: 		string topic will publish to
  * @param  data: 		buffer data send point to
  * @param  data_length: length of data
  * @param  qos:		qos
  * @param  retain:		retain
  * @retval TRUE if success queue
  */
BOOL ICACHE_FLASH_ATTR
MQTT_Publish(MQTT_Client *client, const char* topic, const char* data, int data_length, int qos, int retain)
{
	uint8_t dataBuffer[MQTT_BUF_SIZE];
	uint16_t dataLen;
//	uint32_t i;
	client->mqtt_state.outbound_message = mqtt_msg_publish(&client->mqtt_state.mqtt_connection,
	                                      topic, data, data_length,
	                                      qos, retain,
	                                      &client->mqtt_state.pending_msg_id);
	if (client->mqtt_state.outbound_message->length == 0) {
		INFO("MQTT: Queuing publish failed\r\n");
		return FALSE;
	}
	INFO("MQTT: queuing publish, length: %d, queue size(%d/%d)\r\n", client->mqtt_state.outbound_message->length, client->msgQueue.rb.fill_cnt, client->msgQueue.rb.size);
    os_printf("MQTT_Publish id: %d\n", client->mqtt_state.pending_msg_id);
 	while (QUEUE_Puts(&client->msgQueue, client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length) == -1) {
		INFO("MQTT_Publish MQTT: Queue full\r\n");
		if (QUEUE_Gets(&client->msgQueue, dataBuffer, &dataLen, MQTT_BUF_SIZE) == -1) {
			INFO("MQTT: Serious buffer error\r\n");
			return FALSE;
		}
	}
//	system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)client);
	return TRUE;
}


/**
  * @brief  MQTT initialization mqtt client function
  * @param  client: 	MQTT_Client reference
  * @param  clientid: 	MQTT client id
  * @param  client_user:MQTT client user
  * @param  client_pass:MQTT client password
  * @param  client_pass:MQTT keep alive timer, in second
  * @retval None
  */
void ICACHE_FLASH_ATTR
MQTT_InitClient(MQTT_Client *mqttClient, uint8_t* client_id, uint8_t* client_user, uint8_t* client_pass, uint32_t keepAliveTime, uint8_t cleanSession)
{
	uint32_t temp;
	INFO("MQTT_InitClient\r\n");

	os_memset(&mqttClient->connect_info, 0, sizeof(mqtt_connect_info_t));

	temp = os_strlen(client_id);
	mqttClient->connect_info.client_id = (uint8_t*)os_zalloc(temp + 1);
	os_strcpy(mqttClient->connect_info.client_id, client_id);
	mqttClient->connect_info.client_id[temp] = 0;

	if (client_user)
	{
		temp = os_strlen(client_user);
		mqttClient->connect_info.username = (uint8_t*)os_zalloc(temp + 1);
		os_strcpy(mqttClient->connect_info.username, client_user);
		mqttClient->connect_info.username[temp] = 0;
	}

	if (client_pass)
	{
		temp = os_strlen(client_pass);
		mqttClient->connect_info.password = (uint8_t*)os_zalloc(temp + 1);
		os_strcpy(mqttClient->connect_info.password, client_pass);
		mqttClient->connect_info.password[temp] = 0;
	}


	mqttClient->connect_info.keepalive = keepAliveTime;
	mqttClient->connect_info.clean_session = cleanSession;

	mqttClient->mqtt_state.in_buffer = (uint8_t *)os_zalloc(MQTT_BUF_SIZE);
	mqttClient->mqtt_state.in_buffer_length = MQTT_BUF_SIZE;
	mqttClient->mqtt_state.out_buffer =  (uint8_t *)os_zalloc(MQTT_BUF_SIZE);
	mqttClient->mqtt_state.out_buffer_length = MQTT_BUF_SIZE;
	mqttClient->mqtt_state.connect_info = &mqttClient->connect_info;

	mqtt_msg_init(&mqttClient->mqtt_state.mqtt_connection, mqttClient->mqtt_state.out_buffer, mqttClient->mqtt_state.out_buffer_length);

	QUEUE_Init(&mqttClient->msgQueue, QUEUE_BUFFER_SIZE);

//	system_os_task(MQTT_Task, MQTT_TASK_PRIO, mqtt_procTaskQueue, MQTT_TASK_QUEUE_SIZE);
//	system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)mqttClient);
}
