#include <esp8266.h>
#include <string.h>
#include "ota.h"
#include "rboot-api.h"
#include "rboot-ota.h"
#include "tinyprintf.h"
#include "utils.h"

static rboot_ota_request ota_req;

// Callback fired when rBoot finishes the HTTP transfer
ICACHE_FLASH_ATTR 
static void ota_upgrade_callback(rboot_ota_request *req, bool result) {
	if (result == true) {
#ifdef DEBUG
		os_printf("OTA Upgrade successful. Rebooting to rom %d...\n", req->rom_slot);
#endif
		rboot_set_current_rom(req->rom_slot);
		system_restart_defered();
	} else {
#ifdef DEBUG
		os_printf("OTA Upgrade failed.\n");
#endif
	}
}

ICACHE_FLASH_ATTR
bool start_ota_upgrade(const char *base_url, uint8_t *out_target_rom) {
	uint8_t current_rom;
	uint8_t target_rom;
	char ota_url[256] = {0};

	if (base_url == NULL || strlen(base_url) == 0) {
		return false;
	}

	current_rom = rboot_get_current_rom();
	target_rom = (current_rom == 0) ? 1 : 0;
	
	if (out_target_rom != NULL) {
		*out_target_rom = target_rom;
	}

	// Format the URL based on the target slot (e.g. http://server.com/release/123/latest/user2.bin)
	tfp_snprintf(ota_url, sizeof(ota_url), "%suser%d.bin", base_url, target_rom + 1);

	memset(&ota_req, 0, sizeof(rboot_ota_request));
	strncpy((char*)ota_req.ota_url, ota_url, sizeof(ota_req.ota_url) - 1);
	ota_req.rom_slot = target_rom;
	ota_req.callback = (ota_callback)ota_upgrade_callback;

#ifdef DEBUG
	os_printf("Starting OTA from: %s\n", ota_url);
#endif

	// Start HTTP download directly to flash
	return rboot_ota_start(&ota_req);
}
