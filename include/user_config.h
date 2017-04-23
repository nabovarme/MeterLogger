#ifndef _USER_CONFIG_H_
#define _USER_CONFIG_H_

//#define PRINTF_DEBUG

//#define CFG_HOLDER	0x00FF55A4	/* Change this value to load default configurations */
#define CFG_LOCATION	0x76	/* Please don't change or if you know what you doing */
//#define MQTT_CA_FLASH_SECTOR	0x5c

//#define MQTT_SSL_ENABLE
//#define CLIENT_SSL_ENABLE

/*DEFAULT CONFIGURATIONS*/

#define DEFAULT_METER_SERIAL	"9999999"

// MQTT
#define MQTT_TOPIC_L (128)
#define MQTT_MESSAGE_L (512)

#define MQTT_HOST			"loppen.christiania.org" //or "mqtt.yourdomain.com"
//#define MQTT_HOST			"10.0.1.3"
#define MQTT_PORT			1883
#define MQTT_BUF_SIZE		2048
#define MQTT_KEEPALIVE		60	 /*second*/

#define MQTT_CLIENT_ID		"ESP_%08X"
#define MQTT_USER			"esp8266"
#define MQTT_PASS			"chah5Kai"

//#define DEFAULT_SECURITY	1		// 1 for ssl, 0 for none
#define DEFAULT_SECURITY	0		// 1 for ssl, 0 for none

#define MQTT_RECONNECT_TIMEOUT 	5	/*second*/

#ifdef AP
#define QUEUE_BUFFER_SIZE		 		4096
#else
#ifdef IMPULSE
#define QUEUE_BUFFER_SIZE				4096	//20480	// larger queue for impulse meters
#else
#define QUEUE_BUFFER_SIZE				4096	//16384	// larger queue
#endif	// IMPULSE
#endif	// AP

//#ifndef MQTT_SSL_SIZE
//#define MQTT_SSL_SIZE					4096
//#endif

#define PROTOCOL_NAMEv31	/*MQTT version 3.1 compatible with Mosquitto v0.15*/
//PROTOCOL_NAMEv311			/*MQTT version 3.11 compatible with https://eclipse.org/paho/clients/testing/*/

// WIRELESS
#ifndef KEY
#define KEY { 0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6, 0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c }
#endif
//#define STA_SSID "Slux"
//#define STA_PASS "w1reless"
#define STA_SSID "Loppen Public"
#define STA_PASS ""
#define STA_FALLBACK_SSID "stofferFon"
#define STA_FALLBACK_PASS "w1reless"

#ifdef IMPULSE
#define AP_SSID	"EL_%s"
#else
#define AP_SSID	"KAM_%07u"
#endif
#ifndef AP_PASSWORD
#define AP_PASSWORD	"aabbccddeeff"
#endif
#define AP_NETWORK		"10.0.5.0"
#define AP_MESH_SSID	"mesh-%08x"
#define STA_TYPE 		AUTH_WPA_WPA2_PSK

#endif

