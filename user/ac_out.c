#include "ets_sys.h"
#include "osapi.h"
#include "user_interface.h"

#include "led.h"
#include "ac_out.h"

static volatile os_timer_t ac_test_timer;
static volatile os_timer_t ac_out_off_timer;


ICACHE_FLASH_ATTR
void ac_out_init() {
	//Set GPIO14 and GPIO15 to output mode
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTMS_U, FUNC_GPIO14);
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDO_U, FUNC_GPIO15);
}

ICACHE_FLASH_ATTR
void ac_test_timer_func(void *arg) {
	// do blinky stuff
	if (GPIO_REG_READ(GPIO_OUT_ADDRESS) & BIT14) {
		//Set GPI14 to LOW
		gpio_output_set(0, BIT14, BIT14, 0);
		led_pattern_b();
	}
	else {
		//Set GPI14 to HIGH
		gpio_output_set(BIT14, 0, BIT14, 0);
		led_pattern_a();
	}
	
	if (GPIO_REG_READ(GPIO_OUT_ADDRESS) & BIT15) {
		//Set GPI15 to LOW
		gpio_output_set(0, BIT15, BIT15, 0);
	}
	else {
		//Set GPI15 to HIGH
		gpio_output_set(BIT15, 0, BIT15, 0);
	}
}

ICACHE_FLASH_ATTR
void ac_test() {
#ifdef DEBUG
	os_printf("\n\rac test on\n\r");
#endif
	led_pattern_a();
	
	// set GPIO14 high and GPIO15 low
	gpio_output_set(BIT14, 0, BIT14, 0);
	gpio_output_set(0, BIT15, BIT15, 0);
	
	os_timer_disarm(&ac_test_timer);
	os_timer_setfn(&ac_test_timer, (os_timer_func_t *)ac_test_timer_func, NULL);
	os_timer_arm(&ac_test_timer, 120000, 1);
}

ICACHE_FLASH_ATTR
void ac_motor_valve_open() {
#ifdef DEBUG
	os_printf("\n\rac 1 on\n\r");
#endif
	ac_off();
	led_pattern_a();
	
	//Set GPI14 to HIGH
	gpio_output_set(BIT14, 0, BIT14, 0);
}

ICACHE_FLASH_ATTR
void ac_motor_valve_close() {
#ifdef DEBUG
	os_printf("\n\rac 2 on\n\r");
#endif
	ac_off();
	led_pattern_b();
	
	//Set GPI15 to HIGH
	gpio_output_set(BIT15, 0, BIT15, 0);
}

ICACHE_FLASH_ATTR
void ac_off() {
#ifdef DEBUG
	os_printf("\n\rac 1 and 2 off\n\r");
#endif
	
	os_timer_disarm(&ac_test_timer);
	//Set GPI14 to LOW
	gpio_output_set(0, BIT14, BIT14, 0);
	
	//Set GPI15 to LOW
	gpio_output_set(0, BIT15, BIT15, 0);

	led_off();
	led_stop_pattern();
}

