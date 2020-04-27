#ifndef PROFILER_H
#define PROFILER_H

#ifdef DEBUG_PROFILER
ICACHE_FLASH_ATTR uint32_t profile_get_time();
ICACHE_FLASH_ATTR void *profile_get_func();

#endif	// DEBUG_PROFILER

#endif	// PROFILER_H