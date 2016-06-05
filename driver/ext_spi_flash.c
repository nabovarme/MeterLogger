#include "driver/spi.h"
#include "driver/ext_spi_flash.h"

#include <limits.h>

ICACHE_FLASH_ATTR
void ext_spi_init() {
#ifdef DEBUG
	uint16_t flash_id;
#endif
	
	spi_init_gpio(HSPI, SPI_CLK_USE_DIV);
	spi_clock(HSPI, 4, 2); //10MHz
	spi_tx_byte_order(HSPI, SPI_BYTE_ORDER_LOW_TO_HIGH);
	spi_rx_byte_order(HSPI, SPI_BYTE_ORDER_LOW_TO_HIGH); 
	
	SET_PERI_REG_MASK(SPI_USER(HSPI), SPI_CS_SETUP|SPI_CS_HOLD);
	CLEAR_PERI_REG_MASK(SPI_USER(HSPI), SPI_FLASH_MODE);
	

#ifdef DEBUG
	flash_id = ext_spi_flash_id();
	os_printf("manufacturer ID: 0x%x, device ID: 0x%x\n", (flash_id >> 8), (flash_id & 0xff));
#endif
}

ICACHE_FLASH_ATTR
uint16_t ext_spi_flash_id() {
	return spi_transaction(HSPI, 8, 0x90, 24, 0, 0, 0, 16, 0);	// read manufacturer ID / device ID
}

ICACHE_FLASH_ATTR
bool ext_spi_flash_erase_sector(uint16_t sec) {
	sec = sec << 12;	// 4 kB per sector in NAND FLASH RAM
	spi_transaction(HSPI, 8, 0x06, 0, 0, 0, 0, 0, 0);		// write enable
	spi_transaction(HSPI, 8, 0x20, 24, sec, 0, 0, 0, 0);	// sector erase
	
	while (spi_transaction(HSPI, 8, 0x05, 0, 0, 0, 0, 16, 0) & 0x100) {	// read status register
		// wait while BUSY flag is set
	}
	
	return true;
}

ICACHE_FLASH_ATTR
bool ext_spi_flash_read(uint32_t src_addr, uint32_t *dst_addr, uint32_t size) {
	uint16_t i;
	
	if (size <= sizeof(uint32_t)) {
		*dst_addr = spi_transaction(HSPI, 8, 0x03, 24, src_addr, 0, 0, size * CHAR_BIT, 0);
		//SWAP_UINT32(*dst_addr);
	}
	else {
		for (i = 0; i < (size / sizeof(uint32_t)); i++) {
			*(dst_addr + i) = spi_transaction(HSPI, 8, 0x03, 24, src_addr + sizeof(uint32_t) * i, 0, 0, sizeof(uint32_t) * CHAR_BIT, 0);
			//SWAP_UINT32(*dst_addr);
		}
		if (size % sizeof(uint32_t)) {
			*(dst_addr + i) = spi_transaction(HSPI, 8, 0x03, 24, src_addr + sizeof(uint32_t) * i, 0, 0, (size % sizeof(uint32_t)) * CHAR_BIT, 0);
			//SWAP_UINT32(*dst_addr);
		}
	}
	
	return true;
}

ICACHE_FLASH_ATTR
bool ext_spi_flash_write(uint32_t dst_addr, uint32_t *src_addr, uint32_t size) {
	uint16_t i;
	
	if (size <= sizeof(uint32_t)) {
		spi_transaction(HSPI, 8, 0x06, 0, 0, 0, 0, 0, 0);	// write enable
		spi_transaction(HSPI, 8, 0x02, 24, dst_addr, size * CHAR_BIT, *src_addr, 0, 0);	// page program, 8 * size bit data
		
		while (spi_transaction(HSPI, 8, 0x05, 0, 0, 0, 0, 16, 0) & 0x100) {	// read status register
			// wait while BUSY flag is set
		}
	}
	else {
		for (i = 0; i < (size / sizeof(uint32_t)); i++) {
			spi_transaction(HSPI, 8, 0x06, 0, 0, 0, 0, 0, 0);	// write enable
			spi_transaction(HSPI, 8, 0x02, 24, dst_addr + sizeof(uint32_t) * i, sizeof(uint32_t) * CHAR_BIT, *(src_addr + i), 0, 0);	// page program, 8 * size bit data
			
			while (spi_transaction(HSPI, 8, 0x05, 0, 0, 0, 0, 16, 0) & 0x100) {	// read status register
				// wait while BUSY flag is set
			}
		}
		if (size % sizeof(uint32_t)) {
			spi_transaction(HSPI, 8, 0x06, 0, 0, 0, 0, 0, 0);	// write enable
			spi_transaction(HSPI, 8, 0x02, 24, dst_addr + sizeof(uint32_t) * i, (size % sizeof(uint32_t)) * CHAR_BIT, *(src_addr + i) & ((1 << (size % sizeof(uint32_t)) * CHAR_BIT) - 1), 0, 0);	// page program, 8 * size bit data
			
			while (spi_transaction(HSPI, 8, 0x05, 0, 0, 0, 0, 16, 0) & 0x100) {	// read status register
				// wait while BUSY flag is set
			}
		}
	}
	
	return true;
}

ICACHE_FLASH_ATTR
void ext_spi_flash_hexdump(uint32_t addr) {
#ifdef DEBUG
	uint8_t flash_data_buf;
	uint32_t byte_count;

	os_printf("%08x ", addr);
	for (byte_count = 0; byte_count < 15; byte_count++) {
		ext_spi_flash_read(addr, &flash_data_buf, sizeof(flash_data_buf));
		os_printf("%02x ", (flash_data_buf & 0xff));
		addr++;
	}
	ext_spi_flash_read(addr, &flash_data_buf, sizeof(flash_data_buf));
	os_printf("%02x\n", (flash_data_buf & 0xff));
	addr++;
#endif
}
