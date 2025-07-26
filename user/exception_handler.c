#include <esp8266.h>
#include "tinyprintf.h"
#include "exception_handler.h"
#include "xtensa/corebits.h"
#include "user_config.h"
#include "user_interface.h"
#include "watchdog.h"

#define STACK_TRACE_BUFFER_N 128
#define MAX_CALL_STACK_DEPTH 32

struct XTensa_exception_frame_s saved_regs;

struct stack_trace_context_t {
	size_t flash_index;
	char buffer[STACK_TRACE_BUFFER_N];
	size_t buffer_index;
} stack_trace_context;

char stack_trace_buffer[STACK_TRACE_BUFFER_N];

extern void _xtos_set_exception_handler(int cause, void (exhandler)(struct XTensa_exception_frame_s *frame));
extern void ets_wdt_disable();
extern void ets_wdt_enable();
extern void save_extra_sfrs_for_exception();

ICACHE_FLASH_ATTR
static unsigned int getaregval(int reg) {
	if (reg == 0) return saved_regs.a0;
	if (reg == 1) return saved_regs.a1;
	return saved_regs.a[reg - 2];
}

ICACHE_FLASH_ATTR
static void stack_trace_append(char *c) {
	unsigned int len;

	len = strlen(c);
	if (len == 0) return;

	if ((stack_trace_context.flash_index * STACK_TRACE_BUFFER_N) > STACK_TRACE_N) return;

	if (len + stack_trace_context.buffer_index < STACK_TRACE_BUFFER_N) {
		memcpy(stack_trace_context.buffer + stack_trace_context.buffer_index, c, len);
		stack_trace_context.buffer_index += len;
	} else {
		memcpy(stack_trace_context.buffer + stack_trace_context.buffer_index, c, STACK_TRACE_BUFFER_N - stack_trace_context.buffer_index);
		spi_flash_write((STACK_TRACE_SEC * SPI_FLASH_SEC_SIZE) + (stack_trace_context.flash_index * STACK_TRACE_BUFFER_N),
						(uint32_t *)stack_trace_context.buffer,
						STACK_TRACE_BUFFER_N);
		c += STACK_TRACE_BUFFER_N - stack_trace_context.buffer_index;
		len = strlen(c);
		stack_trace_context.flash_index++;
		stack_trace_context.buffer_index = 0;

		while (len > STACK_TRACE_BUFFER_N) {
			memcpy(stack_trace_context.buffer, c, STACK_TRACE_BUFFER_N);
			spi_flash_write((STACK_TRACE_SEC * SPI_FLASH_SEC_SIZE) + (stack_trace_context.flash_index * STACK_TRACE_BUFFER_N),
							(uint32_t *)stack_trace_context.buffer,
							STACK_TRACE_BUFFER_N);
			c += STACK_TRACE_BUFFER_N;
			len -= STACK_TRACE_BUFFER_N;
			stack_trace_context.flash_index++;
		}

		if (len) {
			memset(stack_trace_context.buffer, 0, STACK_TRACE_BUFFER_N);
			memcpy(stack_trace_context.buffer, c, len);
			stack_trace_context.buffer_index = len;
		}
	}
}

ICACHE_FLASH_ATTR
static void stack_trace_last() {
	spi_flash_write((STACK_TRACE_SEC * SPI_FLASH_SEC_SIZE) + (stack_trace_context.flash_index * STACK_TRACE_BUFFER_N),
					(uint32_t *)stack_trace_context.buffer,
					STACK_TRACE_BUFFER_N);
}

