#ifndef EXCEPTION_HANDLER_H
#define EXCEPTION_HANDLER_H

#include <esp8266.h>
#include "xtensa/corebits.h"

// ============================================================================
// FLASH STORAGE ALLOCATION (Physical Memory Map Partition)
// ============================================================================
#define CRASH_FLASH_START_SEC       0xF8    // Start Sector (248 @ 0xF8000)
#define CRASH_FLASH_TOTAL_BYTES     0x4000  // 16 KB total reserved partition size

// ============================================================================
// RAM CAPTURE LIMITS (Volatile Memory Limits)
// ============================================================================
#define CRASH_RAM_STACK_READ_MAX    2048    // Max RAM bytes to read from SP (2 KB)

#define CRASH_LOG_MAGIC             0xDEADBEEF

// Xtensa Exception Frame pushed onto stack by SDK/HAL
struct XTensa_exception_frame_s {
	uint32_t pc;
	uint32_t ps;
	uint32_t sar;
	uint32_t vpri;
	uint32_t a0;
	uint32_t a[14]; // Registers A2..A15
	uint32_t litbase;
	uint32_t sr176;
	uint32_t sr208;
	uint32_t a1;    // Populated with true interrupted SP
	uint32_t reason;
	uint32_t excvaddr;
};

// Binary header persisted at the start of flash partition (0xF8000)
typedef struct __attribute__((packed)) {
	uint32_t magic;
	uint32_t build_timestamp;
	struct XTensa_exception_frame_s regs;
	uint32_t interrupted_sp;
	uint32_t stack_dump_len;
} crash_log_header_t;

ICACHE_FLASH_ATTR void exception_handler_init(void);

#endif // EXCEPTION_HANDLER_H
