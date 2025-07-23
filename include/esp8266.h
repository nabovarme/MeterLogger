// Combined include file for esp8266

#ifndef ESP_CONST_DATA
#define ESP_CONST_DATA	__attribute__((aligned(4))) __attribute__((section(".irom.text")))
#endif

#ifndef IRAM_ATTR
#define IRAM_ATTR __attribute__((section(".iram.text")))
#endif

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef FREERTOS
#include <stdint.h>
#include <espressif/esp_common.h>

#else
#include <c_types.h>
#include <ets_sys.h>
#include <gpio.h>
#include <mem.h>
#include <osapi.h>
#include <user_interface.h>
#include <upgrade.h>
#include <esp_sdk_ver.h>
#endif

#include "platform.h"

// missing from sdk supplied include files
void ets_isr_unmask(unsigned intr);
void ets_isr_mask(unsigned intr);
