#include <esp8266.h>
#include "xtensa/corebits.h"

struct XTensa_exception_frame_s {
  uint32_t pc;
  uint32_t ps;
  uint32_t sar;
  uint32_t vpri;
  uint32_t a0;
  uint32_t a[14]; //a2..a15
  // The following are added manually by the exception code; the HAL doesn't set these on an exception.
  uint32_t litbase;
  uint32_t sr176;
  uint32_t sr208;
  uint32_t a1;
  uint32_t reason;
  uint32_t excvaddr;
};

extern void _xtos_set_exception_handler(int cause, void (exhandler)(struct XTensa_exception_frame_s *frame));

ICACHE_FLASH_ATTR
void exception_handler_init();
