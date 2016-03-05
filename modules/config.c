/*
/* config.c
*
* Copyright (c) 2014-2015, Tuan PM <tuanpm at live dot com>
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* * Redistributions of source code must retain the above copyright notice,
* this list of conditions and the following disclaimer.
* * Redistributions in binary form must reproduce the above copyright
* notice, this list of conditions and the following disclaimer in the
* documentation and/or other materials provided with the distribution.
* * Neither the name of Redis nor the names of its contributors may be used
* to endorse or promote products derived from this software without
* specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
* AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
* IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
* ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
* LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
* CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
* SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
* CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
* ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
* POSSIBILITY OF SUCH DAMAGE.
*/
#include <esp8266.h>

#include "mqtt.h"
#include "config.h"
#include "user_config.h"
#include "debug.h"

syscfg_t sys_cfg;
SAVE_FLAG saveFlag;

#define SAVE_DEFER_TIME 2000
static os_timer_t config_save_timer;
char config_save_timer_running;

void ICACHE_FLASH_ATTR
cfg_save() {
	 //INFO("cfg_save() essid: %s pw: %s\n", sys_cfg.sta_ssid, sys_cfg.sta_pwd);
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
}

void ICACHE_FLASH_ATTR
cfg_load() {
	INFO("\r\nload ...\r\n");
	/*
	char essid[128];
	char passwd[128];
	os_memset(essid, 0x00, sizeof essid);
	os_memset(passwd, 0x00, sizeof passwd);
	os_strncpy(essid, (char*)sys_cfg.sta_ssid, 32);
	os_strncpy(passwd, (char*)sys_cfg.sta_pwd, 64);
	INFO("cfg_load() essid: %s pw: %s\n", essid, passwd);
	*/
	
	spi_flash_read((CFG_LOCATION + 3) * SPI_FLASH_SEC_SIZE,
				   (uint32 *)&saveFlag, sizeof(SAVE_FLAG));
	if (saveFlag.flag == 0) {
		spi_flash_read((CFG_LOCATION + 0) * SPI_FLASH_SEC_SIZE,
					   (uint32 *)&sys_cfg, sizeof(syscfg_t));
	} else {
		spi_flash_read((CFG_LOCATION + 1) * SPI_FLASH_SEC_SIZE,
					   (uint32 *)&sys_cfg, sizeof(syscfg_t));
	}
	if(sys_cfg.cfg_holder != CFG_HOLDER){
		os_memset(&sys_cfg, 0x00, sizeof sys_cfg);


		sys_cfg.cfg_holder = CFG_HOLDER;

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

		INFO(" default configuration\r\n");

		cfg_save();
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
