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

#include <esp8266.h>
#include <lwip/ip.h>
#include <lwip/udp.h>
#include <lwip/tcp_impl.h>
#include <netif/etharp.h>
#include <lwip/netif.h>
#include <lwip/lwip_napt.h>
#include <lwip/dns.h>
#include <lwip/app/dhcpserver.h>
#include <lwip/opt.h>
#include <espconn.h>

#include "mqtt_msg.h"
#include "debug.h"
#include "user_config.h"
#include "mqtt.h"
#include "mqtt_utils.h"
#include "queue.h"

#define MQTT_TASK_PRIO				USER_TASK_PRIO_1
#define MQTT_TASK_QUEUE_SIZE		100

#ifndef QUEUE_BUFFER_SIZE
#define QUEUE_BUFFER_SIZE			2048
#endif

os_event_t mqtt_procTaskQueue[MQTT_TASK_QUEUE_SIZE];

struct espconn mqtt_espconn;
esp_tcp mqtt_esp_tcp;

#ifdef PROTOCOL_NAMEv311
LOCAL uint8_t zero_len_id[2] = { 0, 0 };
#endif

LOCAL void ICACHE_FLASH_ATTR
mqtt_dns_found(const char *name, ip_addr_t *ipaddr, void *arg)
{
#ifdef DEBUG
	printf("\t-> %s()\n\r", __FUNCTION__);
#endif
	struct espconn *pConn = (struct espconn *)arg;
	MQTT_Client* client = (MQTT_Client *)pConn->reverse;
	if (client == NULL) return;

	if (ipaddr == NULL)
	{
		INFO("DNS: Found, but got no ip, try to reconnect\r\n");
		client->connState = TCP_RECONNECT_REQ;
		return;
	}

	INFO("DNS: found ip %d.%d.%d.%d\n",
		*((uint8 *) &ipaddr->addr),
		*((uint8 *) &ipaddr->addr + 1),
		*((uint8 *) &ipaddr->addr + 2),
		*((uint8 *) &ipaddr->addr + 3));

	if (ipaddr->addr != IPADDR_ANY) {
		if (client->pCon == NULL || client->pCon->proto.tcp == NULL) {
			INFO("Error: proto.tcp is NULL in mqtt_dns_found\n");
			return;
		}
		
		client->pCon->proto.tcp->remote_ip[0] = (ipaddr->addr) & 0xff;
		client->pCon->proto.tcp->remote_ip[1] = (ipaddr->addr >> 8) & 0xff;
		client->pCon->proto.tcp->remote_ip[2] = (ipaddr->addr >> 16) & 0xff;
		client->pCon->proto.tcp->remote_ip[3] = (ipaddr->addr >> 24) & 0xff;
		if (client->security) {
#ifdef MQTT_SSL_ENABLE
			espconn_secure_connect(client->pCon);
#else
			INFO("TCP: Do not support SSL\r\n");
#endif
		}
		else {
			espconn_connect(client->pCon);
		}

		client->connState = TCP_CONNECTING;
		INFO("TCP: connecting...\r\n");
	}

	system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)client);
}



LOCAL void ICACHE_FLASH_ATTR
deliver_publish(MQTT_Client* client, uint8_t* message, int length)
{
	mqtt_event_data_t event_data;

	event_data.topic_length = length;
	event_data.topic = mqtt_get_publish_topic(message, &event_data.topic_length);
	event_data.data_length = length;
	event_data.data = mqtt_get_publish_data(message, &event_data.data_length);

	if (client->dataCb) {
		//system_soft_wdt_feed();
		WRITE_PERI_REG(0X60000914, 0X73);
		client->dataCb((uint32_t*)client, event_data.topic, event_data.topic_length, event_data.data, event_data.data_length);
	}
}

LOCAL void ICACHE_FLASH_ATTR
deliver_pingresp(MQTT_Client* client, uint8_t* message, int length)
{
	if (client->pingrespCb)
		client->pingrespCb((uint32_t*)client);

}

void ICACHE_FLASH_ATTR
mqtt_send_keepalive(MQTT_Client *client) {
	INFO("\r\nMQTT: Send keepalive packet to %s:%d!\r\n", client->host, client->port);
	client->mqtt_state.outbound_message = mqtt_msg_pingreq(&client->mqtt_state.mqtt_connection);
	client->mqtt_state.pending_msg_type = MQTT_MSG_TYPE_PINGREQ;
	client->mqtt_state.pending_msg_type = mqtt_get_type(client->mqtt_state.outbound_message->data);
	client->mqtt_state.pending_msg_id = mqtt_get_id(client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length);


	client->sendTimeout = MQTT_SEND_TIMOUT;
	INFO("MQTT: Sending, type: %d, id: %04X\r\n", client->mqtt_state.pending_msg_type, client->mqtt_state.pending_msg_id);
	err_t result = ESPCONN_OK;
	if (client->security) {
#ifdef MQTT_SSL_ENABLE
		result = espconn_secure_send(client->pCon, client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length);
#else
		INFO("TCP: Do not support SSL\r\n");
#endif
	}
	else {
		result = espconn_send(client->pCon, client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length);
	}

	client->mqtt_state.outbound_message = NULL;
	if(ESPCONN_OK == result) {
		client->keepAliveTick = 0;
		client->connState = MQTT_DATA;
		system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)client);
	}
	else {
		client->connState = TCP_RECONNECT_DISCONNECTING;
		system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)client);
	}
}

