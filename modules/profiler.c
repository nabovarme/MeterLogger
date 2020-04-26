#include <esp8266.h>
#include "profiler.h"

#ifdef DEBUG_PROFILER

uint32_t t0;
uint32_t overflows = 0;

void profile_func_enter(void *func, void *caller) {
	t0 = system_get_time();
}

void profile_func_exit(void *func, void *caller) {
	uint32_t t_diff = system_get_time() - t0;
	if (t_diff > 10000) {
		// took longer than 10 mS
#ifdef DEBUG
		printf("x %p %p %u\n", func, caller, t_diff);
#endif
		overflows++;
	}
}

uint32_t profile_get() {
	return overflows;
}

#endif	// DEBUG_PROFILER
