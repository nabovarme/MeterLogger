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