/**
  * @brief  Delete tcp client and free all memory
  * @param  mqttClient: The mqtt client which contain TCP client
  * @retval None
  */
void ICACHE_FLASH_ATTR
mqtt_tcpclient_delete(MQTT_Client *mqttClient)
{
	if (mqttClient->pCon != NULL) {
		INFO("TCP: Free memory\r\n");
		// Force abort connections
		espconn_abort(mqttClient->pCon);
		// Delete connections
		espconn_delete(mqttClient->pCon);
		
		mqttClient->pCon = NULL;
	}
}

/**
  * @brief  Delete MQTT client and free all memory
  * @param  mqttClient: The mqtt client
  * @retval None
  */
void ICACHE_FLASH_ATTR
mqtt_client_delete(MQTT_Client *mqttClient)
{
	if(mqttClient == NULL) {
		INFO("MQTT: client already deleted\r\n");
		return;
	}

	if (mqttClient->pCon != NULL){
		mqtt_tcpclient_delete(mqttClient);
	}

	if (mqttClient->host != NULL) {
		os_free(mqttClient->host);
		mqttClient->host = NULL;
	}

	if (mqttClient->user_data != NULL) {
		os_free(mqttClient->user_data);
		mqttClient->user_data = NULL;
	}

	if(mqttClient->mqtt_state.in_buffer != NULL) {
		os_free(mqttClient->mqtt_state.in_buffer);
		mqttClient->mqtt_state.in_buffer = NULL;
	}

	if(mqttClient->mqtt_state.out_buffer != NULL) {
		os_free(mqttClient->mqtt_state.out_buffer);
		mqttClient->mqtt_state.out_buffer = NULL;
	}

	if(mqttClient->mqtt_state.outbound_message != NULL) {
		if(mqttClient->mqtt_state.outbound_message->data != NULL)
		{
			os_free(mqttClient->mqtt_state.outbound_message->data);
			mqttClient->mqtt_state.outbound_message->data = NULL;
		}
	}

	if(mqttClient->mqtt_state.mqtt_connection.buffer != NULL) {
		// Already freed but not NULL
		mqttClient->mqtt_state.mqtt_connection.buffer = NULL;
	}

	if(mqttClient->connect_info.client_id != NULL) {
		os_free(mqttClient->connect_info.client_id);
		mqttClient->connect_info.client_id = NULL;
	}

	if(mqttClient->connect_info.username != NULL) {
		os_free(mqttClient->connect_info.username);
		mqttClient->connect_info.username = NULL;
	}

	if(mqttClient->connect_info.password != NULL) {
		os_free(mqttClient->connect_info.password);
		mqttClient->connect_info.password = NULL;
	}

	if(mqttClient->connect_info.will_topic != NULL) {
		os_free(mqttClient->connect_info.will_topic);
		mqttClient->connect_info.will_topic = NULL;
	}

	if(mqttClient->connect_info.will_message != NULL) {
		os_free(mqttClient->connect_info.will_message);
		mqttClient->connect_info.will_message = NULL;
	}

	if (mqttClient->msgQueue.buf != NULL) {
		os_free(mqttClient->msgQueue.buf);
		mqttClient->msgQueue.buf = NULL;
	}

	// Initialize state
	mqttClient->connState = WIFI_INIT;
	// Clear callback functions to avoid abnormal callback
	mqttClient->connectedCb = NULL;
	mqttClient->disconnectedCb = NULL;
	mqttClient->publishedCb = NULL;
	mqttClient->timeoutCb = NULL;
	mqttClient->dataCb = NULL;
	mqttClient->pingrespCb = NULL;
}


/**
  * @brief  Client received callback function.
  * @param  arg: contain the ip link information
  * @param  pdata: received data
  * @param  len: the lenght of received data
  * @retval None
  */
