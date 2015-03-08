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
//uint64 system_time = 0;
//uint64 system_time_delta;
//uint64 unix_time_delta;
bool unix_time_mutex = false;

//uint64 
static volatile os_timer_t overflow_timer;
static volatile os_timer_t ntp_update_timer;

ICACHE_FLASH_ATTR void overflow_timerfunc(void *arg) {
	//INFO("overflow_timer\n\r");
	while (unix_time_mutex) {
		// do nothing
	}
	unix_time_mutex = true;			// set mutex
	unix_time++;
	unix_time_mutex = false;		// free mutex
}

ICACHE_FLASH_ATTR void ntp_update_timerfunc(void *arg) {
	INFO("ntp_update_timer\n\r");
	ntp_get_time();
}

ICACHE_FLASH_ATTR void init_unix_time(void) {
	//system_time_delta = system_get_time() / 1000000;
	//INFO("init_unix_time: system_time_delta: %llu\n\r", system_time_delta);
	ntp_get_time();
	
	// start unix time second incrementer
    os_timer_disarm(&overflow_timer);
    os_timer_setfn(&overflow_timer, (os_timer_func_t *)overflow_timerfunc, NULL);
    os_timer_arm(&overflow_timer, 1000, 1);		// every second

	// start ntp update timer
    os_timer_disarm(&ntp_update_timer);
    os_timer_setfn(&ntp_update_timer, (os_timer_func_t *)ntp_update_timerfunc, NULL);
    os_timer_arm(&ntp_update_timer, 10000, 1);		// every 10 second
}

ICACHE_FLASH_ATTR uint64 get_unix_time(void) {
	uint64 current_unix_time;

	while (unix_time_mutex) {
		// do nothing
	}
	
	unix_time_mutex = true;			// set mutex
	INFO("unix_time: %llu\n\r", unix_time);
	current_unix_time = unix_time;
	unix_time_mutex = false;		// free mutex

	return current_unix_time;
}

ICACHE_FLASH_ATTR void set_unix_time(uint64 current_unix_time) {
	//uint64 last_system_time_delta;
	while (unix_time_mutex) {
		// do nothing
	}

	unix_time_mutex = true;			// set mutex
	//INFO("current_unix_time: %llu\n\r", current_unix_time);
	
	//INFO("system_time_delta: %llu\n\r", system_time_delta);
	//last_system_time_delta = system_time_delta;
	//system_time_delta = (system_get_time() / 1000000) - system_time_delta;
	//INFO("system_time_delta: %llu\n\r", system_time_delta);
	
	
	unix_time = current_unix_time;
	INFO("unix_time: %llu\n\r", unix_time);
	//system_time = system_get_time() / 1000000;
	//INFO("system_time: %llu\n\r", system_time);
	unix_time_mutex = false;		// free mutex
	
}
