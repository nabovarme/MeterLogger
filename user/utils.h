#ifndef UTILS_H_
#define UTILS_H_


#endif /* UTILS_H_ */

ICACHE_FLASH_ATTR uint16_t crc_ccit_crc16(uint8_t *data_p, unsigned int length);
ICACHE_FLASH_ATTR void float_to_string(float value, char *value_string, int8_t max_decimals);
ICACHE_FLASH_ATTR float string_to_float(unsigned char *value_string, int8_t max_decimals);