//#include "ets_sys.h"
//#include "driver/uart.h"
#include "osapi.h"
//#include "wifi.h"
//#include "config.h"
#include "debug.h"
//#include "gpio.h"
#include "user_interface.h"
#include "mem.h"
#include "c_types.h"
#include "unix_time.h"

uint64 unix_time = 0;
bool unix_time_mutex = false;

ICACHE_FLASH_ATTR void init_unix_time(void) {
	// init sntp
	ip_addr_t *addr = (ip_addr_t *)os_zalloc(sizeof(ip_addr_t));
	sntp_setservername(0, "dk.pool.ntp.org"); // set server 0 by domain name
	sntp_setservername(1, "us.pool.ntp.org"); // set server 1 by domain name
	//ipaddr_aton("210.72.145.44", addr);
	//sntp_setserver(2, addr); // set server 2 by IP address
	sntp_set_timezone(1);	// need support for dayligh saving
	sntp_init();
	os_free(addr);
}

ICACHE_FLASH_ATTR uint32 get_unix_time(void) {
	uint32 current_unix_time;

	current_unix_time = sntp_get_current_timestamp();
	if (current_unix_time > 0) {			// if initialized
		current_unix_time -= (1 * 60 * 60);	// bug in sdk - no correction for time zone
	}

	return current_unix_time;
}
