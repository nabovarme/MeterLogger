#include <esp8266.h>
#include "driver/uart.h"
#include "user_main.h"

static os_timer_t the_timer;
unsigned int i = 0;

static void ICACHE_FLASH_ATTR the_timer_func(void *arg) {
	uint32_t free_heap_0, free_heap_1;
	if (i % 2) {
		free_heap_0 = system_get_free_heap_size();
		printf("system_get_free_heap_size before NULL_MODE change: %u\n\r", free_heap_0);
		wifi_set_opmode_current(NULL_MODE);
		free_heap_1 = system_get_free_heap_size();
		printf("system_get_free_heap_size after NULL_MODE change: %u\n\r", free_heap_1);
		printf("diff: %dB\n\r", free_heap_0 - free_heap_1);
	}
	else {
		free_heap_0 = system_get_free_heap_size();
		printf("system_get_free_heap_size before STATION_MODE change: %u\n\r", free_heap_0);
		wifi_set_opmode_current(STATION_MODE);
		free_heap_1 = system_get_free_heap_size();
		printf("system_get_free_heap_size after STATION_MODE change: %u\n\r", free_heap_1);
		printf("diff: %dB\n\r", free_heap_0 - free_heap_1);
	}
	i++;
	printf("\n\r");
}

ICACHE_FLASH_ATTR void user_init(void) {
	
	system_update_cpu_freq(160);
	uart_init(115200, 115200);
	system_set_os_print(1);

	printf("\n\r");
	printf("SDK version: %s\n\r", system_get_sdk_version());

	// dont enable wireless before we have configured ssid
//	wifi_set_opmode_current(NULL_MODE);
	wifi_station_disconnect();
	// disale auto connect, we handle reconnect with this event handler
	wifi_station_set_auto_connect(0);
	wifi_station_set_reconnect_policy(0);

	// do everything else in system_init_done
	system_init_done_cb(&system_init_done);
}

ICACHE_FLASH_ATTR void system_init_done(void) {
	os_timer_disarm(&the_timer);
	os_timer_setfn(&the_timer, (os_timer_func_t *)the_timer_func, NULL);
	os_timer_arm(&the_timer, 1000, 1);

}
