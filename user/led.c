#include <esp8266.h>
#include <stdarg.h>
#include "tinyprintf.h"
#include "debug.h"
#include "led.h"
#include "wifi.h"
#include "unix_time.h"

#ifndef DISABLE_LED

// Optical transmitter settings on GPIO2 (BIT2, active-low):
// 15 ms per half-bit = 30 ms per Manchester bit (~33.3 bps, ideal for 240 fps Slow-Mo).
// Use 40 ms if recording at 30/60 fps standard video.
#define OPTICAL_HALF_BIT_MS     15
#define OPTICAL_MAX_PAYLOAD     64
// 2x Preamble (0xAA, 0xAA) + SOF (0x7E) + LEN + DATA + CRC8
#define OPTICAL_FRAME_MAX_BYTES (2 + 1 + 1 + OPTICAL_MAX_PAYLOAD + 1)

// FRC1 hardware timer (256 prescaler; calibrated for system_update_cpu_freq(160)
// so OPTICAL_HALF_BIT_MS yields true wall-clock half-bit timing)
#define FRC1_ENABLE_TIMER       BIT7
#define FRC1_AUTO_LOAD          BIT6
#define FRC1_DIV_256            0x08
#define FRC1_EDGE_INT           0x00
#define OPTICAL_FRC1_TICKS      ((OPTICAL_HALF_BIT_MS * 14286) / 32)

static os_timer_t led_blinker_timer;
static os_timer_t led_single_blink_off_timer;

static os_timer_t led_double_blink_timer;
static os_timer_t led_triple_blink_timer;
static os_timer_t led_sub_pattern_timer;

static volatile bool led_disabled = true;
static volatile bool led_blinker_timer_running = false;
static volatile bool led_double_blinker_timer_running = false;
static volatile bool led_triple_blinker_timer_running = false;

static uint8_t led_sub_pattern_state = 0;

static uint8_t optical_tx_buf[OPTICAL_FRAME_MAX_BYTES];
static volatile uint16_t optical_tx_total_bits = 0;
static volatile uint16_t optical_tx_bit_idx = 0;
static volatile uint8_t optical_tx_phase = 0; // 0 = first half-bit, 1 = second half-bit
static volatile bool optical_tx_busy = false;

ICACHE_RAM_ATTR static void optical_hw_timer_stop(void) {
	RTC_CLR_REG_MASK(FRC1_CTRL_ADDRESS, FRC1_ENABLE_TIMER);
	TM1_EDGE_INT_DISABLE();
	ETS_FRC1_INTR_DISABLE();
}

ICACHE_RAM_ATTR static void optical_hw_timer_isr(void *arg) {
	uint16_t byte_idx;
	uint8_t bit_mask;
	uint8_t bit_val;
	bool led_on_state;

	RTC_REG_WRITE(FRC1_INT_ADDRESS, FRC1_INT_CLR_MASK);

	if (!optical_tx_busy || led_disabled) {
		GPIO_REG_WRITE(GPIO_OUT_W1TS_ADDRESS, BIT2); // LED off (HIGH)
		optical_hw_timer_stop();
		optical_tx_busy = false;
		return;
	}

	if (optical_tx_bit_idx >= optical_tx_total_bits) {
		GPIO_REG_WRITE(GPIO_OUT_W1TS_ADDRESS, BIT2); // LED off (HIGH)
		optical_hw_timer_stop();
		optical_tx_busy = false;
		return;
	}

	// Fetch current bit (MSB first)
	byte_idx = optical_tx_bit_idx >> 3;
	bit_mask = 0x80 >> (optical_tx_bit_idx & 0x07);
	bit_val = (optical_tx_buf[byte_idx] & bit_mask) ? 1 : 0;

	// Manchester encoding (IEEE 802.3):
	// Bit 0: ON (phase 0)  -> OFF (phase 1)
	// Bit 1: OFF (phase 0) -> ON (phase 1)
	if (optical_tx_phase == 0) {
		led_on_state = (bit_val == 0);
		optical_tx_phase = 1;
	} else {
		led_on_state = (bit_val == 1);
		optical_tx_phase = 0;
		optical_tx_bit_idx++;
	}

	if (led_on_state) {
		GPIO_REG_WRITE(GPIO_OUT_W1TC_ADDRESS, BIT2); // GPIO2 LOW -> blue LED ON
	} else {
		GPIO_REG_WRITE(GPIO_OUT_W1TS_ADDRESS, BIT2); // GPIO2 HIGH -> blue LED OFF
	}
}

