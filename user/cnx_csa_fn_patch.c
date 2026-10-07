#include "ets_sys.h"
#include "osapi.h"

// Dump raw bytes of a memory buffer safely
static void dump_hex(const void *ptr, size_t len) {
	const uint8_t *b = (const uint8_t *)ptr;
	for (size_t i = 0; i < len; i++) {
		os_printf("%02x ", b[i]);
		if ((i + 1) % 16 == 0) os_printf("\n");
	}
	os_printf("\n");
}

void ICACHE_RAM_ATTR __wrap_cnx_csa_fn(void *arg) {
	os_printf("cnx_csa_fn called with arg ptr: %p\n", arg);
	
	if (arg != NULL) {
		os_printf("arg payload (first 32 bytes):\n");
		dump_hex(arg, 32);
	}
	
	return;
}
