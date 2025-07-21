#include <esp8266.h>
#include <string.h>

//#include "mqtt.h"
#include "config.h"
#include "user_config.h"
#include "debug.h"
#include "wifi.h"
#include "httpd.h"
#include "captdns.h"
#include "unix_time.h"
#include "cron/cron.h"
#include "led.h"
#include "watchdog.h"
#include "tinyprintf.h"
#include "utils.h"

static os_timer_t system_restart_defered_timer;

// crc table
#ifdef ESP_CONST_DATA
ESP_CONST_DATA
const uint32_t 
#else
const uint16_t
#endif
ccit_crc16_table[256] = {
	0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
	0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef,
	0x1231, 0x0210, 0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6,
	0x9339, 0x8318, 0xb37b, 0xa35a, 0xd3bd, 0xc39c, 0xf3ff, 0xe3de,
	0x2462, 0x3443, 0x0420, 0x1401, 0x64e6, 0x74c7, 0x44a4, 0x5485,
	0xa56a, 0xb54b, 0x8528, 0x9509, 0xe5ee, 0xf5cf, 0xc5ac, 0xd58d,
	0x3653, 0x2672, 0x1611, 0x0630, 0x76d7, 0x66f6, 0x5695, 0x46b4,
	0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d, 0xc7bc,
	0x48c4, 0x58e5, 0x6886, 0x78a7, 0x0840, 0x1861, 0x2802, 0x3823,
	0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b,
	0x5af5, 0x4ad4, 0x7ab7, 0x6a96, 0x1a71, 0x0a50, 0x3a33, 0x2a12,
	0xdbfd, 0xcbdc, 0xfbbf, 0xeb9e, 0x9b79, 0x8b58, 0xbb3b, 0xab1a,
	0x6ca6, 0x7c87, 0x4ce4, 0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41,
	0xedae, 0xfd8f, 0xcdec, 0xddcd, 0xad2a, 0xbd0b, 0x8d68, 0x9d49,
	0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70,
	0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a, 0x9f59, 0x8f78,
	0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e, 0xe16f,
	0x1080, 0x00a1, 0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067,
	0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c, 0xe37f, 0xf35e,
	0x02b1, 0x1290, 0x22f3, 0x32d2, 0x4235, 0x5214, 0x6277, 0x7256,
	0xb5ea, 0xa5cb, 0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d,
	0x34e2, 0x24c3, 0x14a0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
	0xa7db, 0xb7fa, 0x8799, 0x97b8, 0xe75f, 0xf77e, 0xc71d, 0xd73c,
	0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657, 0x7676, 0x4615, 0x5634,
	0xd94c, 0xc96d, 0xf90e, 0xe92f, 0x99c8, 0x89e9, 0xb98a, 0xa9ab,
	0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1, 0x3882, 0x28a3,
	0xcb7d, 0xdb5c, 0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a,
	0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1, 0x1ad0, 0x2ab3, 0x3a92,
	0xfd2e, 0xed0f, 0xdd6c, 0xcd4d, 0xbdaa, 0xad8b, 0x9de8, 0x8dc9,
	0x7c26, 0x6c07, 0x5c64, 0x4c45, 0x3ca2, 0x2c83, 0x1ce0, 0x0cc1,
	0xef1f, 0xff3e, 0xcf5d, 0xdf7c, 0xaf9b, 0xbfba, 0x8fd9, 0x9ff8,
	0x6e17, 0x7e36, 0x4e55, 0x5e74, 0x2e93, 0x3eb2, 0x0ed1, 0x1ef0
};

// CRC-CCITT (XModem)
ICACHE_FLASH_ATTR
uint16_t ccit_crc16(uint16_t crc16, uint8_t *data_p, unsigned int length) {
	while (length--) {
		crc16 = (crc16 << 8) ^ ccit_crc16_table[((crc16 >> 8) ^ *data_p++) & 0x00FF];
	}
	return crc16;
}

ICACHE_FLASH_ATTR
unsigned int decimal_number_length(int n) {
	int digits;
	
	digits = n < 0;	//count "minus"
	do {
		digits++;
	} while (n /= 10);
	
	return digits;
}

ICACHE_FLASH_ATTR
int int_pow(int x, int y) {
	int i;
	int result;
	
	if (y == 0) {
		return 1;
	}
	
	result = x;
	for (i = 1; i < y; i++) {
		result *= x;
	}
	return result;
}

