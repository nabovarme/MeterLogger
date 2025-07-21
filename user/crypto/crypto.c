//
//  crypto.c
//  crypto_test
//
//  Created by stoffer on 21/06/2016.
//  Copyright © 2016 stoffer. All rights reserved.
//

#include <esp8266.h>
#include <string.h>
#include "crypto/crypto.h"
#include "crypto/aes.h"
#include "crypto/sha256.h"
#include "crypto/hmac-sha256.h"
#include "user_config.h"

// variables passed to crypto library have to be int32_t aligned becouse they are casted and accessed as such
uint8_t sha256_hash[SHA256_DIGEST_LENGTH];
uint8_t aes_key[16];
uint8_t hmac_sha256_key[16];

void init_aes_hmac_combined(uint8_t *key) {
#ifdef DEBUG
	unsigned int i;
#endif
	_align_32_bit uint8_t master_key[16];
	
	memcpy(master_key, key, 16);
	
#ifdef DEBUG
	system_soft_wdt_stop();
	printf("master key: ");
	for (i = 0; i < 16; i++) {
		printf("%02x", key[i]);
	}
	printf("\n");
	system_soft_wdt_restart();
#endif	
	// generate aes_key and hmac_sha256_key from master_key
	memset(sha256_hash, 0, sizeof(sha256_hash));
	
	sha256_raw(master_key, 16, sha256_hash);
	
	// first 16 bytes is aes key
	memcpy(aes_key, sha256_hash, sizeof(aes_key));
	// last 16 bytes is hmac sha256 key
	memcpy(hmac_sha256_key, sha256_hash + sizeof(aes_key), sizeof(hmac_sha256_key));
	
#ifdef DEBUG
	system_soft_wdt_stop();
	// print keys
	printf("aes key: ");
	for (i = 0; i < 16; i++) {
		printf("%02x", aes_key[i]);
	}
	printf("\n");
	
	printf("hmac sha256 key: ");
	for (i = 0; i < 16; i++) {
		printf("%02x", hmac_sha256_key[i]);
	}
	printf("\n");
	system_soft_wdt_restart();
#endif
}

ICACHE_FLASH_ATTR
size_t encrypt_aes_hmac_combined(uint8_t *dst, uint8_t *topic, size_t topic_l, uint8_t *message, size_t message_l) {
	hmac_sha256_ctx_t hctx;
	size_t i;
	size_t padded_len;
	size_t total_len;
	uint8_t pad;

	// Generate random IV (after HMAC field)
	os_get_random(dst + SHA256_DIGEST_LENGTH, 16);

	// PKCS#7 padding
	pad = 16 - (message_l % 16);
	for (i = 0; i < pad; i++) {
		message[message_l + i] = pad;
	}
	padded_len = message_l + pad;

	// AES-CBC encrypt: ciphertext goes after HMAC + IV
	AES128_CBC_encrypt_buffer(dst + SHA256_DIGEST_LENGTH + 16,
							  message,
							  padded_len,
							  aes_key,
							  dst + SHA256_DIGEST_LENGTH);

	total_len = SHA256_DIGEST_LENGTH + 16 + padded_len;

	// HMAC-SHA256 over topic + IV + ciphertext
	hmac_sha256_init(&hctx, hmac_sha256_key, sizeof(hmac_sha256_key));
	hmac_sha256_update(&hctx, topic, topic_l);
	hmac_sha256_update(&hctx, dst + SHA256_DIGEST_LENGTH, total_len - SHA256_DIGEST_LENGTH);
	hmac_sha256_final(&hctx, dst);

	return total_len;
}

ICACHE_FLASH_ATTR
size_t decrypt_aes_hmac_combined(uint8_t *dst, uint8_t *topic, size_t topic_l, uint8_t *message, size_t message_l) {
	hmac_sha256_ctx_t hctx;
	uint8_t calculated_hmac_sha256[SHA256_DIGEST_LENGTH];
	size_t encrypted_len;
	uint8_t pad;
	size_t i;

	if (message_l < SHA256_DIGEST_LENGTH + 16) {
		// Too short to be valid (HMAC + IV missing)
		return 0;
	}

	// Verify HMAC-SHA256
	hmac_sha256_init(&hctx, hmac_sha256_key, sizeof(hmac_sha256_key));
	hmac_sha256_update(&hctx, topic, topic_l);
	hmac_sha256_update(&hctx, message + SHA256_DIGEST_LENGTH, message_l - SHA256_DIGEST_LENGTH);
	hmac_sha256_final(&hctx, calculated_hmac_sha256);

	if (memcmp(calculated_hmac_sha256, message, SHA256_DIGEST_LENGTH) != 0) {
		return 0;  // Authentication failed
	}

	encrypted_len = message_l - (SHA256_DIGEST_LENGTH + 16);

	AES128_CBC_decrypt_buffer(dst,
							  message + SHA256_DIGEST_LENGTH + 16,
							  encrypted_len,
							  aes_key,
							  message + SHA256_DIGEST_LENGTH);

	// Validate and remove PKCS#7 padding
	if (encrypted_len == 0) {
		return 0;
	}

	pad = dst[encrypted_len - 1];
	if (pad == 0 || pad > 16) {
		return 0;  // Invalid padding
	}

	for (i = 0; i < pad; i++) {
		if (dst[encrypted_len - 1 - i] != pad) {
			return 0;  // Corrupted padding
		}
	}

	return encrypted_len - pad;  // Actual plaintext length
}
