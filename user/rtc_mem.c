#include <esp8266.h>
#include "rtc_mem.h"
#include "debug.h"

// Save data to RTC memory
ICACHE_FLASH_ATTR 
void save_rtc_data(uint32_t data) {
	rtc_data_t rtc_data;
	rtc_data.magic = MAGIC;  // Signature to check validity
	rtc_data.data = data;

	system_rtc_mem_write(RTC_ADDR, &rtc_data, sizeof(rtc_data));
}

// Load data from RTC memory
ICACHE_FLASH_ATTR 
bool load_rtc_data(uint32 *data) {
	rtc_data_t rtc_data;

	if (!system_rtc_mem_read(RTC_ADDR, &rtc_data, sizeof(rtc_data)))
		return false;

	if (rtc_data.magic != MAGIC)
		return false;  // Data invalid

	*data = rtc_data.data;
	return true;
}
