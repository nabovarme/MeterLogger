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

bool ICACHE_FLASH_ATTR
cfg_save(uint16_t *calculated_crc, uint16_t *saved_crc) {
#ifdef IMPULSE
	uint32_t impulse_meter_count_temp;
#endif // IMPULSE

#if defined(IMPULSE) && !defined(DEBUG_NO_METER)	// use internal flash if built with DEBUG_NO_METER=1
	do {
		// try to save until sys_cfg.impulse_meter_count does not change
		impulse_meter_count_temp = sys_cfg.impulse_meter_count;

		// calculate checksum on sys_cfg struct without ccit_crc16
		sys_cfg.ccit_crc16 = ccit_crc16(0xffff, (uint8_t *)&sys_cfg, offsetof(syscfg_t, ccit_crc16) - offsetof(syscfg_t, cfg_holder));	
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
	sys_cfg.ccit_crc16 = ccit_crc16(0xffff, (uint8_t *)&sys_cfg, offsetof(syscfg_t, ccit_crc16) - offsetof(syscfg_t, cfg_holder));
	system_param_save_with_protect(CFG_LOCATION, &sys_cfg, sizeof(syscfg_t));
	
	// check for flash memory error
	if (calculated_crc && saved_crc) {	// check if return values was requested
		*saved_crc = sys_cfg.ccit_crc16;
		cfg_load();	// reload saved config
		*calculated_crc = ccit_crc16(0xffff, (uint8_t *)&sys_cfg, offsetof(syscfg_t, ccit_crc16) - offsetof(syscfg_t, cfg_holder));
		if (*calculated_crc != *saved_crc) {
			return false;
		}
	}
#endif	// IMPULSE
	return true;
}

void ICACHE_FLASH_ATTR
cfg_load() {
	// DEBUG: we suppose nothing else is touching sys_cfg while saving otherwise checksum becomes wrong
	INFO("\r\nload ...\r\n");
#if defined(IMPULSE) && !defined(DEBUG_NO_METER)	// use internal flash if built with DEBUG_NO_METER=1
	ext_spi_flash_read((EXT_CFG_LOCATION + 2) * EXT_SPI_RAM_SEC_SIZE,
				   (uint32 *)&saveFlag, sizeof(SAVE_FLAG));
	if (saveFlag.flag == 0) {
		ext_spi_flash_read((EXT_CFG_LOCATION + 0) * EXT_SPI_RAM_SEC_SIZE,
					   (uint32 *)&sys_cfg, sizeof(syscfg_t));
	} else {
		ext_spi_flash_read((EXT_CFG_LOCATION + 1) * EXT_SPI_RAM_SEC_SIZE,
					   (uint32 *)&sys_cfg, sizeof(syscfg_t));
	}
#else
	system_param_load(CFG_LOCATION, 0, &sys_cfg, sizeof(syscfg_t));
#endif	// IMPULSE

	// if checksum fails...
	if (sys_cfg.ccit_crc16 != ccit_crc16(0xffff, (uint8_t *)&sys_cfg, offsetof(syscfg_t, ccit_crc16) - offsetof(syscfg_t, cfg_holder))) {
#ifdef DEBUG
		printf("config crc error, default conf loaded\n");
#endif // DEBUG
		// if first time config load default conf
		os_memset(&sys_cfg, 0x00, sizeof(syscfg_t));

		tfp_snprintf(sys_cfg.sta_ssid, 64, "%s", STA_SSID);
		tfp_snprintf(sys_cfg.sta_pwd, 64, "%s", STA_PASS);
		sys_cfg.sta_type = STA_TYPE;
#ifdef AP
		sys_cfg.ap_enabled = true;
#else
		sys_cfg.ap_enabled = false;
#endif
		tfp_snprintf(sys_cfg.device_id, 16, MQTT_CLIENT_ID, system_get_chip_id());
		tfp_snprintf(sys_cfg.mqtt_host, 64, "%s", MQTT_HOST);
		sys_cfg.mqtt_port = MQTT_PORT;
		tfp_snprintf(sys_cfg.mqtt_user, 32, "%s", MQTT_USER);
		tfp_snprintf(sys_cfg.mqtt_pass, 32, "%s", MQTT_PASS);

		sys_cfg.security = DEFAULT_SECURITY;	//default non ssl
		
		memcpy(sys_cfg.key, key, sizeof(key));

		sys_cfg.mqtt_keepalive = MQTT_KEEPALIVE;
#ifdef IMPULSE
		tfp_snprintf(sys_cfg.impulse_meter_serial, METER_SERIAL_LEN, DEFAULT_METER_SERIAL);
		tfp_snprintf(sys_cfg.impulse_meter_energy, 2, "0");
		tfp_snprintf(sys_cfg.impulses_per_kwh, 4, "100");
		sys_cfg.impulse_meter_count = 0;
#else
		sys_cfg.ac_thermo_state = 0;
		sys_cfg.offline_close_at = 0;
#ifndef NO_CRON
		memset(&sys_cfg.cron_jobs, 0, sizeof(cron_job_t));
#endif	// NO_CRON
#endif	// IMPULSE

		INFO(" default configuration\r\n");

		cfg_save(NULL, NULL);
	}
	else {
#ifdef DEBUG
		printf("config crc ok\n");
#endif // DEBUG
	}
}

void ICACHE_FLASH_ATTR
cfg_save_defered() {
	os_timer_disarm(&config_save_timer);
	os_timer_setfn(&config_save_timer, (os_timer_func_t *)config_save_timer_func, NULL);
	os_timer_arm(&config_save_timer, SAVE_DEFER_TIME, 0);
}

bool ICACHE_FLASH_ATTR cfg_save_ssid_pwd(char *ssid_pwd, uint16_t *calculated_crc, uint16_t *saved_crc) {
	// for parsing query string formatted parameters
	char *str;
	char *query_string_key, *query_string_value;
	char query_string_key_value[MQTT_MESSAGE_L];
	char *context_query_string, *context_key_value;

	char ssid_pwd_copy[COMMAND_PARAMS_L];

	strncpy(ssid_pwd_copy, ssid_pwd, COMMAND_PARAMS_L);	// make a copy since strtok_r() changes it

	str = strtok_r(ssid_pwd_copy, "&", &context_query_string);
	while (str != NULL) {
		strncpy(query_string_key_value, str, MQTT_MESSAGE_L);
		query_string_key = strtok_r(query_string_key_value, "=", &context_key_value);
		query_string_value = strtok_r(NULL, "=", &context_key_value);
		if (strncmp(query_string_key, "ssid", MQTT_MESSAGE_L) == 0) {
			// un-escape & and =
			query_string_unescape(query_string_value);
			
			// change sta_ssid, save if different
#ifdef DEBUG
			printf("key: %s value: %s\n", query_string_key, query_string_value);
#endif	// DEBUG
			if (strncmp(sys_cfg.sta_ssid, query_string_value, 32 - 1) != 0) {
				memset(sys_cfg.sta_ssid, 0, sizeof(sys_cfg.sta_ssid));
				strncpy(sys_cfg.sta_ssid, query_string_value, 32 - 1);
			}
		}
		else if (strncmp(query_string_key, "pwd", MQTT_MESSAGE_L) == 0) {
			// change sta_pwd, save if different
			if (query_string_value == 0) {
				// there is no value - no password used, use null string
#ifdef DEBUG
				printf("key: %s value: %s\n", query_string_key, "null");
#endif	// DEBUG
				if (strncmp(sys_cfg.sta_pwd, "", 1) != 0) {
					memset(sys_cfg.sta_pwd, 0, sizeof(sys_cfg.sta_pwd));
					strncpy(sys_cfg.sta_pwd, "", 1);
				}
			}
			else {
				// un-escape & and =
				query_string_unescape(query_string_value);
#ifdef DEBUG
				printf("key: %s value: %s\n", query_string_key, query_string_value);
#endif	// DEBUG
				if (strncmp(sys_cfg.sta_pwd, query_string_value, 64 - 1) != 0) {
					memset(sys_cfg.sta_pwd, 0, sizeof(sys_cfg.sta_pwd));
					strncpy(sys_cfg.sta_pwd, query_string_value, 64 - 1);
				}
			}
		}
		str = strtok_r(NULL, "&", &context_query_string);
	}
	return cfg_save(calculated_crc, saved_crc);
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

		cfg_save(NULL, NULL);		
	}
}
