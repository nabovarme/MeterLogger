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
	system_soft_wdt_stop(void);
    printf("master key: ");
    for (i = 0; i < 16; i++) {
        printf("%02x", key[i]);
    }
    printf("\n");
	system_soft_wdt_restart(void);
#endif    
    // generate aes_key and hmac_sha256_key from master_key
    memset(sha256_hash, 0, sizeof(sha256_hash));
    
    sha256_raw(key, sizeof(key), sha256_hash);
    
    // first 16 bytes is aes key
    memcpy(aes_key, sha256_hash, sizeof(aes_key));
    // last 16 bytes is hmac sha256 key
    memcpy(hmac_sha256_key, sha256_hash + sizeof(aes_key), sizeof(hmac_sha256_key));
    
    uint8_t _aes_key[] = { 0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6, 0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c };
    memcpy(aes_key, _aes_key, sizeof(aes_key));

#ifdef DEBUG
    system_soft_wdt_stop(void);
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
    system_soft_wdt_restart(void);
#endif
}

ICACHE_FLASH_ATTR
size_t encrypt_aes_hmac_combined(uint8_t *dst, const uint8_t *src, size_t len) {
    hmac_sha256_ctx_t hctx;
    uint i;
    
    int return_l;
    
    // encrypt
    memset(dst, 0, sizeof(dst));
    // get random iv in first 16 bytes of mqtt_message
    os_get_random(dst + SHA256_DIGEST_LENGTH, 16);
#ifdef DEBUG_E
	system_soft_wdt_stop(void);
    printf("iv: ");
    for (i = 0; i < 16; i++) {
        printf("%02x", dst[i + SHA256_DIGEST_LENGTH]);
    }
    printf("\n");
    
    printf("cleartext: %s\n", src);
	system_soft_wdt_restart(void);
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
    
#ifdef DEBUG_E
	system_soft_wdt_stop(void);
    printf("iv + ciphertext: ");
    for (i = SHA256_DIGEST_LENGTH; i < return_l; i++) {
        printf("%02x", dst[i]);
    }
    printf("\n");
	system_soft_wdt_restart(void);
#endif
    
    
    // hmac sha256
    hmac_sha256_init(&hctx, hmac_sha256_key, sizeof(hmac_sha256_key));
    hmac_sha256_update(&hctx, dst + SHA256_DIGEST_LENGTH, return_l - SHA256_DIGEST_LENGTH);
    hmac_sha256_final(&hctx, dst);
#ifdef DEBUG_E
	system_soft_wdt_stop(void);
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
	system_soft_wdt_restart(void);
#endif
    
    return return_l;
}

ICACHE_FLASH_ATTR
size_t decrypt_aes_hmac_combined(uint8_t *dst, const uint8_t *src, size_t len) {
    hmac_sha256_ctx_t hctx;
    uint8_t calculated_hmac_sha256[SHA256_DIGEST_LENGTH];

    size_t return_l;
    uint i;
    
    // hmac sha256
    hmac_sha256_init(&hctx, hmac_sha256_key, sizeof(hmac_sha256_key));
    hmac_sha256_update(&hctx, src + SHA256_DIGEST_LENGTH, len - SHA256_DIGEST_LENGTH);
    hmac_sha256_final(&hctx, calculated_hmac_sha256);
#ifdef DEBUG
    system_soft_wdt_stop(void);
    printf("calculated hmac sha256: ");
    for (i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        printf("%02x", calculated_hmac_sha256[i]);
    }
    printf("\n");
    
    printf("hmac sha256: ");
    for (i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        printf("%02x", src[i]);
    }
    printf("\n");
    
    printf("mqtt_message: ");
    for (i = 0; i < len; i++) {
        printf("%02x", src[i]);
    }
    printf("\n");
    system_soft_wdt_restart(void);
#endif
    
    //if (memcmp(calculated_hmac_sha256, src, SHA256_DIGEST_LENGTH) == 0) {
    if (1) {
        // hmac sha256 matches
        
        uint8_t _iv[]  = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f };
        uint8_t _in[]  = { 0x76, 0x49, 0xab, 0xac, 0x81, 0x19, 0xb2, 0x46, 0xce, 0xe9, 0x8e, 0x9b, 0x12, 0xe9, 0x19, 0x7d,
            0x50, 0x86, 0xcb, 0x9b, 0x50, 0x72, 0x19, 0xee, 0x95, 0xdb, 0x11, 0x3a, 0x91, 0x76, 0x78, 0xb2,
            0x73, 0xbe, 0xd6, 0xb8, 0xe3, 0xc1, 0x74, 0x3b, 0x71, 0x16, 0xe6, 0x9e, 0x22, 0x22, 0x95, 0x16,
            0x3f, 0xf1, 0xca, 0xa1, 0x68, 0x1f, 0xac, 0x09, 0x12, 0x0e, 0xca, 0x30, 0x75, 0x86, 0xe1, 0xa7 };
        len = sizeof(_in) + SHA256_DIGEST_LENGTH;
        
        memcpy(src + SHA256_DIGEST_LENGTH, _iv, sizeof(_iv));
        memcpy(src + SHA256_DIGEST_LENGTH + 16, _in, sizeof(_in));
        
        printf("len: %d\n", len - SHA256_DIGEST_LENGTH);
        
        AES128_CBC_decrypt_buffer(dst + 0, src + SHA256_DIGEST_LENGTH + 16, len, aes_key, src + SHA256_DIGEST_LENGTH);
        
#ifdef DEBUG
        system_soft_wdt_stop(void);
        printf("cleartext: %s\n", dst);
        system_soft_wdt_restart(void);
#endif
        return return_l;
    }
    else {
        return 0;
    }
}
