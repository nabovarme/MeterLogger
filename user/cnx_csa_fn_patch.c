#include "ets_sys.h"
#include "osapi.h"
#include "c_types.h"
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

//#ifdef DEBUG
//static void dump_hex(const void *ptr, size_t len) {
//	const uint8_t *b = (const uint8_t *)ptr;
//	size_t i;
//	for (i = 0; i < len; i++) {
//		os_printf("%02x ", b[i]);
//		if ((i + 1) % 16 == 0) os_printf("\n");
//	}
//	os_printf("\n");
//}
//#endif

void ICACHE_RAM_ATTR __wrap_cnx_csa_fn(void *arg) {
	// Increment counter and flag for MQTT broadcast
	cnx_csa_call_count++;
	cnx_csa_new_call_flag = true;

//#ifdef DEBUG
//	os_printf("cnx_csa_fn intercepted! (Total calls: %u) arg: %p\n", cnx_csa_call_count, arg);
//	if (arg != NULL) {
//		dump_hex(arg, 32);
//	}
//#endif
	return;
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
