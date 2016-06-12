#include <stddef.h>
#include <esp8266.h>

#include "mqtt.h"
#include "config.h"
#include "user_config.h"
#include "debug.h"
#include "utils.h"
#include "tinyprintf.h"
#include "driver/ext_spi_flash.h"

syscfg_t sys_cfg;
SAVE_FLAG saveFlag;

#define EXT_CFG_LOCATION		0x0

#ifdef EXT_SPI_RAM_IS_NAND
#define EXT_SPI_RAM_SEC_SIZE	0x1000	// 4 kB block size for FLASH RAM
#else
#define EXT_SPI_RAM_SEC_SIZE	0x200	// we only need 512 bytes for config when FeRAM is used
#endif

#define SAVE_DEFER_TIME 2000
static os_timer_t config_save_timer;
char config_save_timer_running;

void ICACHE_FLASH_ATTR
cfg_save() {
#ifdef IMPULSE
	uint32_t impulse_meter_count_temp;
#endif // IMPULSE
	
#ifdef IMPULSE	
	do {
		// try to save until sys_cfg.impulse_meter_count does not change
		impulse_meter_count_temp = sys_cfg.impulse_meter_count;

		// calculate checksum on sys_cfg struct without ccit_crc16
		sys_cfg.ccit_crc16 = ccit_crc16((uint8_t *)&sys_cfg, offsetof(syscfg_t, ccit_crc16) - offsetof(syscfg_t, cfg_holder));	
		ext_spi_flash_read((EXT_CFG_LOCATION + 2) * EXT_SPI_RAM_SEC_SIZE,
		                   (uint32 *)&saveFlag, sizeof(SAVE_FLAG));
	
		if (saveFlag.flag == 0) {
#ifdef EXT_SPI_RAM_IS_NAND
			ext_spi_flash_erase_sector(EXT_CFG_LOCATION + 1);
#endif	// EXT_SPI_RAM_IS_NAND
			ext_spi_flash_write((EXT_CFG_LOCATION + 1) * EXT_SPI_RAM_SEC_SIZE,
							(uint32 *)&sys_cfg, sizeof(syscfg_t));
			saveFlag.flag = 1;
#ifdef EXT_SPI_RAM_IS_NAND
			ext_spi_flash_erase_sector(EXT_CFG_LOCATION + 2);
#endif	// EXT_SPI_RAM_IS_NAND
			ext_spi_flash_write((EXT_CFG_LOCATION + 2) * EXT_SPI_RAM_SEC_SIZE,
							(uint32 *)&saveFlag, sizeof(SAVE_FLAG));
		} else {
#ifdef EXT_SPI_RAM_IS_NAND
			ext_spi_flash_erase_sector(EXT_CFG_LOCATION + 0);
#endif	// EXT_SPI_RAM_IS_NAND
			ext_spi_flash_write((EXT_CFG_LOCATION + 0) * EXT_SPI_RAM_SEC_SIZE,
							(uint32 *)&sys_cfg, sizeof(syscfg_t));
			saveFlag.flag = 0;
#ifdef EXT_SPI_RAM_IS_NAND
			ext_spi_flash_erase_sector(EXT_CFG_LOCATION + 2);
#endif	// EXT_SPI_RAM_IS_NAND
			ext_spi_flash_write((EXT_CFG_LOCATION + 2) * EXT_SPI_RAM_SEC_SIZE,
							(uint32 *)&saveFlag, sizeof(SAVE_FLAG));
		}
	} while (sys_cfg.impulse_meter_count != impulse_meter_count_temp);
#else
	// calculate checksum on sys_cfg struct without ccit_crc16
	sys_cfg.ccit_crc16 = ccit_crc16((uint8_t *)&sys_cfg, offsetof(syscfg_t, ccit_crc16) - offsetof(syscfg_t, cfg_holder));	
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
#endif	// IMPULSE
}

