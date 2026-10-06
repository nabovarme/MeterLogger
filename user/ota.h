#ifndef USER_OTA_H_
#define USER_OTA_H_

#include <esp8266.h>
#include <stdbool.h>

// Starts the OTA upgrade. Returns true if started, and outputs the target rom slot.
ICACHE_FLASH_ATTR bool start_ota_upgrade(const char *base_url, uint8_t *out_target_rom);

#endif /* USER_OTA_H_ */
