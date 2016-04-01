#include <esp8266.h>
#include "debug.h"
#include "led.h"

static os_timer_t led_blinker_timer;

ICACHE_FLASH_ATTR void static led_blinker_timer_func(void *arg) {
	// do blinky stuff
	if (GPIO_REG_READ(GPIO_OUT_ADDRESS) & BIT2) {
		led_on();
	}
	else {
		led_off();
	}
}
	
ICACHE_FLASH_ATTR void led_init(void) {
	//Set GPIO2 to output mode
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO2_U, FUNC_GPIO2);
	
	// Set GPIO2 to HIGH (turn blue led off)
	gpio_output_set(BIT2, 0, BIT2, 0);
}

ICACHE_FLASH_ATTR void led_on(void) {
	//Set GPIO2 to LOW
	gpio_output_set(0, BIT2, BIT2, 0);
}

ICACHE_FLASH_ATTR void led_off(void) {
	
	// Set GPIO2 to HIGH
	gpio_output_set(BIT2, 0, BIT2, 0);
}

ICACHE_FLASH_ATTR void led_pattern_a(void) {
	os_timer_disarm(&led_blinker_timer);
	os_timer_setfn(&led_blinker_timer, (os_timer_func_t *)led_blinker_timer_func, NULL);
	os_timer_arm(&led_blinker_timer, 1000, 1);
}

ICACHE_FLASH_ATTR void led_pattern_b(void) {
	os_timer_disarm(&led_blinker_timer);
	os_timer_setfn(&led_blinker_timer, (os_timer_func_t *)led_blinker_timer_func, NULL);
	os_timer_arm(&led_blinker_timer, 200, 1);
}

ICACHE_FLASH_ATTR void led_stop_pattern(void) {
	os_timer_disarm(&led_blinker_timer);
	led_off();
}

