#ifndef USER_OTA_H_
#define USER_OTA_H_

#include <esp8266.h>
#include <stdbool.h>

#include "mqtt.h"

#define _align_32_bit	__attribute__((aligned(4)))

#define is_aligned(POINTER, BYTE_COUNT) \
	(((uintptr_t)(const void *)(POINTER)) % (BYTE_COUNT) == 0)

// Starts the OTA HTTP download. 
// Returns true if successfully initiated.
ICACHE_FLASH_ATTR bool start_ota_upgrade(MQTT_Client *client, const char *url, const char *hex_sha256, uint8_t *out_target_rom);

#endif /* USER_OTA_H_ */
