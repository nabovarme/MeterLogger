#include "ets_sys.h"
#include "osapi.h"
#include "c_types.h"
#include "user_interface.h"
#include <stdbool.h>

// Fallback macro definition if ICACHE_RAM_ATTR is not defined in SDK headers
#ifndef ICACHE_RAM_ATTR
#define ICACHE_RAM_ATTR __attribute__((section(".iram1.text")))
#endif

// Global tracking variables exposed to user_main.c
uint32_t cnx_csa_call_count = 0;
bool cnx_csa_new_call_flag = false;

// Internal scan state tracking
static volatile bool is_scanning = false;
static scan_done_cb_t user_scan_cb = NULL;

// References to real functions wrapped by GNU linker
extern const char * __real_system_get_sdk_version(void);
extern void __real_cnx_csa_fn(void *arg);
extern bool __real_wifi_station_scan(struct scan_config *config, scan_done_cb_t cb);

// Intercepted scan callback to clear the scanning flag
static void ICACHE_FLASH_ATTR intercepted_scan_cb(void *arg, STATUS status) {
	scan_done_cb_t orig_cb;

	is_scanning = false; // Clear flag when scan completes or fails
	
	orig_cb = user_scan_cb;
	user_scan_cb = NULL;

	if (orig_cb != NULL) {
		orig_cb(arg, status);
	}
}

// Intercept the scan call to set the flag and inject our callback
bool ICACHE_FLASH_ATTR __wrap_wifi_station_scan(struct scan_config *config, scan_done_cb_t cb) {
	bool res;
	
	user_scan_cb = cb;
	is_scanning = true; // Set flag before starting scan

	res = __real_wifi_station_scan(config, intercepted_scan_cb);
	if (!res) {
		// Scan failed to start, clear flag immediately
		is_scanning = false;
		user_scan_cb = NULL;
	}
	
	return res;
}

void ICACHE_RAM_ATTR __wrap_cnx_csa_fn(void *arg) {
	// Increment counter and flag for MQTT/system logging
	cnx_csa_call_count++;
	cnx_csa_new_call_flag = true;

	// If we are actively scanning, this CSA is almost certainly from a foreign AP on another channel.
	if (is_scanning) {
		#ifdef DEBUG
		os_printf("cnx_csa_fn: Blocked rogue CSA switch during active scan\n");
		#endif
		return;
	}

	// Allow valid CSA execution if we are just idling/connected normally
	__real_cnx_csa_fn(arg);
}

ICACHE_FLASH_ATTR
const char * __wrap_system_get_sdk_version(void) {
	static char patched_version[64];
	static bool is_cached = false;
	const char *orig;
	const char *paren;
	size_t prefix_len;

	if (is_cached) {
		return patched_version;
	}

	orig = __real_system_get_sdk_version();

	if (orig == NULL) {
		os_strcpy(patched_version, "unknown-patched");
		is_cached = true;
		return patched_version;
	}

	// Look for the starting parenthesis of the git commit hash e.g., "3.0.6-dev(072755c)"
	paren = os_strchr(orig, '(');

	if (paren != NULL) {
		// Calculate length of version string prior to '('
		prefix_len = (size_t)(paren - orig);

		if (prefix_len < sizeof(patched_version) - 16) {
			// Copy base version (e.g., "3.0.6-dev")
			os_memcpy(patched_version, orig, prefix_len);
			patched_version[prefix_len] = '\0';

			// Append "-patched" and the remaining string starting from "("
			os_sprintf(patched_version + prefix_len, "-patched%s", paren);
			is_cached = true;
			return patched_version;
		}
	} else {
		// Fallback: If no '(' exists (e.g., "3.0.6"), simply append "-patched" to the end
		os_sprintf(patched_version, "%s-patched", orig);
		is_cached = true;
		return patched_version;
	}

	// Safety fallback if buffer overflow protection triggers
	return orig;
}
