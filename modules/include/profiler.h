#ifndef PROFILER_H
#define PROFILER_H

#ifdef DEBUG_PROFILER
ICACHE_FLASH_ATTR
uint32_t profile_get();

#endif	// DEBUG_PROFILER

#endif	// PROFILER_H