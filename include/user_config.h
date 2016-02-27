#ifndef _USER_CONFIG_H_
#define _USER_CONFIG_H_


#define CFG_HOLDER	0x00FF55A4	/* Change this value to load default configurations */
#define CFG_LOCATION	0x3C	/* Please don't change or if you know what you doing */
#define CLIENT_SSL_ENABLE

/*DEFAULT CONFIGURATIONS*/

#define AP_SSID	"KAM_%07u"
#define AP_PASSWORD	"aabbccddeeff"

#define STA_SSID "Slux"
#define STA_PASS "w1reless"
//#define STA_SSID "Loppen Public"
//#define STA_PASS ""
#define STA_FALLBACK_SSID "stofferFon"
#define STA_FALLBACK_PASS "w1reless"
#define STA_TYPE AUTH_WPA2_PSK

#endif	// _USER_CONFIG_H_