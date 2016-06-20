


#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include "crypto/aes.h"
#include "crypto/sha256.h"
#include "crypto/hmac-sha256.h"
#include <esp8266.h>

#define MQTT_MESSAGE_L (192 + 1)

uint8_t master_key[] = { 0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6, 0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c };

uint8_t sha256_hash[SHA256_DIGEST_LENGTH];
uint8_t aes_key[16];
uint8_t hmac_sha256_key[16];

uint8_t cleartext[] = "heap=21376&t1=23.61 C&t2=22.19 C&tdif=1.42 K&flow1=0 l/h&effect1=0.0 kW&hr=73327 h&v1=1321.27 m3&e1=56.726 MWh&";

uint8_t mqtt_message[MQTT_MESSAGE_L];
int mqtt_message_l;

/*
uint8_t ciphertext[] = { 0x1a, 0x2c, 0x45, 0x1a, 0xe5, 0x0b, 0xe2, 0x2e, 0xa7, 0xf0, 0x1a, 0x91, 0xbe, 0x23, 0xe1, 0x6e,
    0x9b, 0x19, 0x7e, 0xc5, 0x44, 0x65, 0x45, 0x8a, 0xcb, 0x4a, 0x7e, 0xc7, 0xd4, 0x97, 0x2c, 0x17,
    0xed, 0x53, 0x31, 0x04, 0xde, 0xf6, 0x60, 0xa9, 0xad, 0x93, 0xdb, 0x08, 0x51, 0x6c, 0xa0, 0x83,
    0x53, 0xba, 0xd5, 0x89, 0x55, 0x9e, 0xea, 0x2d, 0x45, 0xa0, 0x1e, 0xbd, 0xf2, 0x53, 0x0c, 0x53,
    0xa4, 0xcd, 0x98, 0x2a, 0x24, 0x04, 0xd1, 0xb6, 0xe6, 0x07, 0x33, 0x96, 0x16, 0x40, 0x0c, 0x13,
    0x47, 0x16, 0x1b, 0x30, 0xf8, 0x70, 0xbe, 0x0c, 0xe7, 0x46, 0xf6, 0xe4, 0x5c, 0x5c, 0xeb, 0x7b,
    0xc0, 0xb6, 0x0b, 0x99, 0xf2, 0xc1, 0x3d, 0xe8, 0xdd, 0x80, 0xaf, 0x4f, 0x79, 0x60, 0x70, 0x58,
    0x0b, 0x92, 0x6e, 0x90, 0xf0, 0x26, 0x19, 0x07, 0x19, 0xe6, 0xc3, 0x45, 0xc8, 0xca, 0x67, 0xd0 };
uint8_t iv[] = { 0xc9, 0x2e, 0x23, 0x4a, 0xd7, 0x5e, 0xda, 0x49, 0xc8, 0x4a, 0xed, 0xd1, 0x64, 0x3b, 0xde, 0x63 };
*/

uint8_t buffer[1024];

void os_get_random(char *s, size_t length) {
    uint i;
    srand(time(NULL));
    for (i = 0; i < length; i++) {
        s[i] = rand()%256;
    }
}

void aes_encrypt_test() {
    uint i;

    memset(mqtt_message, 0, sizeof(mqtt_message));
    // get random iv in first 16 bytes of mqtt_message
    os_get_random(mqtt_message + SHA256_DIGEST_LENGTH, 16);
    printf("iv: ");
    for (i = 0; i < 16; i++) {
        printf("%02x", mqtt_message[i + SHA256_DIGEST_LENGTH]);
    }
    printf("\n");

    printf("cleartext: %s\n", cleartext);

    // calculate blocks needed for encrypted string
    mqtt_message_l = strlen(cleartext) + 1;
    if (mqtt_message_l % 16) {
        mqtt_message_l = (mqtt_message_l / 16) * 16 + 16;
    }
    else {
        mqtt_message_l = (mqtt_message_l / 16) * 16;
    }
    AES128_CBC_encrypt_buffer(mqtt_message + SHA256_DIGEST_LENGTH + 16, cleartext, mqtt_message_l, aes_key, mqtt_message + SHA256_DIGEST_LENGTH);	// first 32 bytes of mqtt_message contains hmac sha256, next 16 bytes contains IV
    mqtt_message_l += SHA256_DIGEST_LENGTH + 16;

    printf("iv + ciphertext: ");
    for (i = SHA256_DIGEST_LENGTH; i < mqtt_message_l; i++) {
        printf("%02x", mqtt_message[i]);
    }
    printf("\n");
}

void hmac_sha256_test() {
    uint i;
    
    hmac_sha256_ctx_t hctx;
    
    hmac_sha256_init(&hctx, hmac_sha256_key, sizeof(hmac_sha256_key));
    hmac_sha256_update(&hctx, mqtt_message + SHA256_DIGEST_LENGTH, mqtt_message_l - SHA256_DIGEST_LENGTH);
    hmac_sha256_final(&hctx, mqtt_message);
    printf("hmac sha256: ");
    for (i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        printf("%02x", mqtt_message[i]);
    }
    printf("\n");

    printf("mqtt_message: ");
    for (i = 0; i < mqtt_message_l; i++) {
        printf("%02x", mqtt_message[i]);
    }
    printf("\n");
}

int main(int argc, const char * argv[]) {
    uint i;

    printf("master key: ");
    for (i = 0; i < 16; i++) {
        printf("%02x", master_key[i]);
    }
    printf("\n");

    // generate aes_key and hmac_sha256_key from master_key
    memset(sha256_hash, 0, sizeof(sha256_hash));
    
    sha256_raw(master_key, sizeof(master_key), sha256_hash);
    memcpy(aes_key, sha256_hash, sizeof(aes_key));
    memcpy(hmac_sha256_key, sha256_hash + sizeof(aes_key), sizeof(hmac_sha256_key));
    
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
    
/*
    memset(buffer, 0, sizeof(buffer));
    memcpy(buffer, "abc", 3);

    AES128_CBC_decrypt_buffer(buffer + 0, ciphertext + 0,  16, aes_key, iv);
    AES128_CBC_decrypt_buffer(buffer + 16, ciphertext + 16, 16, 0, 0);
    AES128_CBC_decrypt_buffer(buffer + 32, ciphertext + 32, 16, 0, 0);
    AES128_CBC_decrypt_buffer(buffer + 48, ciphertext + 48, 16, 0, 0);
    
    printf("%s\n", buffer);
 */
    aes_encrypt_test();
    hmac_sha256_test();
    
    return 0;
}
