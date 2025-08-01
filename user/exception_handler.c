#include <esp8266.h>
#include "tinyprintf.h"
#include "exception_handler.h"
#include "xtensa/corebits.h"
#include "user_interface.h"
#include "watchdog.h"
#include "user_config.h"

#define STACK_TRACE_BUFFER_N		128

//The asm stub saves the Xtensa registers here when a debugging exception happens.
struct XTensa_exception_frame_s saved_regs;

// struct for buffered save log to flash
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

//Get the value of one of the A registers
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
	
	// only save up to STACK_TRACE_N bytes
	if ((stack_trace_context.flash_index * STACK_TRACE_BUFFER_N) > STACK_TRACE_N) return;
	
	if (len + stack_trace_context.buffer_index < STACK_TRACE_BUFFER_N) {
		// fill into buffer
		memcpy(stack_trace_context.buffer + stack_trace_context.buffer_index, c, len);
		stack_trace_context.buffer_index += len;
	}
	else {
		// data longer than buffer size, we need to split writes
		// data enough left to fill buffer
		memcpy(stack_trace_context.buffer + stack_trace_context.buffer_index, c, STACK_TRACE_BUFFER_N - stack_trace_context.buffer_index);
		spi_flash_write((STACK_TRACE_SEC * SPI_FLASH_SEC_SIZE) + (stack_trace_context.flash_index * STACK_TRACE_BUFFER_N),
						(uint32_t *)stack_trace_context.buffer,
						STACK_TRACE_BUFFER_N);
		c += STACK_TRACE_BUFFER_N - stack_trace_context.buffer_index;
		len = strlen(c);
		stack_trace_context.flash_index++;
		stack_trace_context.buffer_index = 0;
		
		// while enough data fill whole buffer
		while (len > STACK_TRACE_BUFFER_N) {
			c += STACK_TRACE_BUFFER_N;
			memcpy(stack_trace_context.buffer, c, STACK_TRACE_BUFFER_N);
			spi_flash_write((STACK_TRACE_SEC * SPI_FLASH_SEC_SIZE) + (stack_trace_context.flash_index * STACK_TRACE_BUFFER_N),
							(uint32_t *)stack_trace_context.buffer,
							STACK_TRACE_BUFFER_N);
			len -= STACK_TRACE_BUFFER_N;
			stack_trace_context.flash_index++;
		}
		
		// last data
		if (len) {
			memset(stack_trace_context.buffer, 0, STACK_TRACE_BUFFER_N);
			memcpy(stack_trace_context.buffer, c, strlen(c));
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
static void print_stack(uint32_t start, uint32_t end) {
	uint32_t pos;
	uint32_t *values;
	bool looks_like_stack_frame;
#ifdef DEBUG
	printf("\nStack dump:\n");
#endif	// DEBUG
	tfp_snprintf(stack_trace_buffer, STACK_TRACE_BUFFER_N, "\nStack dump:\n");
	stack_trace_append(stack_trace_buffer);

	for (pos = start; pos < end; pos += 0x10) {
		values = (uint32_t*)(pos);
		// rough indicator: stack frames usually have SP saved as the second word
		looks_like_stack_frame = (values[2] == pos + 0x10);
		
#ifdef DEBUG
		printf("%08lx:  %08lx %08lx %08lx %08lx %c\n",
			(long unsigned int)pos,
			(long unsigned int)values[0],
			(long unsigned int)values[1],
			(long unsigned int)values[2],
			(long unsigned int)values[3],
			looks_like_stack_frame ? '<' : ' ');
#endif	// DEBUG
		tfp_snprintf(stack_trace_buffer, STACK_TRACE_BUFFER_N, "%08lx:  %08lx %08lx %08lx %08lx %c\n",
			(long unsigned int)pos,
			(long unsigned int)values[0],
			(long unsigned int)values[1],
			(long unsigned int)values[2],
			(long unsigned int)values[3],
			looks_like_stack_frame ? '<' : ' ');
		stack_trace_append(stack_trace_buffer);
	}
#ifdef DEBUG
	printf("\n");
#endif	// DEBUG
	tfp_snprintf(stack_trace_buffer, STACK_TRACE_BUFFER_N, "\n");
	stack_trace_append(stack_trace_buffer);
	
	stack_trace_last();
}

// Print exception info to console
ICACHE_FLASH_ATTR
static void print_reason() {
	unsigned int i;
	unsigned int r;

	struct XTensa_exception_frame_s *reg = &saved_regs;
#ifdef DEBUG
	printf("\n\n***** Fatal exception %ld\n", (long int) reg->reason);
#endif	// DEBUG
	tfp_snprintf(stack_trace_buffer, STACK_TRACE_BUFFER_N, "\n\n***** Fatal exception %ld\n", (long int) reg->reason);
	stack_trace_append(stack_trace_buffer);

#ifdef DEBUG
	printf("pc=0x%08lx sp=0x%08lx excvaddr=0x%08lx\n",
		(long unsigned int)reg->pc,
		(long unsigned int)reg->a1,
		(long unsigned int)reg->excvaddr);
#endif	// DEBUG
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
#endif	// DEBUG
	tfp_snprintf(stack_trace_buffer, STACK_TRACE_BUFFER_N, "ps=0x%08lx sar=0x%08lx vpri=0x%08lx\n",
		(long unsigned int)reg->ps,
		(long unsigned int)reg->sar,
		(long unsigned int)reg->vpri);
	stack_trace_append(stack_trace_buffer);
	
	for (i = 0; i < 16; i++) {
		r = getaregval(i);
#ifdef DEBUG
		printf("r%02u: 0x%08x=%10u ", i, r, r);
#endif	// DEBUG
		tfp_snprintf(stack_trace_buffer, STACK_TRACE_BUFFER_N, "r%02d: 0x%08x=%10d ", i, r, r);
		stack_trace_append(stack_trace_buffer);
		if (i % 3 == 2) {
#ifdef DEBUG
			printf("\n");
#endif	// DEBUG
			tfp_snprintf(stack_trace_buffer, STACK_TRACE_BUFFER_N, "\n");
			stack_trace_append(stack_trace_buffer);
		}
	}
#ifdef DEBUG
	printf("\n");
#endif	// DEBUG
	tfp_snprintf(stack_trace_buffer, STACK_TRACE_BUFFER_N, "\n");
	stack_trace_append(stack_trace_buffer);
}

ICACHE_FLASH_ATTR
static void exception_handler(struct XTensa_exception_frame_s *frame) {
	uint8_t i;
	
	// Save the extra registers the Xtensa HAL doesn't save
	save_extra_sfrs_for_exception();
	// Copy registers the Xtensa HAL did save to saved_regs
	memcpy(&saved_regs, frame, 19*4);

	// Credits go to Cesanta for this trick. A1 seems to be destroyed, but because it
	// has a fixed offset from the address of the passed frame, we can recover it.
	// saved_regs.a1 = (uint32_t)frame + EXCEPTION_GDB_SP_OFFSET;

	saved_regs.a1 = (uint32_t)frame;

	wifi_station_disconnect();
	wifi_set_opmode(NULL_MODE);			// disable both STA & AP
	wifi_set_sleep_type(MODEM_SLEEP_T);
	system_phy_set_powerup_option(3);	// keep RF off after reset
	
	stop_watchdog();
	system_soft_wdt_stop();
	ets_wdt_disable();
	
	for (i = 0; i * SPI_FLASH_SEC_SIZE < STACK_TRACE_N; i++) {
		spi_flash_erase_sector(STACK_TRACE_SEC + i);
	}
	
	print_reason();

	//print_stack(reg->pc, sp, 0x3fffffb0);
	print_stack(getaregval(1), 0x3fffffb0);
	
	// Wait ~3 seconds so logs flush out (without WDT)
	os_delay_us(3000000);
	
	// Force reboot cleanly (no watchdog needed)
	system_restart();

	// in case it does not restart
	ets_wdt_enable();
	while(1) {
		// wait for watchdog to bite
	}
}

ICACHE_FLASH_ATTR
void exception_handler_init() {
	unsigned int i;
	int exno[] = {EXCCAUSE_ILLEGAL, EXCCAUSE_INSTR_ERROR, EXCCAUSE_DIVIDE_BY_ZERO, EXCCAUSE_UNALIGNED, 
				  EXCCAUSE_INSTR_DATA_ERROR, EXCCAUSE_LOAD_STORE_DATA_ERROR, EXCCAUSE_INSTR_ADDR_ERROR, 
				  EXCCAUSE_LOAD_STORE_ADDR_ERROR, EXCCAUSE_INSTR_PROHIBITED, EXCCAUSE_LOAD_PROHIBITED, EXCCAUSE_STORE_PROHIBITED};

	// initialize buffered save log to flash
	memset(stack_trace_buffer, 0, STACK_TRACE_BUFFER_N);
	memset(stack_trace_context.buffer, 0, STACK_TRACE_BUFFER_N);
	stack_trace_context.flash_index = 0;
	stack_trace_context.buffer_index = 0;

	for (i = 0; i < (sizeof(exno) / sizeof(exno[0])); i++) {
		_xtos_set_exception_handler(exno[i], exception_handler);
	}
}

