#ifndef LED_DATA_H_
#define LED_DATA_H_

#define BIT_DURATION_MS 200  // Logical bit is 200ms, so each half-bit = 100ms
#define MAX_STRING_LEN 64

ICACHE_FLASH_ATTR void init_hw_timer(void);
ICACHE_FLASH_ATTR uint8_t hamming74_encode(uint8_t nibble);
ICACHE_FLASH_ATTR void manchester_encode_bit(uint8_t bit, uint8_t *out, uint32_t *pos);
ICACHE_FLASH_ATTR uint32_t prepare_bit_stream(const char *str);
IRAM_ATTR void send_next_bit(void);
ICACHE_FLASH_ATTR int led_send_string(const char *str);
//IRAM_ATTR static inline void gpio_led_on(void);
//IRAM_ATTR static inline void gpio_led_off(void);

#endif /* LED_DATA_H_ */
