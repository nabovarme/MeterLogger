#include <esp8266.h>
#include "debug.h"
#include "led.h"
#include "led_data.h"

static os_timer_t led_blinker_timer;
static os_timer_t led_single_blink_off_timer;

static os_timer_t led_double_blink_timer;
static os_timer_t led_sub_pattern_timer;

#ifdef DEBUG
static os_timer_t led_data_timer;
#endif

static volatile bool led_disabled = true;
static volatile bool led_blinker_timer_running = false;
static volatile bool led_double_blinker_timer_running = false;

static uint8_t led_sub_pattern_state = 0;

ICACHE_FLASH_ATTR void static led_blinker_timer_func(void *arg) {
	if (led_blinker_timer_running == false || led_disabled) {
		// stop blinking if asked to stop via state variable led_blinker_timer_running
		os_timer_disarm(&led_blinker_timer);
		return;
	}	

	// do blinky stuff
	if (GPIO_REG_READ(GPIO_OUT_ADDRESS) & BIT2) {
		led_on();
	}
	else {
		led_off();
	}
}

ICACHE_FLASH_ATTR void static led_single_blink_off_timer_func(void *arg) {
	led_off();
}
	
ICACHE_FLASH_ATTR void static led_double_blink_timer_func(void *arg) {
	// blink fast two times
	if (led_double_blinker_timer_running == false || led_disabled) {
		// stop blinking if asked to stop via state variable led_double_blinker_timer_running
		os_timer_disarm(&led_double_blink_timer);
		return;
	}	

	led_off();
	
	switch (led_sub_pattern_state) {
		case 0:
			led_on();

			led_sub_pattern_state++;			
			os_timer_disarm(&led_sub_pattern_timer);
			os_timer_setfn(&led_sub_pattern_timer, (os_timer_func_t *)led_double_blink_timer_func, NULL);
			os_timer_arm(&led_sub_pattern_timer, 100, 0);
			break;
		case 1:
			led_off();

			led_sub_pattern_state++;
			os_timer_disarm(&led_sub_pattern_timer);
			os_timer_setfn(&led_sub_pattern_timer, (os_timer_func_t *)led_double_blink_timer_func, NULL);
			os_timer_arm(&led_sub_pattern_timer, 100, 0);
			break;
		case 2:
			led_on();

			led_sub_pattern_state++;
			os_timer_disarm(&led_sub_pattern_timer);
			os_timer_setfn(&led_sub_pattern_timer, (os_timer_func_t *)led_double_blink_timer_func, NULL);
			os_timer_arm(&led_sub_pattern_timer, 100, 0);
			break;
		case 3:
			led_off();

			os_timer_disarm(&led_sub_pattern_timer);
			led_sub_pattern_state = 0;
			break;
	}
}

ICACHE_FLASH_ATTR void static led_data_timer_func(void *arg) {
	led_send_string("Hello");
}

ICACHE_FLASH_ATTR void led_init(void) {
	//Set GPIO2 to output mode
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO2_U, FUNC_GPIO2);
	
	// Set GPIO2 to HIGH (turn blue led off)
	gpio_output_set(BIT2, 0, BIT2, 0);
	
	led_disabled = false;

#ifdef DEBUG
	os_timer_disarm(&led_data_timer);
	os_timer_setfn(&led_data_timer, (os_timer_func_t *)led_data_timer_func, NULL);
	os_timer_arm(&led_data_timer, 20000, 1);
#endif
}

ICACHE_FLASH_ATTR void led_on(void) {
	//Set GPIO2 to LOW
	gpio_output_set(0, BIT2, BIT2, 0);
}

ICACHE_FLASH_ATTR void led_off(void) {
	
	// Set GPIO2 to HIGH
	gpio_output_set(BIT2, 0, BIT2, 0);
}

ICACHE_FLASH_ATTR void led_blink(void) {
	if (led_disabled) {
		return;
	}
	
	led_on();
	os_timer_disarm(&led_single_blink_off_timer);
	os_timer_setfn(&led_single_blink_off_timer, (os_timer_func_t *)led_single_blink_off_timer_func, NULL);
	os_timer_arm(&led_single_blink_off_timer, 1000, 0);
}

ICACHE_FLASH_ATTR void led_blink_short(void) {
	if (led_disabled) {
		return;
	}
	
	led_on();
	os_timer_disarm(&led_single_blink_off_timer);
	os_timer_setfn(&led_single_blink_off_timer, (os_timer_func_t *)led_single_blink_off_timer_func, NULL);
	os_timer_arm(&led_single_blink_off_timer, 100, 0);
}

ICACHE_FLASH_ATTR void led_pattern_a(void) {
	if (led_disabled) {
		return;
	}
	
	os_timer_disarm(&led_blinker_timer);
	os_timer_setfn(&led_blinker_timer, (os_timer_func_t *)led_blinker_timer_func, NULL);
	led_blinker_timer_running = true;	// state variable to control stopping after pattern is done
	os_timer_arm(&led_blinker_timer, 1000, 1);
}

ICACHE_FLASH_ATTR void led_pattern_b(void) {
	if (led_disabled) {
		return;
	}
	
	os_timer_disarm(&led_blinker_timer);
	os_timer_setfn(&led_blinker_timer, (os_timer_func_t *)led_blinker_timer_func, NULL);
	led_blinker_timer_running = true;	// state variable to control stopping after pattern is done
	os_timer_arm(&led_blinker_timer, 200, 1);
}

ICACHE_FLASH_ATTR void led_pattern_c(void) {
	// blink fast two times every 5th second
	if (led_disabled) {
		return;
	}
	
	os_timer_disarm(&led_double_blink_timer);
	os_timer_setfn(&led_double_blink_timer, (os_timer_func_t *)led_double_blink_timer_func, NULL);
	led_double_blinker_timer_running = true;	// state variable to control stopping after pattern is done
	os_timer_arm(&led_double_blink_timer, 3000, 1);
}

ICACHE_FLASH_ATTR void led_stop_pattern(void) {
	led_blinker_timer_running = false;
	led_double_blinker_timer_running = false;
	led_off();
}

ICACHE_FLASH_ATTR void led_destroy(void) {
	led_disabled = true;
	led_stop_pattern();
}