void ICACHE_FLASH_ATTR
mqtt_tcpclient_recv(void *arg, char *pdata, unsigned short len)
{
	uint8_t msg_type;
	uint8_t msg_qos;
	uint16_t msg_id;
	uint8_t msg_conn_ret;

	struct espconn *pCon = (struct espconn*)arg;
	MQTT_Client *client = (MQTT_Client *)pCon->reverse;
	if (client == NULL) return; // aborted connection

//READPACKET:
	INFO("TCP: data received %d bytes\r\n", len);
	// INFO("STATE: %d\r\n", client->connState);
	if (len < MQTT_BUF_SIZE && len > 0) {
		memcpy(client->mqtt_state.in_buffer, pdata, len);
		
		msg_type = mqtt_get_type(client->mqtt_state.in_buffer);
		msg_qos = mqtt_get_qos(client->mqtt_state.in_buffer);
		msg_id = mqtt_get_id(client->mqtt_state.in_buffer, client->mqtt_state.in_buffer_length);
		switch (client->connState) {
		case MQTT_CONNECT_SENDING:
			if (msg_type == MQTT_MSG_TYPE_CONNACK) {
				if (client->mqtt_state.pending_msg_type != MQTT_MSG_TYPE_CONNECT) {
					INFO("MQTT: Invalid packet\r\n");
					if (client->security) {
#ifdef MQTT_SSL_ENABLE
						espconn_secure_disconnect(client->pCon);
#else
						INFO("TCP: Do not support SSL\r\n");
#endif
					}
					else {
						espconn_disconnect(client->pCon);
					}
				} else {
					msg_conn_ret = mqtt_get_connect_return_code(client->mqtt_state.in_buffer);
					switch (msg_conn_ret) {
						case CONNECTION_ACCEPTED:
							INFO("MQTT: Connected to %s:%d\r\n", client->host, client->port);
							client->connState = MQTT_DATA;
							if (client->connectedCb) {
								//system_soft_wdt_feed();
								WRITE_PERI_REG(0X60000914, 0X73);
								client->connectedCb((uint32_t*)client);
							}
						break;
						case CONNECTION_REFUSE_PROTOCOL:
						case CONNECTION_REFUSE_SERVER_UNAVAILABLE:
						case CONNECTION_REFUSE_BAD_USERNAME:
						case CONNECTION_REFUSE_NOT_AUTHORIZED:
							INFO("MQTT: Connection refuse, reason code: %d\r\n", msg_conn_ret);
						default:
						if (client->security) {
#ifdef MQTT_SSL_ENABLE
							espconn_secure_disconnect(client->pCon);
#else
							INFO("TCP: Do not support SSL\r\n");
#endif
						}
						else {
							espconn_disconnect(client->pCon);
						}
					}
				}

			}
			break;
		case MQTT_DATA:
		case MQTT_KEEPALIVE_SEND:
			client->mqtt_state.message_length_read = len;
			client->mqtt_state.message_length = mqtt_get_total_length(client->mqtt_state.in_buffer, client->mqtt_state.message_length_read);

			switch (msg_type) {
			case MQTT_MSG_TYPE_SUBACK:
				if (client->mqtt_state.pending_msg_type == MQTT_MSG_TYPE_SUBSCRIBE && client->mqtt_state.pending_msg_id == msg_id)
					INFO("MQTT: Subscribe successful\r\n");
				break;
			case MQTT_MSG_TYPE_UNSUBACK:
				if (client->mqtt_state.pending_msg_type == MQTT_MSG_TYPE_UNSUBSCRIBE && client->mqtt_state.pending_msg_id == msg_id)
					INFO("MQTT: UnSubscribe successful\r\n");
				break;
			case MQTT_MSG_TYPE_PUBLISH:
				if (msg_qos == 1)
					client->mqtt_state.outbound_message = mqtt_msg_puback(&client->mqtt_state.mqtt_connection, msg_id);
				else if (msg_qos == 2)
					client->mqtt_state.outbound_message = mqtt_msg_pubrec(&client->mqtt_state.mqtt_connection, msg_id);
				if (msg_qos == 1 || msg_qos == 2) {
					INFO("MQTT: Queue response QoS: %d\r\n", msg_qos);
					if (QUEUE_Puts(&client->msgQueue, client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length) == -1) {
						INFO("MQTT: Queue full\r\n");
					}
				}

				deliver_publish(client, client->mqtt_state.in_buffer, client->mqtt_state.message_length_read);
				break;
			case MQTT_MSG_TYPE_PUBACK:
				if (client->mqtt_state.pending_msg_type == MQTT_MSG_TYPE_PUBLISH && client->mqtt_state.pending_msg_id == msg_id) {
					INFO("MQTT: received MQTT_MSG_TYPE_PUBACK, finish QoS1 publish\r\n");
				}
				break;
			case MQTT_MSG_TYPE_PUBREC:
				client->mqtt_state.outbound_message = mqtt_msg_pubrel(&client->mqtt_state.mqtt_connection, msg_id);
				INFO("MQTT: received MQTT_MSG_TYPE_PUBREC for id: %d\r\n", msg_id);
				if (QUEUE_Puts(&client->msgQueue, client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length) == -1) {
					INFO("MQTT: Queue full\r\n");
				}
				break;
			case MQTT_MSG_TYPE_PUBREL:
				client->mqtt_state.outbound_message = mqtt_msg_pubcomp(&client->mqtt_state.mqtt_connection, msg_id);
				if (QUEUE_Puts(&client->msgQueue, client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length) == -1) {
					INFO("MQTT: Queue full\r\n");
				}
				break;
			case MQTT_MSG_TYPE_PUBCOMP:
				INFO("MQTT: received MQTT_MSG_TYPE_PUBCOMP for id: %d\r\n", msg_id);
				if (client->mqtt_state.pending_msg_type == MQTT_MSG_TYPE_PUBLISH && client->mqtt_state.pending_msg_id == msg_id) {
					INFO("MQTT: receive MQTT_MSG_TYPE_PUBCOMP, finish QoS2 publish\r\n");
				}
				break;
			case MQTT_MSG_TYPE_PINGREQ:
				client->mqtt_state.outbound_message = mqtt_msg_pingresp(&client->mqtt_state.mqtt_connection);
				if (QUEUE_Puts(&client->msgQueue, client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length) == -1) {
					INFO("MQTT: Queue full\r\n");
				}
				break;
			case MQTT_MSG_TYPE_PINGRESP:
				deliver_pingresp(client, client->mqtt_state.in_buffer, client->mqtt_state.message_length_read);
				break;
			}
			// NOTE: this is done down here and not in the switch case above
			// because the PSOCK_READBUF_LEN() won't work inside a switch
			// statement due to the way protothreads resume.
			if (msg_type == MQTT_MSG_TYPE_PUBLISH)
			{
				len = client->mqtt_state.message_length_read;
				
				if (client->mqtt_state.message_length < client->mqtt_state.message_length_read)
				{
					INFO("Get another published message - ignoring\r\n");
					
					len -= client->mqtt_state.message_length;
					pdata += client->mqtt_state.message_length;
					
					// save rest of data to buffer so it can be processed via task function
					//client->mqtt_state.in_buffer_length = len;
					//memcpy(client->mqtt_state.in_buffer, pdata, len);
					
					//client->connState = MQTT_PUBLISH_RECV;
					//Not Implement yet
					//goto READPACKET;
				}
			}
			break;
		}
	} else {
		INFO("ERROR: Message too long\r\n");
	}
	system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)client);
}

