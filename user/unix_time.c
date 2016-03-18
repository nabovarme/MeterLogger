#include <esp8266.h>
#include "debug.h"
#include "unix_time.h"

static os_timer_t sntp_check_timer;

uint32_t init_time = 0;
uint32_t current_unix_time;
uint32_t offline_second_counter = 0;

uint64 boot_time;

static os_timer_t offline_second_counter_timer;

ICACHE_FLASH_ATTR void sntp_check_timer_func(void *arg) {
	current_unix_time = sntp_get_current_timestamp();
	
	if (current_unix_time == 0) {
		os_timer_disarm(&sntp_check_timer);
		os_timer_arm(&sntp_check_timer, 2000, 0);
	} else {
		os_timer_disarm(&sntp_check_timer);
		// save init time for use in uptime()
		if (init_time == 0) {		// only set init_time at boot
		    os_timer_disarm(&offline_second_counter_timer);		// stop offline second counter
			
			init_time = current_unix_time;
		}
	}
}

ICACHE_FLASH_ATTR void offline_second_counter_timer_func(void *arg) {
	offline_second_counter++;
}

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
	
	// start timer to make sure we go ntp reply
	os_timer_disarm(&sntp_check_timer);
	os_timer_setfn(&sntp_check_timer, (os_timer_func_t *)sntp_check_timer_func, NULL);
	os_timer_arm(&sntp_check_timer, 2000, 0);

	offline_second_counter = 0;
    os_timer_disarm(&offline_second_counter_timer);
    os_timer_setfn(&offline_second_counter_timer, (os_timer_func_t *)offline_second_counter_timer_func, NULL);
    os_timer_arm(&offline_second_counter_timer, 1000, 1);		// every seconds
}

ICACHE_FLASH_ATTR uint32_t get_unix_time(void) {
	current_unix_time = sntp_get_current_timestamp();
	if (current_unix_time > 0) {			// if initialized
		current_unix_time -= (1 * 60 * 60);	// bug in sdk - no correction for time zone
	}

	return current_unix_time;
}

ICACHE_FLASH_ATTR uint32_t uptime(void) {
	current_unix_time = sntp_get_current_timestamp();
	if (init_time == 0) {
		return offline_second_counter;
	}
	else {
		return current_unix_time - init_time + offline_second_counter;
	}
}
