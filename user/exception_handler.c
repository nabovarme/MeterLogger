// ============================================================================
// ESP8266 Call0 Crash Log Binary Format
// ============================================================================
// The crash log is stored as a raw binary payload. All data is written in 
// Little-Endian byte order (standard for the Xtensa architecture) and aligned 
// to 32-bit (4-byte) boundaries.
//
// The payload consists of a fixed 116-byte header followed immediately by a 
// variable-length raw RAM stack dump.
//
// 1. High-Level Memory Layout
// ----------------------------------------------------------------------------
// [ 0x00 - 0x73 ] (116 bytes) : crash_log_header_t (Magic, build info, CPU regs)
// [ 0x74 - END  ] (Variable)  : Raw DRAM stack payload (Length: stack_dump_len)
//
// 2. Header Structure (crash_log_header_t)
// ----------------------------------------------------------------------------
// Offset  | Size     | Type        | Field Name     | Description
// 0x00    | 4 bytes  | uint32_t    | magic          | Always 0xDEADBEEF
// 0x04    | 4 bytes  | uint32_t    | build_timestamp| Epoch timestamp/build ID
// 0x08    | 100 bytes| struct      | regs           | Xtensa Exception Frame (below)
// 0x6C    | 4 bytes  | uint32_t    | interrupted_sp | True SP (A1) of the task
// 0x70    | 4 bytes  | uint32_t    | stack_dump_len | Length of the stack dump
//
// 3. Xtensa Exception Frame Breakdown (regs)
// ----------------------------------------------------------------------------
// Offset  | Size     | Type        | Field Name     | Description
// 0x08    | 4 bytes  | uint32_t    | pc             | Crash instruction address
// 0x0C    | 4 bytes  | uint32_t    | ps             | Processor Status register
// 0x10    | 4 bytes  | uint32_t    | sar            | Shift Amount Register
// 0x14    | 4 bytes  | uint32_t    | vpri           | Virtual Priority register
// 0x18    | 4 bytes  | uint32_t    | a0             | Register A0 (Return Address)
// 0x1C    | 56 bytes | uint32_t[14]| a[2..15]       | General purpose A2-A15
// 0x54    | 4 bytes  | uint32_t    | litbase        | Literal Base Register
// 0x58    | 4 bytes  | uint32_t    | sr176          | Special Function Register 176
// 0x5C    | 4 bytes  | uint32_t    | sr208          | Special Function Register 208
// 0x60    | 4 bytes  | uint32_t    | a1             | Reg A1 (Same as interrupted_sp)
// 0x64    | 4 bytes  | uint32_t    | reason         | Exception Cause (e.g. 28)
// 0x68    | 4 bytes  | uint32_t    | excvaddr       | Exception Virtual Address
//
// 4. Stack Memory Payload
// ----------------------------------------------------------------------------
// Starts immediately after the header at byte offset 116 (0x74).
// A raw binary copy of the RAM stack (0x3FFF.... memory range). The first 
// 4 bytes correspond to the memory located at `interrupted_sp`.
// 
// Important Parsing Notes for Unwinders:
// - Word Alignment: All stack reads/writes on the ESP8266 are 32-bit aligned.
//   `stack_dump_len` will always be a multiple of 4 bytes.
// - Missing A0 offset in Leaf Functions: Under the Xtensa Call0 ABI, if the 
//   crashing function is a leaf function (does not call other functions), it 
//   will not save A0 into this stack memory payload. An unwinder must check 
//   the `a0` register field (offset 24) to step out of the first frame.
// ============================================================================

#include <esp8266.h>
#include "tinyprintf.h"
#include "exception_handler.h"
#include "xtensa/corebits.h"
#include "user_interface.h"
#include "watchdog.h"
#include "user_config.h"

// Saved register frame
struct XTensa_exception_frame_s saved_regs;

extern void _xtos_set_exception_handler(int cause, void (exhandler)(struct XTensa_exception_frame_s *frame));
extern void save_extra_sfrs_for_exception(void);
extern void ets_wdt_disable(void);
extern void ets_wdt_enable(void);

// Feed hardware WDT directly via register write to prevent NMI resets during flash operations
static inline void feed_hardware_wdt(void) {
	WRITE_PERI_REG(0x60000914, 0x73);
}