void ICACHE_FLASH_ATTR
cfg_load() {
#ifdef IMPULSE
	// DEBUG: we suppose nothing else is touching sys_cfg while saving otherwise checksum becomes wrong
	INFO("\r\nload ...\r\n");
	
	ext_spi_flash_read((EXT_CFG_LOCATION + 2) * EXT_SPI_RAM_SEC_SIZE,
				   (uint32 *)&saveFlag, sizeof(SAVE_FLAG));
	if (saveFlag.flag == 0) {
		ext_spi_flash_read((EXT_CFG_LOCATION + 0) * EXT_SPI_RAM_SEC_SIZE,
					   (uint32 *)&sys_cfg, sizeof(syscfg_t));
	} else {
		ext_spi_flash_read((EXT_CFG_LOCATION + 1) * EXT_SPI_RAM_SEC_SIZE,
					   (uint32 *)&sys_cfg, sizeof(syscfg_t));
	}

	// if checksum fails...
	if (sys_cfg.ccit_crc16 != ccit_crc16((uint8_t *)&sys_cfg, offsetof(syscfg_t, ccit_crc16) - offsetof(syscfg_t, cfg_holder))) {
#ifdef DEBUG
		os_printf("config crc error, default conf loaded\n");
#endif // DEBUG
		// if first time config load default conf
		os_memset(&sys_cfg, 0x00, sizeof(syscfg_t));

		tfp_snprintf(sys_cfg.sta_ssid, 64, "%s", STA_SSID);
		tfp_snprintf(sys_cfg.sta_pwd, 64, "%s", STA_PASS);
		sys_cfg.sta_type = STA_TYPE;

		tfp_snprintf(sys_cfg.device_id, 16, MQTT_CLIENT_ID, system_get_chip_id());
		tfp_snprintf(sys_cfg.mqtt_host, 64, "%s", MQTT_HOST);
		sys_cfg.mqtt_port = MQTT_PORT;
		tfp_snprintf(sys_cfg.mqtt_user, 32, "%s", MQTT_USER);
		tfp_snprintf(sys_cfg.mqtt_pass, 32, "%s", MQTT_PASS);

		sys_cfg.security = DEFAULT_SECURITY;	//default non ssl
		
		// { 0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6, 0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c });
		sys_cfg.aes_key[0] = 0x2b;
		sys_cfg.aes_key[1] = 0x7e;
		sys_cfg.aes_key[2] = 0x15;
		sys_cfg.aes_key[3] = 0x16;
		sys_cfg.aes_key[4] = 0x28;
		sys_cfg.aes_key[5] = 0xae;
		sys_cfg.aes_key[6] = 0xd2;
		sys_cfg.aes_key[7] = 0xa6;
		sys_cfg.aes_key[8] = 0xab;
		sys_cfg.aes_key[9] = 0xf7;
		sys_cfg.aes_key[10] = 0x15;
		sys_cfg.aes_key[11] = 0x88;
		sys_cfg.aes_key[12] = 0x09;
		sys_cfg.aes_key[13] = 0xcf;
		sys_cfg.aes_key[14] = 0x4f;
		sys_cfg.aes_key[15] = 0x3c;

		sys_cfg.mqtt_keepalive = MQTT_KEEPALIVE;
#ifndef IMPULSE
		sys_cfg.ac_thermo_state = 0;
		memset(&sys_cfg.cron_jobs, 0, sizeof(cron_job_t));
#endif
		tfp_snprintf(sys_cfg.impulse_meter_serial, IMPULSE_METER_SERIAL_LEN, DEFAULT_METER_SERIAL);
		tfp_snprintf(sys_cfg.impulse_meter_energy, 2, "0");
		tfp_snprintf(sys_cfg.impulses_per_kwh, 4, "100");
		sys_cfg.impulse_meter_count = 0;
		INFO(" default configuration\r\n");

		cfg_save();
	}
	else {
#ifdef DEBUG
		os_printf("config crc ok\n");
#endif // DEBUG
	}
#else
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
	if (sys_cfg.ccit_crc16 != ccit_crc16((uint8_t *)&sys_cfg, offsetof(syscfg_t, ccit_crc16) - offsetof(syscfg_t, cfg_holder))) {
#ifdef DEBUG
		os_printf("config crc error, default conf loaded\n");
#endif // DEBUG
		// if first time config load default conf
		os_memset(&sys_cfg, 0x00, sizeof(syscfg_t));

		tfp_snprintf(sys_cfg.sta_ssid, 64, "%s", STA_SSID);
		tfp_snprintf(sys_cfg.sta_pwd, 64, "%s", STA_PASS);
		sys_cfg.sta_type = STA_TYPE;

		tfp_snprintf(sys_cfg.device_id, 16, MQTT_CLIENT_ID, system_get_chip_id());
		tfp_snprintf(sys_cfg.mqtt_host, 64, "%s", MQTT_HOST);
		sys_cfg.mqtt_port = MQTT_PORT;
		tfp_snprintf(sys_cfg.mqtt_user, 32, "%s", MQTT_USER);
		tfp_snprintf(sys_cfg.mqtt_pass, 32, "%s", MQTT_PASS);

		sys_cfg.security = DEFAULT_SECURITY;	//default non ssl
		
		// { 0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6, 0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c });
		sys_cfg.aes_key[0] = 0x2b;
		sys_cfg.aes_key[1] = 0x7e;
		sys_cfg.aes_key[2] = 0x15;
		sys_cfg.aes_key[3] = 0x16;
		sys_cfg.aes_key[4] = 0x28;
		sys_cfg.aes_key[5] = 0xae;
		sys_cfg.aes_key[6] = 0xd2;
		sys_cfg.aes_key[7] = 0xa6;
		sys_cfg.aes_key[8] = 0xab;
		sys_cfg.aes_key[9] = 0xf7;
		sys_cfg.aes_key[10] = 0x15;
		sys_cfg.aes_key[11] = 0x88;
		sys_cfg.aes_key[12] = 0x09;
		sys_cfg.aes_key[13] = 0xcf;
		sys_cfg.aes_key[14] = 0x4f;
		sys_cfg.aes_key[15] = 0x3c;

		sys_cfg.mqtt_keepalive = MQTT_KEEPALIVE;
		
		sys_cfg.ac_thermo_state = 0;
		memset(&sys_cfg.cron_jobs, 0, sizeof(cron_job_t));
		INFO(" default configuration\r\n");

		cfg_save();
	}
	else {
#ifdef DEBUG
		os_printf("config crc ok\n");
#endif // DEBUG
	}
#endif // IMPULSE
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
