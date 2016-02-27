#ifndef _USER_CONFIG_H_
#define _USER_CONFIG_H_


#define CFG_HOLDER	0x00FF55A4	/* Change this value to load default configurations */
#define CFG_LOCATION	0x3C	/* Please don't change or if you know what you doing */
#define CLIENT_SSL_ENABLE

/*DEFAULT CONFIGURATIONS*/

// MQTT
#define MQTT_HOST			"loppen.christiania.org" //or "mqtt.yourdomain.com"
//#define MQTT_HOST			"10.0.1.3"
#define MQTT_PORT			1883
#define MQTT_BUF_SIZE		1024
#define MQTT_KEEPALIVE		120	 /*second*/

#define MQTT_CLIENT_ID		"ESP_%08X"
#define MQTT_USER			"esp8266"
#define MQTT_PASS			"chah5Kai"

#define DEFAULT_SECURITY	1		// 1 for ssl, 0 for none

#define MQTT_RECONNECT_TIMEOUT 	5	/*second*/

#define QUEUE_BUFFER_SIZE		 		10240

#define PROTOCOL_NAMEv31	/*MQTT version 3.1 compatible with Mosquitto v0.15*/
//PROTOCOL_NAMEv311			/*MQTT version 3.11 compatible with https://eclipse.org/paho/clients/testing/*/

// WIRELESS
#define STA_SSID "Slux"
#define STA_PASS "w1reless"
//#define STA_SSID "Loppen Public"
//#define STA_PASS ""
#define STA_FALLBACK_SSID "stofferFon"
#define STA_FALLBACK_PASS "w1reless"
#define STA_TYPE AUTH_WPA2_PSK

#define AP_SSID	"KAM_%07u"
#define AP_PASSWORD	"aabbccddeeff"

#endif