ICACHE_FLASH_ATTR
static uint8_t optical_crc8(const uint8_t *data, size_t len) {
	uint8_t crc = 0x00;
	size_t i;
	uint8_t j;

	for (i = 0; i < len; i++) {
		crc ^= data[i];
		for (j = 0; j < 8; j++) {
			if (crc & 0x80) {
				crc = (crc << 1) ^ 0x07;
			} else {
				crc <<= 1;
			}
		}
	}
	return crc;
}

ICACHE_FLASH_ATTR
bool led_optical_is_busy(void) {
	return optical_tx_busy;
}

ICACHE_FLASH_ATTR
bool led_optical_send(const uint8_t *data, uint8_t len) {
	uint8_t idx = 0;

	if (led_disabled || data == NULL || len == 0 || len > OPTICAL_MAX_PAYLOAD || optical_tx_busy) {
		return false;
	}

	// Stop any active blink/pattern timers so GPIO2 is dedicated to optical TX
	led_stop_pattern();
	os_timer_disarm(&led_single_blink_off_timer);

	optical_tx_busy = true;
	gpio_output_set(BIT2, 0, BIT2, 0); // Ensure blue LED starts OFF (HIGH)

	// Build frame: [0xAA, 0xAA preamble] [0x7E SOF] [LEN] [PAYLOAD...] [CRC8]
	optical_tx_buf[idx++] = 0xAA;
	optical_tx_buf[idx++] = 0xAA;
	optical_tx_buf[idx++] = 0x7E;
	optical_tx_buf[idx++] = len;
	memcpy(&optical_tx_buf[idx], data, len);
	idx += len;
	optical_tx_buf[idx] = optical_crc8(&optical_tx_buf[3], len + 1); // CRC over LEN + PAYLOAD
	idx++;

	optical_tx_total_bits = idx * 8;
	optical_tx_bit_idx = 0;
	optical_tx_phase = 0;

	// Arm FRC1 hardware timer for exact 15.000 ms periodic interrupts
	optical_hw_timer_stop();
	ETS_FRC_TIMER1_INTR_ATTACH(optical_hw_timer_isr, NULL);
	RTC_REG_WRITE(FRC1_LOAD_ADDRESS, OPTICAL_FRC1_TICKS);
	TM1_EDGE_INT_ENABLE();
	ETS_FRC1_INTR_ENABLE();
	RTC_REG_WRITE(FRC1_CTRL_ADDRESS, FRC1_ENABLE_TIMER | FRC1_AUTO_LOAD | FRC1_DIV_256 | FRC1_EDGE_INT);

	return true;
}

ICACHE_FLASH_ATTR
bool led_optical_printf(const char *fmt, ...) {
	char buf[OPTICAL_MAX_PAYLOAD + 1];
	va_list args;
	int n;

	if (led_disabled || optical_tx_busy) {
		return false;
	}

	va_start(args, fmt);
	n = tfp_vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	if (n <= 0) {
		return false;
	}
	if (n > OPTICAL_MAX_PAYLOAD) {
		n = OPTICAL_MAX_PAYLOAD;
	}

	return led_optical_send((const uint8_t *)buf, (uint8_t)n);
}