/**
  * @brief  Client send over callback function.
  * @param  arg: contain the ip link information
  * @retval None
  */
void ICACHE_FLASH_ATTR
mqtt_tcpclient_sent_cb(void *arg)
{
	struct espconn *pCon = (struct espconn *)arg;
	MQTT_Client* client = (MQTT_Client *)pCon->reverse;
	if (client == NULL) return; // aborted connection
	
	INFO("TCP: Sent\r\n");
	client->sendTimeout = 0;
	client->keepAliveTick =0;

	if ((client->connState == MQTT_DATA || client->connState == MQTT_KEEPALIVE_SEND)
				&& client->mqtt_state.pending_msg_type == MQTT_MSG_TYPE_PUBLISH) {
		if (client->publishedCb)
			client->publishedCb((uint32_t*)client);
	}
	system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)client);
}

void ICACHE_FLASH_ATTR mqtt_timer(void *arg)
{
	MQTT_Client* client = (MQTT_Client*)arg;

	if (client->connState == MQTT_DATA) {
		client->keepAliveTick ++;
		if (client->keepAliveTick > (client->mqtt_state.connect_info->keepalive / 2)) {
			client->connState = MQTT_KEEPALIVE_SEND;
			system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)client);
		}

	} else if (client->connState == TCP_RECONNECT_REQ) {
		client->reconnectTick ++;
		if (client->reconnectTick > MQTT_RECONNECT_TIMEOUT) {
			client->reconnectTick = 0;
			client->connState = TCP_RECONNECT;
			system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)client);
			if (client->timeoutCb)
				client->timeoutCb((uint32_t*)client);
		}
	} else if (client->connState == TCP_CONNECTING) {
		client->connectTick++;
		if (client->connectTick > MQTT_CONNECT_TIMEOUT) {
			client->connectTick = 0;
			client->connState = TCP_RECONNECT;
			system_os_post(MQTT_TASK_PRIO, 0, (os_param_t) client);
			if (client->timeoutCb)
				client->timeoutCb((uint32_t*) client);
		}
	}
	if (client->sendTimeout > 0)
		client->sendTimeout --;
}

void ICACHE_FLASH_ATTR
mqtt_tcpclient_discon_cb(void *arg)
{
	struct espconn *pespconn = (struct espconn *)arg;
	MQTT_Client* client = (MQTT_Client *)pespconn->reverse;
	if (client == NULL) return;
	
	INFO("TCP: Disconnected callback\r\n");
	if(TCP_DISCONNECTING == client->connState) {
		client->connState = TCP_DISCONNECTED;
	}
	else if(MQTT_DELETING == client->connState) {
		client->connState = MQTT_DELETED;
	}
	else {
		client->connState = TCP_RECONNECT_REQ;
	}
	if (client->disconnectedCb)
		client->disconnectedCb((uint32_t*)client);

	system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)client);
}



/**
  * @brief  Tcp client connect success callback function.
  * @param  arg: contain the ip link information
  * @retval None
  */
void ICACHE_FLASH_ATTR
mqtt_tcpclient_connect_cb(void *arg)
{
	struct espconn *pCon = (struct espconn *)arg;
	MQTT_Client* client = (MQTT_Client *)pCon->reverse;
	if (client == NULL) return; // aborted connection

	espconn_regist_disconcb(client->pCon, mqtt_tcpclient_discon_cb);
	espconn_regist_recvcb(client->pCon, mqtt_tcpclient_recv);////////
	espconn_regist_sentcb(client->pCon, mqtt_tcpclient_sent_cb);///////
	INFO("MQTT: Connected to broker %s:%d\r\n", client->host, client->port);

	mqtt_msg_init(&client->mqtt_state.mqtt_connection, client->mqtt_state.out_buffer, client->mqtt_state.out_buffer_length);
	client->mqtt_state.outbound_message = mqtt_msg_connect(&client->mqtt_state.mqtt_connection, client->mqtt_state.connect_info);
	client->mqtt_state.pending_msg_type = mqtt_get_type(client->mqtt_state.outbound_message->data);
	client->mqtt_state.pending_msg_id = mqtt_get_id(client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length);


	client->sendTimeout = MQTT_SEND_TIMOUT;
	INFO("MQTT: Sending, type: %d, id: %04X\r\n", client->mqtt_state.pending_msg_type, client->mqtt_state.pending_msg_id);
	if (client->security) {
#ifdef MQTT_SSL_ENABLE
		espconn_secure_send(client->pCon, client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length);
#else
		INFO("TCP: Do not support SSL\r\n");
#endif
	}
	else {
		espconn_send(client->pCon, client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length);
	}

	client->mqtt_state.outbound_message = NULL;
	client->connState = MQTT_CONNECT_SENDING;
	system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)client);
}

