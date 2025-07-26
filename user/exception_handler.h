#include <esp8266.h>
#include "xtensa/corebits.h"

// Exception frame structure capturing CPU registers and exception info
struct XTensa_exception_frame_s {
	uint32_t pc;
	uint32_t ps;
	uint32_t sar;
	uint32_t vpri;
	uint32_t a0;
	uint32_t a[14];		 // a2..a15
	// Added manually by exception handler, not set by HAL
	uint32_t litbase;
	uint32_t sr176;
	uint32_t sr208;
	uint32_t a1;		// stack pointer
	uint32_t reason;	// exception cause
	uint32_t excvaddr;	// exception address (if applicable)
};

ICACHE_FLASH_ATTR void exception_handler_init();
