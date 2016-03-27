#include <esp8266.h>

#include "led.h"
#include "ac_out.h"
#include "config.h"

static os_timer_t ac_test_timer;
static os_timer_t ac_out_off_timer;
static os_timer_t ac_pwm_timer;

unsigned int ac_pwm_duty_cycle;
typedef enum {ON, OFF} ac_pwm_state_t;
ac_pwm_state_t ac_pwm_state = OFF;


ICACHE_FLASH_ATTR
void ac_out_init() {
	//Set GPIO14 and GPIO15 to output mode
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTMS_U, FUNC_GPIO14);
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDO_U, FUNC_GPIO15);
}

ICACHE_FLASH_ATTR
void static ac_test_timer_func(void *arg) {
	// do blinky stuff
	if (GPIO_REG_READ(GPIO_OUT_ADDRESS) & BIT14) {
		//Set GPI14 to LOW
		gpio_output_set(0, BIT14, BIT14, 0);
#ifdef LED_ON_AC
		led_pattern_b();
#endif
	}
	else {
		//Set GPI14 to HIGH
		gpio_output_set(BIT14, 0, BIT14, 0);
#ifdef LED_ON_AC
		led_pattern_a();
#endif
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

ICACHE_FLASH_ATTR void static ac_out_off_timer_func(void *arg) {
#ifdef DEBUG
	os_printf("\n\rac 1 and 2 off\n\r");
#endif
	
	//Set GPI14 to LOW
	gpio_output_set(0, BIT14, BIT14, 0);
	
	//Set GPI15 to LOW
	gpio_output_set(0, BIT15, BIT15, 0);

	led_stop_pattern();
#ifdef LED_ON_AC
	led_off();
#endif
}

ICACHE_FLASH_ATTR void static ac_pwm_timer_func(void *arg) {
	// do ac 1 pwm
	if (ac_pwm_state == OFF) {
		ac_pwm_state = ON;
		if (ac_pwm_duty_cycle > 0) {
#ifdef LED_ON_AC
			led_on();
#endif
#ifdef THERMO_NO	
			//Set GPI14 to LOW
			gpio_output_set(0, BIT14, BIT14, 0);
#else	// THERMO_NC
			//Set GPI14 to HIGH
			gpio_output_set(BIT14, 0, BIT14, 0);
#endif
		}
		// reload timer
		os_timer_disarm(&ac_pwm_timer);
		os_timer_setfn(&ac_pwm_timer, (os_timer_func_t *)ac_pwm_timer_func, NULL);
		os_timer_arm(&ac_pwm_timer, ac_pwm_duty_cycle * 10, 1);		// pwm frequency 10 second
	}
	else if (ac_pwm_state == ON) {
		ac_pwm_state = OFF;
		if (ac_pwm_duty_cycle < 1000) {
#ifdef LED_ON_AC
			led_off();
#endif
#ifdef THERMO_NO
			//Set GPI14 to HIGHT
			gpio_output_set(BIT14, 0, BIT14, 0);
#else	// THERMO_NC
			//Set GPI14 to LOW
			gpio_output_set(0, BIT14, BIT14, 0);
#endif
		}
		// reload timer
		os_timer_disarm(&ac_pwm_timer);
		os_timer_setfn(&ac_pwm_timer, (os_timer_func_t *)ac_pwm_timer_func, NULL);
		os_timer_arm(&ac_pwm_timer, (1000 - ac_pwm_duty_cycle) * 10, 1);		// pwm frequency 10 second
	}
}

ICACHE_FLASH_ATTR
void ac_test() {
#ifdef DEBUG
	os_printf("\n\rac test on\n\r");
#endif
#ifdef LED_ON_AC
	led_pattern_a();
#endif
	
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
#ifdef LED_ON_AC
	led_pattern_a();
#endif
	
	//Set GPI14 to HIGH
	gpio_output_set(BIT14, 0, BIT14, 0);
	
	// wait 60 seconds and turn ac output off
	os_timer_disarm(&ac_out_off_timer);
	os_timer_setfn(&ac_out_off_timer, (os_timer_func_t *)ac_out_off_timer_func, NULL);
	os_timer_arm(&ac_out_off_timer, 60000, 0);
}

ICACHE_FLASH_ATTR
void ac_motor_valve_close() {
#ifdef DEBUG
	os_printf("\n\rac 2 on\n\r");
#endif
	ac_off();
#ifdef LED_ON_AC
	led_pattern_b();
#endif
	
	//Set GPI15 to HIGH
	gpio_output_set(BIT15, 0, BIT15, 0);
	
	// wait 60 seconds and turn ac output off
	os_timer_disarm(&ac_out_off_timer);
	os_timer_setfn(&ac_out_off_timer, (os_timer_func_t *)ac_out_off_timer_func, NULL);
	os_timer_arm(&ac_out_off_timer, 60000, 0);
}

ICACHE_FLASH_ATTR
void ac_thermo_open() {
#ifdef DEBUG
	os_printf("\n\rac 1 open\n\r");
#endif
#ifdef LED_ON_AC
	led_pattern_b();
#endif

#ifdef THERMO_NO	
	//Set GPI14 to LOW
	gpio_output_set(0, BIT14, BIT14, 0);
#else	// THERMO_NC
	//Set GPI14 to HIGH
	gpio_output_set(BIT14, 0, BIT14, 0);
#endif
	if (!sys_cfg.ac_thermo_state) {	// only save if state changed
		sys_cfg.ac_thermo_state = 1;
		cfg_save_defered();
	}
}

ICACHE_FLASH_ATTR
void ac_thermo_close() {
#ifdef DEBUG
	os_printf("\n\rac 1 close\n\r");
#endif
#ifdef LED_ON_AC
	led_pattern_a();
#endif
	
#ifdef THERMO_NO
	//Set GPI14 to HIGHT
	gpio_output_set(BIT14, 0, BIT14, 0);
#else	// THERMO_NC
	//Set GPI14 to LOW
	gpio_output_set(0, BIT14, BIT14, 0);
#endif
	if (sys_cfg.ac_thermo_state) {	// only save if state changed
		sys_cfg.ac_thermo_state = 0;
		cfg_save_defered();
	}
}

ICACHE_FLASH_ATTR
void ac_thermo_pwm(unsigned int duty_cycle) {
#ifdef DEBUG
	os_printf("\n\rac 1 pwm\n\r");
#endif
#ifdef LED_ON_AC
	led_stop_pattern();
#endif
	ac_pwm_duty_cycle = duty_cycle;
	os_timer_disarm(&ac_pwm_timer);
	os_timer_setfn(&ac_pwm_timer, (os_timer_func_t *)ac_pwm_timer_func, NULL);
	os_timer_arm(&ac_pwm_timer, 0, 0);	// fire now once, ac_pwm_timer start itself
}

ICACHE_FLASH_ATTR
void ac_off() {
	// turn ac output off
	os_timer_disarm(&ac_test_timer);
	os_timer_disarm(&ac_out_off_timer);
	os_timer_setfn(&ac_out_off_timer, (os_timer_func_t *)ac_out_off_timer_func, NULL);
	os_timer_arm(&ac_out_off_timer, 0, 0);
}

