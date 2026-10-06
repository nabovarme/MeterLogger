#include <esp8266.h>
#include "user_pre_init.h"
#include "rboot-api.h"

#if ESP_SDK_VERSION >= 030000

#define EAGLE_FLASH_BIN_ADDR					(SYSTEM_PARTITION_CUSTOMER_BEGIN + 1)
#define EAGLE_IROM0TEXT_BIN_ADDR				(SYSTEM_PARTITION_CUSTOMER_BEGIN + 2)

// 1MB Flash Mapping (SPI_FLASH_SIZE_MAP == 2)
#define SPI_FLASH_SIZE_MAP						2
#define SYSTEM_PARTITION_RF_CAL_ADDR			0xFB000
#define SYSTEM_PARTITION_PHY_DATA_ADDR			0xFC000
#define SYSTEM_PARTITION_SYSTEM_PARAMETER_ADDR	0xFD000

// We leave the address and size for the app partitions at 0 initially,
// and populate them dynamically in user_pre_init based on the active rBoot slot.
static partition_item_t partition_table[] = {
	{ EAGLE_FLASH_BIN_ADDR,                 0x00000, 0x00000 },
	{ EAGLE_IROM0TEXT_BIN_ADDR,             0x00000, 0x00000 },
	{ SYSTEM_PARTITION_RF_CAL,              SYSTEM_PARTITION_RF_CAL_ADDR, 0x1000 },
	{ SYSTEM_PARTITION_PHY_DATA,            SYSTEM_PARTITION_PHY_DATA_ADDR, 0x1000 },
	{ SYSTEM_PARTITION_SYSTEM_PARAMETER,    SYSTEM_PARTITION_SYSTEM_PARAMETER_ADDR, 0x3000 },
};

ICACHE_FLASH_ATTR void user_pre_init(void) {
	// 1. Ask rBoot which slot we are currently booting from
	uint8_t current_rom = rboot_get_current_rom();
	
	// 2. Adjust SDK partition table to match the active linker script
	if (current_rom == 0) {
		// ROM 0 (user1.bin): Starts at 0x02000, ends at 0x7E000 (ESPFS start)
		// Total usable size: 0x7C000
		partition_table[0].addr = 0x02000;
		partition_table[0].size = 0x10000;
		partition_table[1].addr = 0x12000;
		partition_table[1].size = 0x6C000; // 0x7C000 - 0x10000
	} else {
		// ROM 1 (user2.bin): Starts at 0x82000, ends at 0xFB000 (RF_CAL start)
		// Total usable size: 0x79000
		partition_table[0].addr = 0x82000;
		partition_table[0].size = 0x10000;
		partition_table[1].addr = 0x92000;
		partition_table[1].size = 0x69000; // 0x79000 - 0x10000
	}

	// 3. Register the adjusted table with the SDK
	if(!system_partition_table_regist(partition_table, sizeof(partition_table)/sizeof(partition_table[0]), SPI_FLASH_SIZE_MAP)) {
		os_printf("system_partition_table_regist fail\r\n");
		while(1); // Halt on fatal SDK initialization error
	}
}

#endif
