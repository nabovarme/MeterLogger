#include <esp8266.h>
#include "debug.h"
#include "led.h"
#include "led_data.h"

static os_timer_t send_timer;

static uint32_t total_bits = 0;	// Total bits to send
static uint32_t bit_index = 0;	// Current bit index

#define PREAMBLE_LEN 8
static const uint8_t preamble_bits[PREAMBLE_LEN] = {1,0,1,0,1,0,1,0}; // 8 ones

// Buffer to hold all bits of the whole string
// We'll build a dynamic bit array of 7 bits per nibble (2 nibbles per char)
static uint8_t bits_buffer[MAX_STRING_LEN * 8 * 7];  // max bits: chars * 2 nibbles * 7 bits

// Hamming(7,4) encode 4 bits into 7 bits
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

	encoded = (p1 << 6) | (p2 << 5) | (d1 << 4) | (p3 << 3) | (d2 << 2) | (d3 << 1) | (d4 << 0);

	return encoded;
}

// Prepare bit stream from string
// Returns number of bits prepared, or 0 if too long
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

	// First copy preamble bits into bits_buffer
	for (i = 0; i < PREAMBLE_LEN; i++) {
		bits_buffer[bit_pos++] = preamble_bits[i];
	}

	// Then encode and append the string bits
	for (i = 0; i < len; i++) {
		high_nibble = (str[i] >> 4) & 0x0F;
		low_nibble = str[i] & 0x0F;

		encoded_high = hamming74_encode(high_nibble);
		encoded_low = hamming74_encode(low_nibble);

		for (bit = 6; bit >= 0; bit--) {
			bits_buffer[bit_pos++] = (encoded_high >> bit) & 1;
		}
		for (bit = 6; bit >= 0; bit--) {
			bits_buffer[bit_pos++] = (encoded_low >> bit) & 1;
		}
	}

	return bit_pos;
}

// Timer callback: send one bit at a time
ICACHE_FLASH_ATTR
void send_next_bit(void *arg) {
	if (bit_index >= total_bits) {
		// All bits sent, stop timer and turn LED off
		os_timer_disarm(&send_timer);
#ifdef DEBUG
		os_printf("\n");
#endif
		led_off();
		return;
	}

	if (bits_buffer[bit_index]) {
#ifdef DEBUG
		os_printf("-");
#endif
		led_on();
	} else {
#ifdef DEBUG
		os_printf("_");
#endif
		led_off();
	}
	bit_index++;
}

// Start sending string asynchronously
ICACHE_FLASH_ATTR
int led_send_string(const char *str) {
	total_bits = prepare_bit_stream(str);
	if (total_bits == 0) {
		return -1; // error or too long string
	}

	bit_index = 0;

	// Setup timer to fire every BIT_DURATION_MS milliseconds
	os_timer_disarm(&send_timer);
	os_timer_setfn(&send_timer, send_next_bit, NULL);
	os_timer_arm(&send_timer, BIT_DURATION_MS, 1); // repeat = 1 (periodic)

	return 0;
}
