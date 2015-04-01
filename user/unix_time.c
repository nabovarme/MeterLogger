//#include "ets_sys.h"
//#include "driver/uart.h"
#include "osapi.h"
//#include "wifi.h"
//#include "config.h"
#include "debug.h"
//#include "gpio.h"
#include "user_interface.h"
//#include "mem.h"
#include "c_types.h"
#include "unix_time.h"

uint64 unix_time = 0;
bool unix_time_mutex = false;

//uint64 
static volatile os_timer_t overflow_timer;
static volatile os_timer_t ntp_update_timer;

ICACHE_FLASH_ATTR void overflow_timerfunc(void *arg) {
	while (unix_time_mutex) {
		// do nothing
	}
	unix_time_mutex = true;			// set mutex
	unix_time++;
	unix_time_mutex = false;		// free mutex
}

ICACHE_FLASH_ATTR void ntp_update_timer_func(void *arg) {
	INFO("ntp_update_timer\n\r");
	ntp_get_time();
}

ICACHE_FLASH_ATTR void init_unix_time(void) {
	uint32 ntp_update_interval;
	ntp_get_time();
	
	// start unix time second incrementer
    os_timer_disarm(&overflow_timer);
    os_timer_setfn(&overflow_timer, (os_timer_func_t *)overflow_timerfunc, NULL);
    os_timer_arm(&overflow_timer, 1000, 1);		// every second

	// start ntp update timer
	ntp_update_interval = ((rand() % 3600) + 3600) * 1000;	// every hour + random time < one hour
    os_timer_disarm(&ntp_update_timer);
    os_timer_setfn(&ntp_update_timer, (os_timer_func_t *)ntp_update_timer_func, NULL);
    os_timer_arm(&ntp_update_timer, ntp_update_interval, 1);
}

ICACHE_FLASH_ATTR uint64 get_unix_time(void) {
	uint64 current_unix_time;

	while (unix_time_mutex) {
		// do nothing
	}
	
	unix_time_mutex = true;			// set mutex
	current_unix_time = unix_time;
	unix_time_mutex = false;		// free mutex

	return current_unix_time;
}

// callback for ntp to set unix_time
ICACHE_FLASH_ATTR void set_unix_time(uint64 current_unix_time) {
	//uint64 last_system_time_delta;
	while (unix_time_mutex) {
		// do nothing
	}

	unix_time_mutex = true;			// set mutex
	unix_time = current_unix_time;
	unix_time_mutex = false;		// free mutex
	
}