ICACHE_FLASH_ATTR
static void capture_call_stack() {
	uint32_t fp;
	uint32_t depth;
	uint32_t prev_fp;
	uint32_t ret_addr;
	uint32_t local1, local2, local3, local4;
	uint32_t stack_limit = 0x40000000; // ESP8266 DRAM upper bound approx

	fp = getaregval(1);  /* current frame pointer (A1) */
	depth = 0;

	tfp_snprintf(stack_trace_buffer, STACK_TRACE_BUFFER_N, "\nCall stack trace:\n");
	stack_trace_append(stack_trace_buffer);

	while (depth < MAX_CALL_STACK_DEPTH) {
		// Make sure we can safely read 8 bytes (fp, ret_addr)
		if (fp < 0x3FFAE000 || fp + 8 >= stack_limit) {
			tfp_snprintf(stack_trace_buffer, STACK_TRACE_BUFFER_N,
						 "  Stopped: invalid frame pointer 0x%08x\n", fp);
			stack_trace_append(stack_trace_buffer);
#ifdef DEBUG
			printf("  Stopped: invalid frame pointer 0x%08x\n", fp);
#endif
			break;
		}

		prev_fp = *((uint32_t *)fp);
		ret_addr = *((uint32_t *)(fp + 4));

		if (ret_addr == 0 || ret_addr == 0xFFFFFFFF) {
			break;
		}

		// Read 4 locals if safe
		if (fp + 20 < stack_limit) {
			local1 = *((uint32_t *)(fp + 8));
			local2 = *((uint32_t *)(fp + 12));
			local3 = *((uint32_t *)(fp + 16));
			local4 = *((uint32_t *)(fp + 20));
		} else {
			local1 = local2 = local3 = local4 = 0;
		}

		tfp_snprintf(stack_trace_buffer, STACK_TRACE_BUFFER_N,
					 " #%02d: fp=0x%08x ret=0x%08x locals: %08x %08x %08x %08x\n",
					 depth, fp, ret_addr, local1, local2, local3, local4);
		stack_trace_append(stack_trace_buffer);

#ifdef DEBUG
		printf(" #%02d: fp=0x%08x ret=0x%08x locals: %08x %08x %08x %08x\n",
			   depth, fp, ret_addr, local1, local2, local3, local4);
#endif

		if (prev_fp <= fp || prev_fp >= stack_limit) {
			break;
		}

		fp = prev_fp;
		depth++;
	}

	// Print addresses for addr2line symbolication
	tfp_snprintf(stack_trace_buffer, STACK_TRACE_BUFFER_N, "\nAddresses for addr2line:\n");
	stack_trace_append(stack_trace_buffer);

	fp = getaregval(1);
	depth = 0;
	while (depth < MAX_CALL_STACK_DEPTH) {
		if (fp < 0x3FFAE000 || fp + 8 >= stack_limit) break;

		prev_fp = *((uint32_t *)fp);
		ret_addr = *((uint32_t *)(fp + 4));

		if (ret_addr == 0 || ret_addr == 0xFFFFFFFF) break;

		tfp_snprintf(stack_trace_buffer, STACK_TRACE_BUFFER_N, "0x%08x ", ret_addr);
		stack_trace_append(stack_trace_buffer);

		if (prev_fp <= fp || prev_fp >= stack_limit) break;

		fp = prev_fp;
		depth++;
	}
	tfp_snprintf(stack_trace_buffer, STACK_TRACE_BUFFER_N, "\n");
	stack_trace_append(stack_trace_buffer);
	stack_trace_last();
	

#ifdef DEBUG
	printf("\nAddresses for addr2line:\n");
	fp = getaregval(1);
	depth = 0;
	while (depth < MAX_CALL_STACK_DEPTH) {
		if (fp < 0x3FFAE000 || fp >= stack_limit) break;

		prev_fp = *((uint32_t *)fp);
		ret_addr = *((uint32_t *)(fp + 4));
		if (ret_addr == 0 || ret_addr == 0xFFFFFFFF) break;

		printf("0x%08x ", ret_addr);

		if (prev_fp <= fp || prev_fp >= stack_limit) break;

		fp = prev_fp;
		depth++;
	}
	printf("\n");
#endif
}

ICACHE_FLASH_ATTR
static void print_stack(uint32_t start) {
	uint32_t pos;
	uint32_t *values;
	uint32_t dram_end;
	int looks_like_stack_frame;

#ifdef DEBUG
	printf("\nStack dump:\n");
#endif

	/* ESP8266 DRAM upper bound (0x40000000 is the start of MMIO) */
	dram_end = 0x3FFFFC00;

	tfp_snprintf(stack_trace_buffer, STACK_TRACE_BUFFER_N, "\nStack dump:\n");
	stack_trace_append(stack_trace_buffer);

	/* Sanity check: if SP invalid, bail */
	if (start < 0x3FFAE000 || start >= dram_end) {
		tfp_snprintf(stack_trace_buffer, STACK_TRACE_BUFFER_N,
					 "Invalid stack pointer: 0x%08x\n", start);
		stack_trace_append(stack_trace_buffer);
#ifdef DEBUG
		printf("Invalid stack pointer: 0x%08x\n", start);
#endif
		stack_trace_last();
		return;
	}

	/* Dump stack contents in 16-byte aligned blocks */
	for (pos = start; pos < dram_end; pos += 0x10) {
		values = (uint32_t *)pos;

		/* Detect a potential stack frame (heuristic) */
		looks_like_stack_frame = (values[2] == pos + 0x10);

#ifdef DEBUG
		printf("%08lx:  %08lx %08lx %08lx %08lx %c\n",
			(long unsigned int)pos,
			(long unsigned int)values[0],
			(long unsigned int)values[1],
			(long unsigned int)values[2],
			(long unsigned int)values[3],
			looks_like_stack_frame ? '<' : ' ');
#endif
		tfp_snprintf(stack_trace_buffer, STACK_TRACE_BUFFER_N, "%08lx:  %08lx %08lx %08lx %08lx %c\n",
			(long unsigned int)pos,
			(long unsigned int)values[0],
			(long unsigned int)values[1],
			(long unsigned int)values[2],
			(long unsigned int)values[3],
			looks_like_stack_frame ? '<' : ' ');
		stack_trace_append(stack_trace_buffer);
	}

	tfp_snprintf(stack_trace_buffer, STACK_TRACE_BUFFER_N, "\n");
	stack_trace_append(stack_trace_buffer);
	
#ifdef DEBUG
	printf("\n");
#endif

	stack_trace_last();
}

