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
uint64 system_time = 0;
bool unix_time_mutex = false;

//uint64 
static volatile os_timer_t overflow_timer;

ICACHE_FLASH_ATTR void overflow_timerfunc(void *arg) {
	INFO("overflow_timer\n\r");
	while (unix_time_mutex) {
		// do nothing
	}
	unix_time_mutex = true;			// set mutex
	//INFO("inside mutex, system_time: %lld\n\r");
	system_time += 4295;
	unix_time_mutex = false;		// free mutex
}

ICACHE_FLASH_ATTR void init_unix_time(void) {
	ntp_get_time();
	
	//start timer
    //Disarm timer
    os_timer_disarm(&overflow_timer);

    //Setup timer
    os_timer_setfn(&overflow_timer, (os_timer_func_t *)overflow_timerfunc, NULL);

    //Arm the timer
    //&sample_timer is the pointer
    //1000 is the fire time in ms
    //0 for once and 1 for repeating
    os_timer_arm(&overflow_timer, 0xffffffff, 1);
}

ICACHE_FLASH_ATTR uint64 get_unix_time(void) {
	uint64 current_unix_time;

	while (unix_time_mutex) {
		// do nothing
	}
	
	unix_time_mutex = true;			// set mutex
	current_unix_time = unix_time + system_time + (system_get_time() / 1000000);
	unix_time_mutex = false;		// free mutex

	return current_unix_time;
}

ICACHE_FLASH_ATTR void set_unix_time(uint64 current_unix_time) {
	while (unix_time_mutex) {
		// do nothing
	}

	unix_time_mutex = true;			// set mutex
	unix_time = current_unix_time;
	system_time = system_get_time() / 1000000; //64bit danger
	unix_time_mutex = false;		// free mutex
	
}