ICACHE_FLASH_ATTR
int query_string_escape(char *str, size_t size) {
	char *p;
	int len = strlen(str);

	// Escape '&' as "%26"
	for (p = str; (p = strchr(p, '&')) != NULL; ) {
		// Check if we have space for 2 extra chars ("%26" replaces "&")
		if (len + 2 >= size) {
			return -1;  // Not enough space
		}
		memmove(p + 3, p + 1, len - (p - str));	// Shift the rest forward (+1 for '\0')
		memcpy(p, "%26", 3);					// Insert "%26"
		len += 2;								// Two extra chars added
		p += 3;									// Skip past the inserted "%26"
	}

	// Escape '=' as "%3D"
	for (p = str; (p = strchr(p, '=')) != NULL; ) {
		if (len + 2 >= size) {
			return -1;  // Not enough space
		}
		memmove(p + 3, p + 1, len - (p - str));
		memcpy(p, "%3D", 3);
		len += 2;
		p += 3;
	}

	return len;
}

ICACHE_FLASH_ATTR
int query_string_unescape(char *str) {
	char *p;
	int len;
	
	// replace "%26" with "&";
	len = strlen(str);
	for (p = str; (p = strstr(p, "%26")); ++p) {
		memmove(p + 1, p + 3, len - (p - str) + 1);
		memcpy(p, "&", 1);
	}
	
	// and "%3d" with "=";
	len = strlen(str);
	for (p = str; (p = strstr(p, "%3d")); ++p) {
		memmove(p + 1, p + 3, len - (p - str) + 1);
		memcpy(p, "=", 1);
	}
	return strlen(str);
}

ICACHE_FLASH_ATTR
size_t spi_flash_size() {					// returns the flash chip's size, in BYTES
	uint32_t id = spi_flash_get_id();
	uint8_t manufacturer_id = id & 0xff;
//	uint8_t type_id = (id >> 8) & 0xff;		// not relevant for size calculation
	uint8_t size_id = (id >> 16) & 0xff;	// lucky for us, WinBond ID's their chips as a form that lets us calculate the size
	if (manufacturer_id == 0xef) {
		return 1 << size_id;
	}
	else if (manufacturer_id == 0xe0) {		// LG Semi (Goldstar)
		return 1 << size_id;
	}
	else if (manufacturer_id == 0x1c) {		// Mitsubishi (https://www.mikrocontroller.net/attachment/39268/jep106k.pdf)
		return 1 << size_id;
	}
	else if (manufacturer_id == 0x20) {		// XMC - Wuhan Xinxin Semiconductor Manufacturing Corp
		return 1 << size_id;
	}
	else if (manufacturer_id == 0x85) {		// GTE (https://www.mikrocontroller.net/attachment/39268/jep106k.pdf)
		return 1 << size_id;
	}
	else if (manufacturer_id == 0xc8) {		// Apple Computer (https://www.mikrocontroller.net/attachment/39268/jep106k.pdf)
		return 1 << size_id;
	}
	else {
		// could not identify chip
		return 0;
	}
}

ICACHE_FLASH_ATTR void static system_restart_defered_timer_func(void *arg) {
	os_timer_disarm(&system_restart_defered_timer);
	system_restart();
#ifdef DEBUG
	printf("...restarting now\n\r");
#endif
}

ICACHE_FLASH_ATTR
void system_restart_defered() {
	httpdStop();		// stop http configuration server
	captdnsStop();		// stop captive dns

	wifi_destroy();
	set_my_auto_connect(false);
	wifi_station_disconnect();
	wifi_set_opmode_current(NULL_MODE);
	led_destroy();	// stop led blinking timers if its running
#ifndef IMPULSE
#ifndef NO_CRON
	cron_destroy();
#endif	// NO_CRON
#endif	// IMPULSE
	destroy_unix_time();
	stop_watchdog();
		
	ETS_UART_INTR_DISABLE();

	os_timer_disarm(&system_restart_defered_timer);
	os_timer_setfn(&system_restart_defered_timer, system_restart_defered_timer_func, NULL);
	os_timer_arm(&system_restart_defered_timer, 16000, 0);
#ifdef DEBUG
	printf("going to restart in 16 sec...\n\r");
#endif
}