ICACHE_FLASH_ATTR
bool led_optical_log_wifi(void) {
	struct station_config sta_cfg;
	char ssid[33];
	sint8 rssi;
	uint8 ch;
	uint32 heap;
	uint32 status;
	uint64 up;

	if (led_disabled || optical_tx_busy) {
		return false;
	}

	memset(&sta_cfg, 0, sizeof(sta_cfg));
	if (!wifi_station_get_config(&sta_cfg)) {
		return false;
	}

	memcpy(ssid, sta_cfg.ssid, 32);
	ssid[32] = '\0';

	rssi   = wifi_station_get_rssi();
	ch     = wifi_get_channel();
	heap   = system_get_free_heap_size();
	status = wifi_get_status();
	up     = get_uptime();

	if (rssi == 31) {
		return led_optical_printf("%s n/a ch%u st%u(%s) up%llus heap%u",
			ssid[0] ? ssid : "<none>", ch, status, wifi_get_reason_desc((uint8_t)status), up, heap);
	}
	return led_optical_printf("%s %ddBm ch%u st%u(%s) up%llus heap%u",
		ssid[0] ? ssid : "<none>", (int)rssi, ch, status, wifi_get_reason_desc((uint8_t)status), up, heap);
}

ICACHE_FLASH_ATTR void static led_blinker_timer_func(void *arg) {
	if (led_blinker_timer_running == false || led_disabled || optical_tx_busy) {
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
	if (led_double_blinker_timer_running == false || led_disabled || optical_tx_busy) {
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

ICACHE_FLASH_ATTR void static led_triple_blink_timer_func(void *arg) {
	// blink fast three times
	if (led_triple_blinker_timer_running == false || led_disabled || optical_tx_busy) {
		// stop blinking if asked to stop via state variable led_triple_blinker_timer_running
		os_timer_disarm(&led_triple_blink_timer);
		return;
	}	

	led_off();
	
	switch (led_sub_pattern_state) {
		case 0:
		case 2:
		case 4:
			led_on();
			led_sub_pattern_state++;			
			os_timer_disarm(&led_sub_pattern_timer);
			os_timer_setfn(&led_sub_pattern_timer, (os_timer_func_t *)led_triple_blink_timer_func, NULL);
			os_timer_arm(&led_sub_pattern_timer, 100, 0);
			break;
		case 1:
		case 3:
			led_off();
			led_sub_pattern_state++;
			os_timer_disarm(&led_sub_pattern_timer);
			os_timer_setfn(&led_sub_pattern_timer, (os_timer_func_t *)led_triple_blink_timer_func, NULL);
			os_timer_arm(&led_sub_pattern_timer, 100, 0);
			break;
		case 5:
			led_off();
			os_timer_disarm(&led_sub_pattern_timer);
			led_sub_pattern_state = 0;
			break;
	}
}

ICACHE_FLASH_ATTR void led_init(void) {
	//Set GPIO2 to output mode
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO2_U, FUNC_GPIO2);
	
	// Set GPIO2 to HIGH (turn blue led off)
	gpio_output_set(BIT2, 0, BIT2, 0);
	
	led_disabled = false;
}

ICACHE_FLASH_ATTR void led_on(void) {
	if (optical_tx_busy) {
		return;
	}
	//Set GPIO2 to LOW
	gpio_output_set(0, BIT2, BIT2, 0);
}

ICACHE_FLASH_ATTR void led_off(void) {
	if (optical_tx_busy) {
		return;
	}
	// Set GPIO2 to HIGH
	gpio_output_set(BIT2, 0, BIT2, 0);
}

ICACHE_FLASH_ATTR void led_blink(void) {
	if (led_disabled || optical_tx_busy) {
		return;
	}
	
	led_on();
	os_timer_disarm(&led_single_blink_off_timer);
	os_timer_setfn(&led_single_blink_off_timer, (os_timer_func_t *)led_single_blink_off_timer_func, NULL);
	os_timer_arm(&led_single_blink_off_timer, 1000, 0);
}

ICACHE_FLASH_ATTR void led_blink_short(void) {
	if (led_disabled || optical_tx_busy) {
		return;
	}
	
	led_on();
	os_timer_disarm(&led_single_blink_off_timer);
	os_timer_setfn(&led_single_blink_off_timer, (os_timer_func_t *)led_single_blink_off_timer_func, NULL);
	os_timer_arm(&led_single_blink_off_timer, 100, 0);
}

ICACHE_FLASH_ATTR void led_pattern_a(void) {
	if (led_disabled || optical_tx_busy) {
		return;
	}
	
	os_timer_disarm(&led_blinker_timer);
	os_timer_setfn(&led_blinker_timer, (os_timer_func_t *)led_blinker_timer_func, NULL);
	led_blinker_timer_running = true;	// state variable to control stopping after pattern is done
	os_timer_arm(&led_blinker_timer, 1000, 1);
}

ICACHE_FLASH_ATTR void led_pattern_b(void) {
	if (led_disabled || optical_tx_busy) {
		return;
	}
	
	os_timer_disarm(&led_blinker_timer);
	os_timer_setfn(&led_blinker_timer, (os_timer_func_t *)led_blinker_timer_func, NULL);
	led_blinker_timer_running = true;	// state variable to control stopping after pattern is done
	os_timer_arm(&led_blinker_timer, 200, 1);
}

ICACHE_FLASH_ATTR void led_pattern_c(void) {
	// blink fast two times every 5th second
	if (led_disabled || optical_tx_busy) {
		return;
	}
	
	os_timer_disarm(&led_double_blink_timer);
	os_timer_setfn(&led_double_blink_timer, (os_timer_func_t *)led_double_blink_timer_func, NULL);
	led_double_blinker_timer_running = true;	// state variable to control stopping after pattern is done
	os_timer_arm(&led_double_blink_timer, 3000, 1);
}

ICACHE_FLASH_ATTR void led_pattern_d(void) {
	// blink fast three times every 2 seconds
	if (led_disabled || optical_tx_busy) {
		return;
	}
	
	os_timer_disarm(&led_triple_blink_timer);
	os_timer_setfn(&led_triple_blink_timer, (os_timer_func_t *)led_triple_blink_timer_func, NULL);
	led_triple_blinker_timer_running = true;	// state variable to control stopping after pattern is done
	os_timer_arm(&led_triple_blink_timer, 2000, 1);
}

ICACHE_FLASH_ATTR void led_stop_pattern(void) {
	led_blinker_timer_running = false;
	led_double_blinker_timer_running = false;
	led_triple_blinker_timer_running = false;
	led_sub_pattern_state = 0; // Reset sub-state so patterns don't corrupt each other
	led_off();
}

ICACHE_FLASH_ATTR void led_destroy(void) {
	led_disabled = true;
	optical_hw_timer_stop();
	optical_tx_busy = false;
	led_stop_pattern();
}

#else

// Dummy stub functions preserved so existing call sites compile cleanly
ICACHE_FLASH_ATTR void led_init(void) {}
ICACHE_FLASH_ATTR void led_on(void) {}
ICACHE_FLASH_ATTR void led_off(void) {}
ICACHE_FLASH_ATTR void led_blink(void) {}
ICACHE_FLASH_ATTR void led_blink_short(void) {}
ICACHE_FLASH_ATTR void led_pattern_a(void) {}
ICACHE_FLASH_ATTR void led_pattern_b(void) {}
ICACHE_FLASH_ATTR void led_pattern_c(void) {}
ICACHE_FLASH_ATTR void led_pattern_d(void) {}
ICACHE_FLASH_ATTR void led_stop_pattern(void) {}
ICACHE_FLASH_ATTR void led_destroy(void) {}
ICACHE_FLASH_ATTR bool led_optical_send(const uint8_t *data, uint8_t len) { return false; }
ICACHE_FLASH_ATTR bool led_optical_printf(const char *fmt, ...) { return false; }
ICACHE_FLASH_ATTR bool led_optical_is_busy(void) { return false; }
ICACHE_FLASH_ATTR bool led_optical_log_wifi(void) { return false; }

#endif // DISABLE_LED
