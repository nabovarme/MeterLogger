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

// Reference to the original system_get_sdk_version function provided by SDK
extern const char * __real_system_get_sdk_version(void);
extern void __real_cnx_csa_fn(void *arg);

void ICACHE_RAM_ATTR __wrap_cnx_csa_fn(void *arg) {
	// Increment counter and flag for MQTT/system logging
	cnx_csa_call_count++;
	cnx_csa_new_call_flag = true;

	// Check if station interface is enabled
	uint8 mode = wifi_get_opmode();
	if (mode != STATION_MODE && mode != STATIONAP_MODE) {
		// Station disabled; ignore CSA frame
		return;
	}

	// Validate current connection status
	// If the station is scanning or disconnected, ignore incoming CSA commands
	enum station_status status = wifi_station_get_connect_status();
	if (status != STATION_GOT_IP && status != STATION_CONNECTED) {
		#ifdef DEBUG
		os_printf("cnx_csa_fn: Blocked unassociated/scanning CSA switch (status: %d)\n", status);
		#endif
		return;
	}

	// Option A: If you want to block ALL CSA execution permanently (100% immune to rogue CSAs):
	// return;

	// Option B: Allow valid CSA execution if station is currently associated to an AP
	__real_cnx_csa_fn(arg);
}

ICACHE_FLASH_ATTR
const char * __wrap_system_get_sdk_version(void) {
	static char patched_version[64];
	static bool is_cached = false;

	if (is_cached) {
		return patched_version;
	}

	const char *orig = __real_system_get_sdk_version();

	if (orig == NULL) {
		os_strcpy(patched_version, "unknown-patched");
		is_cached = true;
		return patched_version;
	}

	// Look for the starting parenthesis of the git commit hash e.g., "3.0.6-dev(072755c)"
	const char *paren = os_strchr(orig, '(');

	if (paren != NULL) {
		// Calculate length of version string prior to '('
		size_t prefix_len = paren - orig;

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
