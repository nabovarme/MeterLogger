#ifndef LED_H_
#define LED_H_

#include <c_types.h>

ICACHE_FLASH_ATTR void led_init(void);
ICACHE_FLASH_ATTR void led_on(void);
ICACHE_FLASH_ATTR void led_off(void);
ICACHE_FLASH_ATTR void led_blink(void);
ICACHE_FLASH_ATTR void led_blink_short(void);
ICACHE_FLASH_ATTR void led_pattern_a(void);
ICACHE_FLASH_ATTR void led_pattern_b(void);
ICACHE_FLASH_ATTR void led_pattern_c(void);
ICACHE_FLASH_ATTR void led_pattern_d(void);
ICACHE_FLASH_ATTR void led_stop_pattern(void);
ICACHE_FLASH_ATTR void led_destroy(void);

#ifdef ENABLE_LED_OPTICAL
// Optical Manchester-encoded log output on GPIO2
ICACHE_FLASH_ATTR bool led_optical_send(const uint8_t *data, uint8_t len);
ICACHE_FLASH_ATTR bool led_optical_printf(const char *fmt, ...);
ICACHE_FLASH_ATTR bool led_optical_is_busy(void);
#endif

#endif /* LED_H_ */
