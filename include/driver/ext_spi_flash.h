#ifndef EXT_SPI_FLASH_H_
#define EXT_SPI_FLASH_H_

#include "driver/ext_spi_flash.h"

ICACHE_FLASH_ATTR void ext_spi_init();
ICACHE_FLASH_ATTR uint16_t ext_spi_flash_id();
ICACHE_FLASH_ATTR bool ext_spi_flash_erase_sector(uint16_t sec);
ICACHE_FLASH_ATTR bool ext_spi_flash_read(uint32_t src_addr, uint32_t *dst_addr, uint32_t size);
ICACHE_FLASH_ATTR bool ext_spi_flash_write(uint32_t dst_addr, uint32_t *src_addr, uint32_t size);
ICACHE_FLASH_ATTR void ext_spi_flash_hexdump(uint32_t addr);

#endif /* EXT_SPI_FLASH_H_ */
