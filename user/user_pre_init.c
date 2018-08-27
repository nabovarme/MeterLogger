#include <esp8266.h>

#define EAGLE_FLASH_BIN_ADDR				（SYSTEM_PARTITION_CUSTOMER_BEGIN + 1）
#define EAGLE_IROM0TEXT_BIN_ADDR			（SYSTEM_PARTITION_CUSTOMER_BEGIN + 2）

static const partition_item_t partition_table[] = {
	{ EAGLE_FLASH_BIN_ADDR, 	0x00000, 0x10000},
	{ EAGLE_IROM0TEXT_BIN_ADDR, 0x10000, 0x60000},
	{ SYSTEM_PARTITION_RF_CAL, SYSTEM_PARTITION_RF_CAL_ADDR, 0x1000},
	{ SYSTEM_PARTITION_PHY_DATA, SYSTEM_PARTITION_PHY_DATA_ADDR, 0x1000},
	{ SYSTEM_PARTITION_SYSTEM_PARAMETER,SYSTEM_PARTITION_SYSTEM_PARAMETER_ADDR, 0x3000},
};

ICACHE_FLASH_ATTR void user_pre_init(void) {
	if(!system_partition_table_regist(partition_table, sizeof(partition_table)/sizeof(partition_table[0]),SPI_FLASH_SIZE_MAP)) {
		printf("system_partition_table_regist fail\r\n");
		while(1);
	}
}

