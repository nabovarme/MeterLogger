#ifndef UTILS_H_
#define UTILS_H_

ICACHE_FLASH_ATTR uint16_t ccit_crc16(uint16_t crc16, uint8_t *data_p, unsigned int length);
ICACHE_FLASH_ATTR unsigned int decimal_number_length(int n);
ICACHE_FLASH_ATTR int int_pow(int x, int y);
ICACHE_FLASH_ATTR int query_string_escape(char *str, size_t size);
ICACHE_FLASH_ATTR int query_string_unescape(char *str);
ICACHE_FLASH_ATTR size_t spi_flash_size();
ICACHE_FLASH_ATTR void system_restart_defered();
#endif /* UTILS_H_ */

