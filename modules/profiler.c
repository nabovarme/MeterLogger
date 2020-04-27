#include <esp8266.h>
#include "profiler.h"

#ifdef DEBUG_PROFILER

uint32_t t0;
uint32_t function_time_limit_exeeded_count = 0;

__attribute__((no_instrument_function))
void __cyg_profile_func_enter(void *func, void *caller) {
	t0 = system_get_time();
}

__attribute__((no_instrument_function))
void __cyg_profile_func_exit(void *func, void *caller) {
	uint32_t t_diff = system_get_time() - t0;
	if (t_diff > 10000) {
		// took longer than 10 mS
#ifdef DEBUG
		printf("x %p %p %u\n", func, caller, t_diff);
#endif
		function_time_limit_exeeded_count++;
	}
}

ICACHE_FLASH_ATTR
uint32_t profile_get() {
#ifdef DEBUG
		printf("function time limit exceeded: %u\n", function_time_limit_exeeded_count);
#endif
	return function_time_limit_exeeded_count;
}

#endif	// DEBUG_PROFILER
