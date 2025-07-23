#ifndef LED_DATA_H_
#define LED_DATA_H_

#define BIT_DURATION_MS 100  // Using ms for easier os_timer setup
#define MAX_STRING_LEN 64

ICACHE_FLASH_ATTR uint8_t hamming74_encode(uint8_t nibble);
ICACHE_FLASH_ATTR uint32_t prepare_bit_stream(const char *str);
ICACHE_FLASH_ATTR void send_next_bit(void *arg);
ICACHE_FLASH_ATTR int led_send_string(const char *str);

#endif /* LED_DATA_H_ */