// NOTE: The exception handler MUST NOT be marked with ICACHE_FLASH_ATTR because
// reading from Flash Cache during flash sector erase/write operations causes fatal collisions.
static void exception_handler(struct XTensa_exception_frame_s *frame) {
	uint32_t interrupted_sp;
	uint32_t stack_top_boundary;
	uint32_t stack_dump_len;
	uint32_t flash_addr;
	uint32_t sector_count;
	uint32_t i;
	crash_log_header_t header;

	// 1. Save extra Special Function Registers (SFRs) using the assembly stub
	save_extra_sfrs_for_exception();

	// 2. Copy the CPU register frame (the first 19 words captured by the HAL)
	memcpy(&saved_regs, frame, 19 * sizeof(uint32_t));

	// 3. Compute the true interrupted stack pointer (A1)
	// The exception frame sits on top of the target stack upon entry (100 bytes / 0x64).
	interrupted_sp = (uint32_t)frame + sizeof(struct XTensa_exception_frame_s);
	saved_regs.a1 = interrupted_sp;

	// 4. Disable watchdog timers
	stop_watchdog();
	system_soft_wdt_stop();
	ets_wdt_disable();
	feed_hardware_wdt();

	// 5. Safely calculate the stack dump length from DRAM
	stack_top_boundary = 0x3FFFFFB0; // Upper boundary of RAM stack
	if (interrupted_sp >= 0x3FFE8000 && interrupted_sp < stack_top_boundary) {
		stack_dump_len = stack_top_boundary - interrupted_sp;
		if (stack_dump_len > CRASH_RAM_STACK_READ_MAX) {
			stack_dump_len = CRASH_RAM_STACK_READ_MAX;
		}
	} else {
		// Corrupted or out-of-bounds Stack Pointer
		stack_dump_len = 0;
	}

	// Align length to 32-bit (4-byte) word boundary for SPI Flash write requirements
	stack_dump_len &= ~3;

	// 6. Populate the crash log binary header
	header.magic = CRASH_LOG_MAGIC;
	header.build_timestamp = 0x20261022; // Can be linked to the build system timestamp
	memcpy(&header.regs, &saved_regs, sizeof(struct XTensa_exception_frame_s));
	header.interrupted_sp = interrupted_sp;
	header.stack_dump_len = stack_dump_len;

	// 7. Erase the crash log flash partition (0xF8000 - 0xFBFFF) safely
	flash_addr = CRASH_FLASH_START_SEC * SPI_FLASH_SEC_SIZE;
	sector_count = (CRASH_FLASH_TOTAL_BYTES + SPI_FLASH_SEC_SIZE - 1) / SPI_FLASH_SEC_SIZE;

	for (i = 0; i < sector_count; i++) {
		feed_hardware_wdt();
		spi_flash_erase_sector(CRASH_FLASH_START_SEC + i);
	}

	// 8. Write the binary crash header to flash
	feed_hardware_wdt();
	spi_flash_write(flash_addr, (uint32_t *)&header, sizeof(crash_log_header_t));
	flash_addr += sizeof(crash_log_header_t);

	// 9. Write the raw binary stack memory dump directly following the header
	if (stack_dump_len > 0) {
		feed_hardware_wdt();
		spi_flash_write(flash_addr, (uint32_t *)interrupted_sp, stack_dump_len);
	}

	feed_hardware_wdt();

#ifdef DEBUG
	printf("\n*** FATAL EXCEPTION Cause=%lu PC=0x%08lx Interrupted SP=0x%08lx EXCVADDR=0x%08lx ***\n",
		   (unsigned long)saved_regs.reason,
		   (unsigned long)saved_regs.pc,
		   (unsigned long)interrupted_sp,
		   (unsigned long)saved_regs.excvaddr);
#endif

	// 10. Flush UART output and reboot
	for (i = 0; i < 100; i++) {
		feed_hardware_wdt();
		os_delay_us(10000); // 1-second total delay
	}

	system_restart();

	// Fallback loop for hardware WDT reset if system_restart() fails
	ets_wdt_enable();
	while (1) {
		// Wait for hardware watchdog reset
	}
}

ICACHE_FLASH_ATTR
void exception_handler_init(void) {
	unsigned int i;
	int exno[] = {
		EXCCAUSE_ILLEGAL,
		EXCCAUSE_INSTR_ERROR,
		EXCCAUSE_DIVIDE_BY_ZERO,
		EXCCAUSE_UNALIGNED,
		EXCCAUSE_INSTR_DATA_ERROR,
		EXCCAUSE_LOAD_STORE_DATA_ERROR,
		EXCCAUSE_INSTR_ADDR_ERROR,
		EXCCAUSE_LOAD_STORE_ADDR_ERROR,
		EXCCAUSE_INSTR_PROHIBITED,
		EXCCAUSE_LOAD_PROHIBITED,
		EXCCAUSE_STORE_PROHIBITED
	};

	for (i = 0; i < (sizeof(exno) / sizeof(exno[0])); i++) {
		_xtos_set_exception_handler(exno[i], exception_handler);
	}
}
