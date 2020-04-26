#ifndef PROFILER_H
#define PROFILER_H

#ifdef DEBUG_PROFILER
#define PROFILE_FUNC_ENTER()	profile_func_enter(__builtin_return_address, NULL)
#define PROFILE_FUNC_EXIT()		profile_func_exit(__builtin_return_address, NULL)

void profile_func_enter(void *func, void *caller);
void profile_func_exit(void *func, void *caller);
uint32_t profile_get();

#else
#define PROFILE_FUNC_ENTER
#define PROFILE_FUNC_EXIT
#endif

#endif