/**
  * @brief  Tcp client connect repeat callback function.
  * @param  arg: contain the ip link information
  * @retval None
  */
void ICACHE_FLASH_ATTR
mqtt_tcpclient_recon_cb(void *arg, sint8 errType)
{
	struct espconn *pCon = (struct espconn *)arg;
	MQTT_Client* client = (MQTT_Client *)pCon->reverse;
	if (client == NULL) return; // aborted connection

	INFO("TCP: Reconnect to %s:%d\r\n", client->host, client->port);

	client->connState = TCP_RECONNECT_REQ;

	system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)client);

}

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
	client->mqtt_state.outbound_message = mqtt_msg_publish(&client->mqtt_state.mqtt_connection,
	                                      topic, data, data_length,
	                                      qos, retain,
	                                      &client->mqtt_state.pending_msg_id);
	if (client->mqtt_state.outbound_message->length == 0) {
		INFO("MQTT: Queuing publish failed\r\n");
		return FALSE;
	}
	INFO("MQTT: queuing publish, length: %d, queue size(%ld/%ld)\r\n", client->mqtt_state.outbound_message->length, client->msgQueue.rb.fill_cnt, client->msgQueue.rb.size);
	while (QUEUE_Puts(&client->msgQueue, client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length) == -1) {
		INFO("MQTT: Queue full\r\n");
		if (QUEUE_Gets(&client->msgQueue, dataBuffer, &dataLen, MQTT_BUF_SIZE) == -1) {
			INFO("MQTT: Serious buffer error\r\n");
			return FALSE;
		}
	}
	system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)client);
	return TRUE;
}

/**
  * @brief  MQTT subscibe function.
  * @param  client: 	MQTT_Client reference
  * @param  topic: 		string topic will subscribe
  * @param  qos:		qos
  * @retval TRUE if success queue
  */
BOOL ICACHE_FLASH_ATTR
MQTT_Subscribe(MQTT_Client *client, char* topic, uint8_t qos)
{
	uint8_t dataBuffer[MQTT_BUF_SIZE];
	uint16_t dataLen;

	client->mqtt_state.outbound_message = mqtt_msg_subscribe(&client->mqtt_state.mqtt_connection,
	                                      topic, qos,
	                                      &client->mqtt_state.pending_msg_id);
	INFO("MQTT: queue subscribe, topic\"%s\", id: %d\r\n", topic, client->mqtt_state.pending_msg_id);
	while (QUEUE_Puts(&client->msgQueue, client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length) == -1) {
		INFO("MQTT: Queue full\r\n");
		if (QUEUE_Gets(&client->msgQueue, dataBuffer, &dataLen, MQTT_BUF_SIZE) == -1) {
			INFO("MQTT: Serious buffer error\r\n");
			return FALSE;
		}
	}
	system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)client);
	return TRUE;
}

/**
  * @brief  MQTT un-subscibe function.
  * @param  client: 	MQTT_Client reference
  * @param  topic:   String topic will un-subscribe
  * @retval TRUE if success queue
  */
BOOL ICACHE_FLASH_ATTR
MQTT_UnSubscribe(MQTT_Client *client, char* topic)
{
	uint8_t dataBuffer[MQTT_BUF_SIZE];
	uint16_t dataLen;
	client->mqtt_state.outbound_message = mqtt_msg_unsubscribe(&client->mqtt_state.mqtt_connection,
	                                      topic,
	                                      &client->mqtt_state.pending_msg_id);
	INFO("MQTT: queue un-subscribe, topic\"%s\", id: %d\r\n", topic, client->mqtt_state.pending_msg_id);
	while (QUEUE_Puts(&client->msgQueue, client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length) == -1) {
		INFO("MQTT: Queue full\r\n");
		if (QUEUE_Gets(&client->msgQueue, dataBuffer, &dataLen, MQTT_BUF_SIZE) == -1) {
			INFO("MQTT: Serious buffer error\r\n");
			return FALSE;
		}
	}
	system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)client);
	return TRUE;
}

/**
  * @brief  MQTT ping function.
  * @param  client: 	MQTT_Client reference
  * @retval TRUE if success queue
  */
