#include <stddef.h>
#include <esp8266.h>

#include "mqtt.h"
#include "config.h"
#include "user_config.h"
#include "debug.h"
#include <utils.h>

syscfg_t sys_cfg;
SAVE_FLAG saveFlag;

#define SAVE_DEFER_TIME 2000
static os_timer_t config_save_timer;
char config_save_timer_running;

void ICACHE_FLASH_ATTR
cfg_save() {
	uint32_t impulse_meter_count_temp;
	int i = 0;
	
	do {
		// try to save until sys_cfg.impulse_meter_count does not change
		impulse_meter_count_temp = sys_cfg.impulse_meter_count;

		// calculate checksum on sys_cfg struct without ccit_crc16
		sys_cfg.ccit_crc16 = ccit_crc16(&sys_cfg, offsetof(syscfg_t, ccit_crc16) - offsetof(syscfg_t, cfg_holder));	
		spi_flash_read((CFG_LOCATION + 3) * SPI_FLASH_SEC_SIZE,
		                   (uint32 *)&saveFlag, sizeof(SAVE_FLAG));
	
		if (saveFlag.flag == 0) {
			spi_flash_erase_sector(CFG_LOCATION + 1);
			spi_flash_write((CFG_LOCATION + 1) * SPI_FLASH_SEC_SIZE,
							(uint32 *)&sys_cfg, sizeof(syscfg_t));
			saveFlag.flag = 1;
			spi_flash_erase_sector(CFG_LOCATION + 3);
			spi_flash_write((CFG_LOCATION + 3) * SPI_FLASH_SEC_SIZE,
							(uint32 *)&saveFlag, sizeof(SAVE_FLAG));
		} else {
			spi_flash_erase_sector(CFG_LOCATION + 0);
			spi_flash_write((CFG_LOCATION + 0) * SPI_FLASH_SEC_SIZE,
							(uint32 *)&sys_cfg, sizeof(syscfg_t));
			saveFlag.flag = 0;
			spi_flash_erase_sector(CFG_LOCATION + 3);
			spi_flash_write((CFG_LOCATION + 3) * SPI_FLASH_SEC_SIZE,
							(uint32 *)&saveFlag, sizeof(SAVE_FLAG));
		}
	} while (sys_cfg.impulse_meter_count != impulse_meter_count_temp);
}

void ICACHE_FLASH_ATTR
cfg_load() {
	// DEBUG: we suppose nothing else is touching sys_cfg while saving otherwise checksum becomes wrong
	INFO("\r\nload ...\r\n");
	
	spi_flash_read((CFG_LOCATION + 3) * SPI_FLASH_SEC_SIZE,
				   (uint32 *)&saveFlag, sizeof(SAVE_FLAG));
	if (saveFlag.flag == 0) {
		spi_flash_read((CFG_LOCATION + 0) * SPI_FLASH_SEC_SIZE,
					   (uint32 *)&sys_cfg, sizeof(syscfg_t));
	} else {
		spi_flash_read((CFG_LOCATION + 1) * SPI_FLASH_SEC_SIZE,
					   (uint32 *)&sys_cfg, sizeof(syscfg_t));
	}

	// if checksum fails...
	if (sys_cfg.ccit_crc16 != ccit_crc16(&sys_cfg, offsetof(syscfg_t, ccit_crc16) - offsetof(syscfg_t, cfg_holder))) {
#ifdef DEBUG
		os_printf("config crc error, default conf loaded\n");
#endif // DEBUG
		// if first time config load default conf
		os_memset(&sys_cfg, 0x00, sizeof(syscfg_t));

		os_sprintf(sys_cfg.sta_ssid, "%s", STA_SSID);
		os_sprintf(sys_cfg.sta_pwd, "%s", STA_PASS);
		sys_cfg.sta_type = STA_TYPE;

		os_sprintf(sys_cfg.device_id, MQTT_CLIENT_ID, system_get_chip_id());
		os_sprintf(sys_cfg.mqtt_host, "%s", MQTT_HOST);
		sys_cfg.mqtt_port = MQTT_PORT;
		os_sprintf(sys_cfg.mqtt_user, "%s", MQTT_USER);
		os_sprintf(sys_cfg.mqtt_pass, "%s", MQTT_PASS);

		sys_cfg.security = DEFAULT_SECURITY;	//default non ssl

		sys_cfg.mqtt_keepalive = MQTT_KEEPALIVE;
		
#ifdef IMPULSE
		sys_cfg.ac_thermo_state = 0;
		memset(&sys_cfg.cron_jobs, 0, sizeof(cron_job_t));
		os_sprintf(sys_cfg.impulse_meter_serial, DEFAULT_METER_SERIAL);
		os_sprintf(sys_cfg.impulse_meter_energy, "0");
		os_sprintf(sys_cfg.impulses_per_kwh, "100");
		sys_cfg.impulse_meter_count = 0;
#endif
		INFO(" default configuration\r\n");

		cfg_save();
	}
	else {
#ifdef DEBUG
		os_printf("config crc ok\n");
#endif // DEBUG
	}
}

void ICACHE_FLASH_ATTR
cfg_save_defered() {
	os_timer_disarm(&config_save_timer);
	os_timer_setfn(&config_save_timer, (os_timer_func_t *)config_save_timer_func, NULL);
	os_timer_arm(&config_save_timer, SAVE_DEFER_TIME, 0);
}

ICACHE_FLASH_ATTR
void config_save_timer_func(void *arg) {
	if (config_save_timer_running) {
		// reschedule
		os_timer_disarm(&config_save_timer);
		os_timer_setfn(&config_save_timer, (os_timer_func_t *)config_save_timer_func, NULL);
		os_timer_arm(&config_save_timer, SAVE_DEFER_TIME, 0);
	}
	else {
		// stop timer
		os_timer_disarm(&config_save_timer);
		config_save_timer_running = 0;

		cfg_save();		
	}
}
