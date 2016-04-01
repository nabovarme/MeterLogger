#ifndef UTILS_H_
#define UTILS_H_


#endif /* UTILS_H_ */

ICACHE_FLASH_ATTR uint16_t ccit_crc16(uint8_t *data_p, unsigned int length);
ICACHE_FLASH_ATTR void w_to_kw_str(char *w, char *kw);
ICACHE_FLASH_ATTR void kw_to_w_str(char *kw, char *w);
ICACHE_FLASH_ATTR unsigned int decimal_number_length(int n);
ICACHE_FLASH_ATTR int int_pow(int x, int y);
