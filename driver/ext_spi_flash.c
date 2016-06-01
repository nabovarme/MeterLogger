#include "driver/spi.h"
#include "driver/ext_spi_flash.h"

ICACHE_FLASH_ATTR
uint16_t ext_spi_init() {
	spi_init_gpio(HSPI, SPI_CLK_USE_DIV);
	spi_clock(HSPI, 4, 2); //10MHz
	spi_tx_byte_order(HSPI, SPI_BYTE_ORDER_HIGH_TO_LOW);
	spi_rx_byte_order(HSPI, SPI_BYTE_ORDER_HIGH_TO_LOW); 
	
	SET_PERI_REG_MASK(SPI_USER(HSPI), SPI_CS_SETUP|SPI_CS_HOLD);
	CLEAR_PERI_REG_MASK(SPI_USER(HSPI), SPI_FLASH_MODE);
}

ICACHE_FLASH_ATTR
uint16_t ext_spi_flash_id() {
	return spi_transaction(HSPI, 8, 0x90, 24, 0, 0, 0, 16, 0);	// read manufacturer ID / device ID
}

//	spi_transaction(HSPI, 8, 0x06, 0, 0, 0, 0, 0, 0);	// send WREN SPI command