BOOL ICACHE_FLASH_ATTR
MQTT_Ping(MQTT_Client *client)
{
	uint8_t dataBuffer[MQTT_BUF_SIZE];
	uint16_t dataLen;
	client->mqtt_state.outbound_message = mqtt_msg_pingreq(&client->mqtt_state.mqtt_connection);
	if(client->mqtt_state.outbound_message->length == 0){
		INFO("MQTT: Queuing publish failed\r\n");
		return FALSE;
	}
	INFO("MQTT: queuing publish, length: %d, queue size(%ld/%ld)\r\n", client->mqtt_state.outbound_message->length, client->msgQueue.rb.fill_cnt, client->msgQueue.rb.size);
	while(QUEUE_Puts(&client->msgQueue, client->mqtt_state.outbound_message->data, client->mqtt_state.outbound_message->length) == -1){
		INFO("MQTT: Queue full\r\n");
		if(QUEUE_Gets(&client->msgQueue, dataBuffer, &dataLen, MQTT_BUF_SIZE) == -1) {
			INFO("MQTT: Serious buffer error\r\n");
			return FALSE;
		}
	}
	system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)client);
	return TRUE;
}

void ICACHE_FLASH_ATTR
MQTT_Task(os_event_t *e)
{
	MQTT_Client* client = (MQTT_Client*)e->par;
	uint8_t dataBuffer[MQTT_BUF_SIZE];
	uint16_t dataLen;

#ifdef DEBUG
	printf("\t-> %s()\n\r", __FUNCTION__);
	printf("\t\tevent sig: %d, par: %d, conn state: %d\n\r", e->sig, e->par, client->connState);
#endif

	if (e->par == 0)
		return;
	switch (client->connState) {

	case TCP_RECONNECT_REQ:
		break;
	case TCP_RECONNECT:
		mqtt_tcpclient_delete(client);
		MQTT_Connect(client);
		INFO("TCP: Reconnect to: %s:%d\r\n", client->host, client->port);
		client->connState = TCP_CONNECTING;
		break;
	case MQTT_DELETING:
	case TCP_DISCONNECTING:
	case TCP_RECONNECT_DISCONNECTING:
		if (client->security) {
#ifdef MQTT_SSL_ENABLE
			espconn_secure_disconnect(client->pCon);
#else
			INFO("TCP: Do not support SSL\r\n");
#endif
		}
		else {
			espconn_disconnect(client->pCon);
		}
		break;
	case TCP_DISCONNECTED:
		INFO("MQTT: Disconnected\r\n");
		mqtt_tcpclient_delete(client);
		break;
	case MQTT_DELETED:
		INFO("MQTT: Deleted client\r\n");
		mqtt_client_delete(client);
		break;
	case MQTT_KEEPALIVE_SEND:
		mqtt_send_keepalive(client);
		break;
	case MQTT_DATA:
		if (QUEUE_IsEmpty(&client->msgQueue) || client->sendTimeout != 0) {
			break;
		}
		if (QUEUE_Gets(&client->msgQueue, dataBuffer, &dataLen, MQTT_BUF_SIZE) == 0) {
			client->mqtt_state.pending_msg_type = mqtt_get_type(dataBuffer);
			client->mqtt_state.pending_msg_id = mqtt_get_id(dataBuffer, dataLen);


			client->sendTimeout = MQTT_SEND_TIMOUT;
			INFO("MQTT: Sending, type: %d, id: %04X\r\n", client->mqtt_state.pending_msg_type, client->mqtt_state.pending_msg_id);
			client->keepAliveTick = 0;
			if (client->security) {
#ifdef MQTT_SSL_ENABLE
				if (espconn_secure_send(client->pCon, dataBuffer, dataLen)) != 0) {
					// error sending, put it back into the queue again
					INFO("MQTT: espconn_secure_send() returned an error, re-queueing\r\n");
					if (QUEUE_Puts(&client->msgQueue, dataBuffer, dataLen) == -1) {
						INFO("MQTT: Queue full\r\n");
					}
				}
#else
				INFO("TCP: Do not support SSL\r\n");
#endif
			}
			else {
				if (espconn_send(client->pCon, dataBuffer, dataLen) != 0) {
					// error sending, put it back into the queue again
					INFO("MQTT: espconn_send() returned an error, re-queueing\r\n");
					if (QUEUE_Puts(&client->msgQueue, dataBuffer, dataLen) == -1) {
						INFO("MQTT: Queue full\r\n");
					}
				}
			}

			client->mqtt_state.outbound_message = NULL;
			break;
		}
		break;
//	case MQTT_PUBLISH_RECV:
//		INFO("MQTT: processing next mqtt packet in tcp packet\r\n");
//		client->connState = MQTT_MSG_TYPE_PUBLISH;
//		memcpy(dataBuffer, client->mqtt_state.in_buffer, client->mqtt_state.in_buffer_length);
//		dataLen = client->mqtt_state.in_buffer_length;
//		mqtt_tcpclient_recv(client, dataBuffer, dataLen);
//		break;
	}
}

/**
  * @brief  MQTT initialization connection function
  * @param  client: 	MQTT_Client reference
  * @param  host: 	Domain or IP string
  * @param  port: 	Port to connect
  * @param  security:		1 for ssl, 0 for none
  * @retval None
  */
