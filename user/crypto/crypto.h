//
//  crypto.h
//  crypto_test
//
//  Created by stoffer on 21/06/2016.
//  Copyright © 2016 stoffer. All rights reserved.
//

#ifndef crypto_h
#define crypto_h

#include <esp8266.h>
#include <stdio.h>

#define _align_32_bit	__attribute__((aligned(4)))

#define is_aligned(POINTER, BYTE_COUNT) \
	(((uintptr_t)(const void *)(POINTER)) % (BYTE_COUNT) == 0)

ICACHE_FLASH_ATTR void init_aes_hmac_combined(uint8_t *key);
ICACHE_FLASH_ATTR size_t encrypt_aes_hmac_combined(uint8_t *dst, uint8_t *topic, size_t topic_l, uint8_t *message, size_t message_l);
ICACHE_FLASH_ATTR size_t decrypt_aes_hmac_combined(uint8_t *dst, uint8_t *topic, size_t topic_l, uint8_t *message, size_t message_l);

#endif /* crypto_h */
