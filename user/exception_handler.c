#include <esp8266.h>
#include "exception_handler.h"
#include "xtensa/corebits.h"

static void exception_handler(struct XTensa_exception_frame_s *frame) {
  //Save the extra registers the Xtensa HAL doesn't save
//  extern void gdbstub_save_extra_sfrs_for_exception();
//  gdbstub_save_extra_sfrs_for_exception();
  //Copy registers the Xtensa HAL did save to gdbstub_savedRegs
//  os_memcpy(&gdbstub_savedRegs, frame, 19*4);
  //Credits go to Cesanta for this trick. A1 seems to be destroyed, but because it
  //has a fixed offset from the address of the passed frame, we can recover it.
  //gdbstub_savedRegs.a1=(uint32_t)frame+EXCEPTION_GDB_SP_OFFSET;
//  gdbstub_savedRegs.a1=(uint32_t)frame;

//  ets_wdt_disable();
//  printReason();
	printf("XXX exception!\n\r");
//  ets_wdt_enable();
//  while(1) ;
}

void exception_handler_init() {
	_xtos_set_exception_handler(EXCCAUSE_ILLEGAL, exception_handler);
	_xtos_set_exception_handler(EXCCAUSE_SYSCALL, exception_handler);
	_xtos_set_exception_handler(EXCCAUSE_INSTR_ERROR, exception_handler);
	_xtos_set_exception_handler(EXCCAUSE_LOAD_STORE_ERROR, exception_handler);
	_xtos_set_exception_handler(EXCCAUSE_DIVIDE_BY_ZERO, exception_handler);
	_xtos_set_exception_handler(EXCCAUSE_UNALIGNED, exception_handler);
	_xtos_set_exception_handler(EXCCAUSE_INSTR_DATA_ERROR, exception_handler);
	_xtos_set_exception_handler(EXCCAUSE_LOAD_STORE_DATA_ERROR, exception_handler);
	_xtos_set_exception_handler(EXCCAUSE_INSTR_ADDR_ERROR, exception_handler);
	_xtos_set_exception_handler(EXCCAUSE_LOAD_STORE_ADDR_ERROR, exception_handler);
	_xtos_set_exception_handler(EXCCAUSE_INSTR_PROHIBITED, exception_handler);
	_xtos_set_exception_handler(EXCCAUSE_LOAD_PROHIBITED, exception_handler);
	_xtos_set_exception_handler(EXCCAUSE_STORE_PROHIBITED, exception_handler);
}