ICACHE_FLASH_ATTR
static void print_reason() {
	unsigned int i;
	unsigned int r;
	struct XTensa_exception_frame_s *reg = &saved_regs;
#ifdef DEBUG
	printf("\n\n***** Fatal exception %ld\n", (long int)reg->reason);
#endif
	tfp_snprintf(stack_trace_buffer, STACK_TRACE_BUFFER_N, "\n\n***** Fatal exception %ld\n", (long int)reg->reason);
	stack_trace_append(stack_trace_buffer);

#ifdef DEBUG
	printf("pc=0x%08lx sp=0x%08lx excvaddr=0x%08lx\n",
		   (long unsigned int)reg->pc,
		   (long unsigned int)reg->a1,
		   (long unsigned int)reg->excvaddr);
#endif
	tfp_snprintf(stack_trace_buffer, STACK_TRACE_BUFFER_N, "pc=0x%08lx sp=0x%08lx excvaddr=0x%08lx\n",
				 (long unsigned int)reg->pc,
				 (long unsigned int)reg->a1,
				 (long unsigned int)reg->excvaddr);
	stack_trace_append(stack_trace_buffer);

#ifdef DEBUG
	printf("ps=0x%08lx sar=0x%08lx vpri=0x%08lx\n",
		   (long unsigned int)reg->ps,
		   (long unsigned int)reg->sar,
		   (long unsigned int)reg->vpri);
#endif
	tfp_snprintf(stack_trace_buffer, STACK_TRACE_BUFFER_N, "ps=0x%08lx sar=0x%08lx vpri=0x%08lx\n",
				 (long unsigned int)reg->ps,
				 (long unsigned int)reg->sar,
				 (long unsigned int)reg->vpri);
	stack_trace_append(stack_trace_buffer);

	for (i = 0; i < 16; i++) {
		r = getaregval(i);
#ifdef DEBUG
		printf("r%02u: 0x%08x=%10u ", i, r, r);
#endif
		tfp_snprintf(stack_trace_buffer, STACK_TRACE_BUFFER_N, "r%02d: 0x%08x=%10d ", i, r, r);
		stack_trace_append(stack_trace_buffer);
		if (i % 3 == 2) {
#ifdef DEBUG
			printf("\n");
#endif
			tfp_snprintf(stack_trace_buffer, STACK_TRACE_BUFFER_N, "\n");
			stack_trace_append(stack_trace_buffer);
		}
	}
#ifdef DEBUG
	printf("\n");
#endif
	tfp_snprintf(stack_trace_buffer, STACK_TRACE_BUFFER_N, "\n");
	stack_trace_append(stack_trace_buffer);
	stack_trace_last();
}

ICACHE_FLASH_ATTR
static void exception_handler(struct XTensa_exception_frame_s *frame) {
	uint8_t i;

	save_extra_sfrs_for_exception();
	memcpy(&saved_regs, frame, 19 * 4);
	saved_regs.a1 = (uint32_t)frame;

	wifi_station_disconnect();
	wifi_set_opmode(NULL_MODE);  // disable both STA & AP
	wifi_set_sleep_type(MODEM_SLEEP_T);
	// Optionally disable RF completely:
	system_phy_set_powerup_option(3);  // keep RF off after reset

	stop_watchdog();
	system_soft_wdt_stop();
	ets_wdt_disable();
	
	for (i = 0; i * SPI_FLASH_SEC_SIZE < STACK_TRACE_N; i++) {
		spi_flash_erase_sector(STACK_TRACE_SEC + i);
	}

	print_reason();
	print_stack(getaregval(1));
	capture_call_stack();

	// Give serial output some time to flush
	os_delay_us(2000000);
	ets_wdt_enable();

	while (1) {
		// wait for watchdog to bite
	}
}

ICACHE_FLASH_ATTR
void exception_handler_init() {
	unsigned int i;
	int exno[] = {EXCCAUSE_ILLEGAL, EXCCAUSE_INSTR_ERROR, EXCCAUSE_DIVIDE_BY_ZERO, EXCCAUSE_UNALIGNED,
				  EXCCAUSE_INSTR_DATA_ERROR, EXCCAUSE_LOAD_STORE_DATA_ERROR, EXCCAUSE_INSTR_ADDR_ERROR,
				  EXCCAUSE_LOAD_STORE_ADDR_ERROR, EXCCAUSE_INSTR_PROHIBITED, EXCCAUSE_LOAD_PROHIBITED, EXCCAUSE_STORE_PROHIBITED};

	memset(stack_trace_buffer, 0, STACK_TRACE_BUFFER_N);
	memset(stack_trace_context.buffer, 0, STACK_TRACE_BUFFER_N);
	stack_trace_context.flash_index = 0;
	stack_trace_context.buffer_index = 0;

	for (i = 0; i < (sizeof(exno) / sizeof(exno[0])); i++) {
		_xtos_set_exception_handler(exno[i], exception_handler);
	}
}

