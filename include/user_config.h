#ifndef _USER_CONFIG_H_
#define _USER_CONFIG_H_

//#define PRINTF_DEBUG

//#define CFG_HOLDER	0x00FF55A4	/* Change this value to load default configurations */
#define CFG_LOCATION	0x76	/* Please don't change or if you know what you doing */
//#define MQTT_CA_FLASH_SECTOR	0x5c

#define STACK_TRACE_N				0x4000
#define STACK_TRACE_SEC				0x80

#ifdef IMPULSE
#ifndef FLOW_METER	// electricity meter
#define IMPULSE_EDGE_TO_EDGE_TIME_MIN	(80)	// mS
#define IMPULSE_EDGE_TO_EDGE_TIME_MAX	(120)	// mS
#else				// analog water meter
#define IMPULSE_EDGE_TO_EDGE_TIME_MIN	(0)	// dont use time filter for analog meters
#define IMPULSE_EDGE_TO_EDGE_TIME_MAX	(0)	// dont use time filter for analog meters
#endif	// FLOW_METER
#endif	// IMPULSE

//#define MQTT_SSL_ENABLE
//#define CLIENT_SSL_ENABLE

/*DEFAULT CONFIGURATIONS*/

#define SSID_LENGTH         32
#define SSID_ESCAPED_LENGTH (SSID_LENGTH * 3)  // & encoded as %26 takes up three times more space

#define AP_SSID_LENGTH		32

#ifndef DEFAULT_METER_SERIAL
#define DEFAULT_METER_SERIAL	"9999999"
#endif

// MQTT
#define MQTT_TOPIC_L (128)
#define MQTT_MESSAGE_L (512)

#define MQTT_HOST			"nabovarme.meterlogger.net" //or "mqtt.yourdomain.com"
//#define MQTT_HOST			"10.0.1.3"
#define MQTT_PORT			1883
#define MQTT_BUF_SIZE		2048
#define MQTT_KEEPALIVE		60	 /*second*/

#define MQTT_CLIENT_ID		"ML_%08X"
#define MQTT_USER			"esp8266"
#define MQTT_PASS			"chah5Kai"

//#define DEFAULT_SECURITY	1		// 1 for ssl, 0 for none
#define DEFAULT_SECURITY	0		// 1 for ssl, 0 for none

#define MQTT_RECONNECT_TIMEOUT 	5	/*second*/

#ifdef AP
#ifdef DEBUG_NO_METER
#define QUEUE_BUFFER_SIZE		 		4096
#else
#define QUEUE_BUFFER_SIZE		 		8192
#endif	// DEBUG_NO_METER
#else
#ifdef IMPULSE
#define QUEUE_BUFFER_SIZE				20480	// larger queue for impulse meters
#else
#define QUEUE_BUFFER_SIZE				16384	// larger queue
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

#define STA_SSID			"Loppen Public"
#define STA_PASS			""
#define STA_TYPE			AUTH_OPEN
#define STA_FALLBACK_SSID	"stofferFon"
#define STA_FALLBACK_PASS	"w1reless"

#ifdef IMPULSE
#ifndef FLOW_METER
#define AP_SSID				"EL_%s"
#else
#define AP_SSID				"WATER_%s"
#endif	// FLOW_METER
#else
#define AP_SSID				"KAM_%07u"
#endif
#ifndef AP_PASSWORD
#define AP_PASSWORD			"aabbccddeeff"
#endif
#define AP_TYPE				AUTH_WPA_WPA2_PSK

#define AP_NETWORK			"10.0.5.0"
#define AP_MESH_SSID		"mesh-%s"
#define AP_MESH_PASS		"w1reless"
#define AP_MESH_TYPE		AUTH_WPA_WPA2_PSK

#endif

