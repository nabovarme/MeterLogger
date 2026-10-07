#include "ets_sys.h"
#include "osapi.h"
#include "c_types.h"

static void dump_hex(const void *ptr, size_t len) {
	const uint8_t *b = (const uint8_t *)ptr;
	size_t i;
	for (i = 0; i < len; i++) {
		os_printf("%02x ", b[i]);
		if ((i + 1) % 16 == 0) os_printf("\n");
	}
	os_printf("\n");
}

void ICACHE_RAM_ATTR __wrap_cnx_csa_fn(void *arg) {
	os_printf("cnx_csa_fn called with arg: %p\n", arg);
	if (arg != NULL) {
		dump_hex(arg, 32);
	}
}
