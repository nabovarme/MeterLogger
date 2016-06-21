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

__attribute__((aligned(4))) uint8_t sha256_hash[SHA256_DIGEST_LENGTH];
__attribute__((aligned(4))) uint8_t aes_key[16];
__attribute__((aligned(4))) uint8_t hmac_sha256_key[16];

void init_aes_hmac_combined(const uint8_t *key) {
    uint i;
    
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
    
    sha256_raw(key, sizeof(key), sha256_hash);
    
    // first 16 bytes is aes key
    memcpy(aes_key, sha256_hash, sizeof(aes_key));
    // last 16 bytes is hmac sha256 key
    memcpy(hmac_sha256_key, sha256_hash + sizeof(aes_key), sizeof(hmac_sha256_key));
}

ICACHE_FLASH_ATTR
size_t encrypt_aes_hmac_combined(uint8_t *dst, const uint8_t*src, size_t len) {
    hmac_sha256_ctx_t hctx;
    uint i;
    
    int return_l;
    
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
    
    // encrypt
    memset(dst, 0, sizeof(dst));
    // get random iv in first 16 bytes of mqtt_message
    os_get_random(dst + SHA256_DIGEST_LENGTH, 16);
#ifdef DEBUG
	system_soft_wdt_stop();
    printf("iv: ");
    for (i = 0; i < 16; i++) {
        printf("%02x", dst[i + SHA256_DIGEST_LENGTH]);
    }
    printf("\n");
    
    printf("cleartext: %s\n", src);
	system_soft_wdt_restart();
#endif
    // calculate blocks needed for encrypted string
    return_l = strlen(src) + 1;
    if (return_l % 16) {
        return_l = (return_l / 16) * 16 + 16;
    }
    else {
        return_l = (return_l / 16) * 16;
    }
    AES128_CBC_encrypt_buffer(dst + SHA256_DIGEST_LENGTH + 16, src, return_l, aes_key, dst + SHA256_DIGEST_LENGTH);	// first 32 bytes of mqtt_message contains hmac sha256, next 16 bytes contains IV
    return_l += SHA256_DIGEST_LENGTH + 16;
    
#ifdef DEBUG
	system_soft_wdt_stop();
    printf("iv + ciphertext: ");
    for (i = SHA256_DIGEST_LENGTH; i < return_l; i++) {
        printf("%02x", dst[i]);
    }
    printf("\n");
	system_soft_wdt_restart();
#endif
    
    
    // hmac sha256
    hmac_sha256_init(&hctx, hmac_sha256_key, sizeof(hmac_sha256_key));
    hmac_sha256_update(&hctx, dst + SHA256_DIGEST_LENGTH, return_l - SHA256_DIGEST_LENGTH);
    hmac_sha256_final(&hctx, dst);
#ifdef DEBUG
	system_soft_wdt_stop();
    printf("hmac sha256: ");
    for (i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        printf("%02x", dst[i]);
    }
    printf("\n");
    
    printf("mqtt_message: ");
    for (i = 0; i < return_l; i++) {
        printf("%02x", dst[i]);
    }
    printf("\n");
	system_soft_wdt_restart();
#endif
    
    return return_l;
}

ICACHE_FLASH_ATTR
void decrypt_aes_hmac_combined(uint8_t *dst, const uint8_t*src, size_t len) {
    
}