void ICACHE_FLASH_ATTR
MQTT_InitConnection(MQTT_Client *mqttClient, uint8_t* host, uint32_t port, uint8_t security)
{
	uint32_t temp;
	INFO("MQTT:InitConnection\r\n");
	memset(mqttClient, 0, sizeof(MQTT_Client));
	temp = strlen(host);
	mqttClient->host = (uint8_t*)os_zalloc(temp + 1);
	strcpy(mqttClient->host, host);
	mqttClient->host[temp] = 0;
	mqttClient->port = port;
	mqttClient->security = security;

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
BOOL ICACHE_FLASH_ATTR
MQTT_InitClient(MQTT_Client *mqttClient, uint8_t* client_id, uint8_t* client_user, uint8_t* client_pass, uint32_t keepAliveTime, uint8_t cleanSession)
{
	uint32_t temp;
	INFO("MQTT:InitClient\r\n");

	memset(&mqttClient->connect_info, 0, sizeof(mqtt_connect_info_t));

	if ( !client_id ) {
		/* Should be allowed by broker, but clean session flag must be set. */
#ifdef PROTOCOL_NAMEv311
		if (cleanSession) {
			mqttClient->connect_info.client_id = zero_len_id;
		} else {
			INFO("cleanSession must be set to use 0 length client_id\r\n");
			return false;
		}
		/* Not supported. Return. */
#else
		INFO("Client ID required for MQTT < 3.1.1!\r\n");
		return false;
#endif
	}

    /* If connect_info's client_id is still NULL and we get here, we can        *
     * assume the passed client_id is non-NULL.                                 */
    if ( !(mqttClient->connect_info.client_id) )
    {
      temp = strlen(client_id);
      mqttClient->connect_info.client_id = (uint8_t*)os_zalloc(temp + 1);
      strcpy(mqttClient->connect_info.client_id, client_id);
      mqttClient->connect_info.client_id[temp] = 0;
    }

	if (client_user)
	{
		temp = strlen(client_user);
		mqttClient->connect_info.username = (uint8_t*)os_zalloc(temp + 1);
		strcpy(mqttClient->connect_info.username, client_user);
		mqttClient->connect_info.username[temp] = 0;
	}

	if (client_pass)
	{
		temp = strlen(client_pass);
		mqttClient->connect_info.password = (uint8_t*)os_zalloc(temp + 1);
		strcpy(mqttClient->connect_info.password, client_pass);
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

	system_os_task(MQTT_Task, MQTT_TASK_PRIO, mqtt_procTaskQueue, MQTT_TASK_QUEUE_SIZE);
	system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)mqttClient);
	return true;
}
void ICACHE_FLASH_ATTR
MQTT_InitLWT(MQTT_Client *mqttClient, uint8_t* will_topic, uint8_t* will_msg, uint8_t will_qos, uint8_t will_retain)
{
	uint32_t temp;
	temp = strlen(will_topic);
	mqttClient->connect_info.will_topic = (uint8_t*)os_zalloc(temp + 1);
	strcpy(mqttClient->connect_info.will_topic, will_topic);
	mqttClient->connect_info.will_topic[temp] = 0;

	temp = strlen(will_msg);
	mqttClient->connect_info.will_message = (uint8_t*)os_zalloc(temp + 1);
	strcpy(mqttClient->connect_info.will_message, will_msg);
	mqttClient->connect_info.will_message[temp] = 0;


	mqttClient->connect_info.will_qos = will_qos;
	mqttClient->connect_info.will_retain = will_retain;
}
/**
  * @brief  Begin connect to MQTT broker
  * @param  client: MQTT_Client reference
  * @retval None
  */
void ICACHE_FLASH_ATTR
MQTT_Connect(MQTT_Client *mqttClient)
{
	uint32_t keeplive;
	err_t dns_err;
	
	if (mqttClient->pCon) {
		// Clean up the old connection forcefully - using MQTT_Disconnect
		// does not actually release the old connection until the
		// disconnection callback is invoked.
		mqtt_tcpclient_delete(mqttClient);
	}
	
	memset(&mqtt_espconn, 0, sizeof(mqtt_espconn));
	mqttClient->pCon = &mqtt_espconn;
	mqttClient->pCon->type = ESPCONN_TCP;
	mqttClient->pCon->state = ESPCONN_NONE;
	memset(&mqtt_esp_tcp, 0, sizeof(mqtt_esp_tcp));
	mqttClient->pCon->proto.tcp = &mqtt_esp_tcp;
	mqttClient->pCon->proto.tcp->local_port = espconn_port();
	mqttClient->pCon->proto.tcp->remote_port = mqttClient->port;
	mqttClient->pCon->reverse = mqttClient;
	
	// enable TCP keep alive
	espconn_set_opt(mqttClient->pCon, ESPCONN_KEEPALIVE);
	
	// set keepalive: 75s = 60 + 5*3
	keeplive = 60;
	espconn_set_keepalive(mqttClient->pCon, ESPCONN_KEEPIDLE, &keeplive);

	keeplive = 5;
	espconn_set_keepalive(mqttClient->pCon, ESPCONN_KEEPINTVL, &keeplive);
	
	keeplive = 3;	//try times
	espconn_set_keepalive(mqttClient->pCon, ESPCONN_KEEPCNT, &keeplive);
	
	espconn_regist_connectcb(mqttClient->pCon, mqtt_tcpclient_connect_cb);
	espconn_regist_reconcb(mqttClient->pCon, mqtt_tcpclient_recon_cb);

	mqttClient->keepAliveTick = 0;
	mqttClient->reconnectTick = 0;
	mqttClient->connectTick = 0;

	os_timer_disarm(&mqttClient->mqttTimer);
	os_timer_setfn(&mqttClient->mqttTimer, (os_timer_func_t *)mqtt_timer, mqttClient);
	os_timer_arm(&mqttClient->mqttTimer, 1000, 1);

	if (UTILS_StrToIP(mqttClient->host, &mqttClient->pCon->proto.tcp->remote_ip)) {
		INFO("TCP: Connect to ip  %s:%d\r\n", mqttClient->host, mqttClient->port);
		if (mqttClient->security)
		{
#ifdef MQTT_SSL_ENABLE
			espconn_secure_connect(mqttClient->pCon);
#else
			INFO("TCP: Do not support SSL\r\n");
#endif
		}
		else
		{
			espconn_connect(mqttClient->pCon);
		}
	}
	else {
		INFO("TCP: Connect to domain %s:%d\r\n", mqttClient->host, mqttClient->port);
		dns_err = dns_gethostbyname(mqttClient->host, &mqttClient->ip, mqtt_dns_found, mqttClient->pCon);
		if (dns_err == ERR_OK) {
#ifdef DEBUG
			printf("dns_gethostbyname() returned ERR_OK\n\r");
#endif	// DEBUG
			mqtt_dns_found(mqttClient->host, &mqttClient->ip, mqttClient->pCon);
		} else if (dns_err == ERR_INPROGRESS) {
			/* DNS request sent, wait for sntp_dns_found being called */
#ifdef DEBUG
			printf("dns_gethostbyname() returned ERR_INPROGRESS\n\r");
#endif	// DEBUG
			mqttClient->connState = TCP_CONNECTING;
			system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)mqttClient);
		}
	}
}

