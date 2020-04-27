#include <esp8266.h>
#include "profiler.h"

#ifdef DEBUG_PROFILER

uint32_t t0;
uint32_t last_time = 0;
void *last_func = NULL;

__attribute__((no_instrument_function))
void __cyg_profile_func_enter(void *func, void *caller) {
	t0 = system_get_time();
}

__attribute__((no_instrument_function))
void __cyg_profile_func_exit(void *func, void *caller) {
	uint32_t t_diff = system_get_time() - t0;
	if (t_diff > last_time) {
		last_time = t_diff;
		last_func = func;
	}
}

ICACHE_FLASH_ATTR
uint32_t profile_get_time() {
	return last_time;
}

ICACHE_FLASH_ATTR
void *profile_get_func() {
	return last_func;
}

#endif	// DEBUG_PROFILER
