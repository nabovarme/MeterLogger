#ifndef USER_OTA_H_
#define USER_OTA_H_

#include <esp8266.h>
#include <stdbool.h>

// Starts the OTA HTTP download. 
// Returns true if successfully initiated.
ICACHE_FLASH_ATTR bool start_ota_upgrade(const char *url, uint8_t *out_target_rom);

#endif /* USER_OTA_H_ */