void ICACHE_FLASH_ATTR
MQTT_Disconnect(MQTT_Client *mqttClient)
{
	mqttClient->connState = TCP_DISCONNECTING;
	system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)mqttClient);
	os_timer_disarm(&mqttClient->mqttTimer);
}

void ICACHE_FLASH_ATTR
MQTT_DeleteClient(MQTT_Client *mqttClient)
{
	if(NULL == mqttClient)
		return;

	mqttClient->connState = MQTT_DELETED;
	// if(TCP_DISCONNECTED == mqttClient->connState) {
	// 	mqttClient->connState = MQTT_DELETED;
	// } else if(MQTT_DELETED != mqttClient->connState) {
	// 	mqttClient->connState = MQTT_DELETING;	
	// }

	system_os_post(MQTT_TASK_PRIO, 0, (os_param_t)mqttClient);
	os_timer_disarm(&mqttClient->mqttTimer);
}

void ICACHE_FLASH_ATTR
MQTT_OnConnected(MQTT_Client *mqttClient, MqttCallback connectedCb)
{
	mqttClient->connectedCb = connectedCb;
}

void ICACHE_FLASH_ATTR
MQTT_OnDisconnected(MQTT_Client *mqttClient, MqttCallback disconnectedCb)
{
	mqttClient->disconnectedCb = disconnectedCb;
}

void ICACHE_FLASH_ATTR
MQTT_OnData(MQTT_Client *mqttClient, MqttDataCallback dataCb)
{
	mqttClient->dataCb = dataCb;
}

void ICACHE_FLASH_ATTR
MQTT_OnPublished(MQTT_Client *mqttClient, MqttCallback publishedCb)
{
	mqttClient->publishedCb = publishedCb;
}

void ICACHE_FLASH_ATTR
MQTT_OnPingResp(MQTT_Client *mqttClient, MqttCallback pingrespCb)
{
	mqttClient->pingrespCb = pingrespCb;
}

void ICACHE_FLASH_ATTR
MQTT_OnTimeout(MQTT_Client *mqttClient, MqttCallback timeoutCb)
{
	mqttClient->timeoutCb = timeoutCb;
}

#ifdef DEBUG
void ICACHE_FLASH_ATTR
debug_print_mqtt_queue(MQTT_Client *client) {
	uint32_t i;
	if (client) {
		printf("size: %u, queue:\n", (uint32_t)client->msgQueue.rb.size);
		for (i = 0; i < client->msgQueue.rb.size; i++) {
			if ((i >= 1) && (*(client->msgQueue.rb.p_o + i - 1) == 0x7f) && (*(client->msgQueue.rb.p_o + i) == 0x7e)) {
				printf(".");
			}

			if ((client->msgQueue.rb.p_r == (client->msgQueue.rb.p_o + i)) && (client->msgQueue.rb.p_w == (client->msgQueue.rb.p_o + i))) {
				printf(" >< %02x", *(client->msgQueue.rb.p_o + i));
			}
			else if (client->msgQueue.rb.p_r == (client->msgQueue.rb.p_o + i)) {
				printf(" >%02x", *(client->msgQueue.rb.p_o + i));
			}
			else if (client->msgQueue.rb.p_w == (client->msgQueue.rb.p_o + i)) {
				// rolled back
				printf("< %02x", *(client->msgQueue.rb.p_o + i));
			}
			else {
				printf("%02x", *(client->msgQueue.rb.p_o + i));
			}
		}
		printf("\nfilled: %u\n", (uint32_t)client->msgQueue.rb.fill_cnt);
	}
}
#endif	// DEBUG
