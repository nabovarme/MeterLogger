#include <esp8266.h>
#include "debug.h"
#include "led.h"
#include "led_data.h"
#include "driver/hw_timer.h"

static uint32_t total_bits = 0;  // Total Manchester *encoded* bits to send
static uint32_t bit_index = 0;   // Current bit index

#define PREAMBLE_LEN 8
static const uint8_t preamble_bits[PREAMBLE_LEN] = {1,0,1,0,1,0,1,0}; // 8 bits (still used before encoding)

// Each logical bit will expand into 2 Manchester bits
static uint8_t bits_buffer[MAX_STRING_LEN * 8 * 7 * 2];  // double size for Manchester

IRAM_ATTR static inline void gpio_led_on(void) {
	// Drive low (active-low LED)
	*((volatile uint32_t *)GPIO_OUT_W1TC_ADDRESS) = (1 << 2);
	*((volatile uint32_t *)GPIO_ENABLE_ADDRESS) |= (1 << 2);
}

IRAM_ATTR static inline void gpio_led_off(void) {
	// Drive high (turn LED off)
	*((volatile uint32_t *)GPIO_OUT_W1TS_ADDRESS) = (1 << 2);
	*((volatile uint32_t *)GPIO_ENABLE_ADDRESS) |= (1 << 2);
}

ICACHE_FLASH_ATTR
void init_hw_timer(void) {
	hw_timer_init(FRC1_SOURCE, 1);  // FRC1, edge mode
}

// Hamming(7,4) encode 4 bits into 7 bits (unchanged)
ICACHE_FLASH_ATTR
uint8_t hamming74_encode(uint8_t nibble) {
	uint8_t d1, d2, d3, d4;
	uint8_t p1, p2, p3;
	uint8_t encoded;

	d1 = (nibble >> 0) & 1;
	d2 = (nibble >> 1) & 1;
	d3 = (nibble >> 2) & 1;
	d4 = (nibble >> 3) & 1;

	p1 = d1 ^ d2 ^ d4;
	p2 = d1 ^ d3 ^ d4;
	p3 = d2 ^ d3 ^ d4;

	encoded = (p1 << 6) | (p2 << 5) | (d1 << 4) |
			  (p3 << 3) | (d2 << 2) | (d3 << 1) | (d4 << 0);

	return encoded;
}

// Convert a raw bit (0 or 1) into 2 Manchester bits
ICACHE_FLASH_ATTR
void manchester_encode_bit(uint8_t bit, uint8_t *out, uint32_t *pos) {
	if (bit) {
		// 1 -> LOW then HIGH
		out[(*pos)++] = 0;
		out[(*pos)++] = 1;
	} else {
		// 0 -> HIGH then LOW
		out[(*pos)++] = 1;
		out[(*pos)++] = 0;
	}
}

// Prepare bit stream (Manchester-encoded)
ICACHE_FLASH_ATTR
uint32_t prepare_bit_stream(const char *str) {
	int bit;
	uint32_t bit_pos = 0;
	uint32_t i;
	uint8_t encoded_high, encoded_low;
	uint8_t high_nibble, low_nibble;

	uint32_t len = strlen(str);
	if (len > MAX_STRING_LEN) {
		return 0;  // too long
	}

	// Encode preamble into Manchester
	for (i = 0; i < PREAMBLE_LEN; i++) {
		manchester_encode_bit(preamble_bits[i], bits_buffer, &bit_pos);
	}

	// Encode string (Hamming + Manchester)
	for (i = 0; i < len; i++) {
		high_nibble = (str[i] >> 4) & 0x0F;
		low_nibble = str[i] & 0x0F;

		encoded_high = hamming74_encode(high_nibble);
		encoded_low = hamming74_encode(low_nibble);

		for (bit = 6; bit >= 0; bit--) {
			manchester_encode_bit((encoded_high >> bit) & 1, bits_buffer, &bit_pos);
		}
		for (bit = 6; bit >= 0; bit--) {
			manchester_encode_bit((encoded_low >> bit) & 1, bits_buffer, &bit_pos);
		}
	}

	return bit_pos;  // Manchester bits count
}

// Hardware timer callback
IRAM_ATTR
void send_next_bit(void) {
	if (bit_index >= total_bits) {
#ifdef DEBUG
//		os_printf("\n");
#endif
		gpio_led_off();
		return;
	}

	if (bits_buffer[bit_index]) {
#ifdef DEBUG
//		os_printf("-");
#endif
		gpio_led_on();
	} else {
#ifdef DEBUG
//		os_printf("_");
#endif
		gpio_led_off();
	}
	bit_index++;
}

// Start sending string asynchronously using hw_timer
ICACHE_FLASH_ATTR
int led_send_string(const char *str) {
	total_bits = prepare_bit_stream(str);
	if (total_bits == 0) {
		return -1;
	}

	bit_index = 0;

//	hw_timer_init(NMI_SOURCE, 0);  // Use FRC1, auto-reload mode
	hw_timer_set_func(send_next_bit);

	// Period in microseconds: BIT_DURATION_MS/2 per Manchester half-bit
	hw_timer_arm((BIT_DURATION_MS * 1000) / 2);

	return 0;
}
