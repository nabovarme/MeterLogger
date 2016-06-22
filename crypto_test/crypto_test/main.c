


#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include "crypto/crypto.h"
#include "crypto/aes.h"
#include "crypto/sha256.h"
#include "crypto/hmac-sha256.h"

#include <esp8266.h>

__attribute__((aligned(4))) uint8_t master_key[] = { 0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6, 0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c };

uint8_t cleartext[] = "heap=21376&t1=23.61 C&t2=22.19 C&tdif=1.42 K&flow1=0 l/h&effect1=0.0 kW&hr=73327 h&v1=1321.27 m3&e1=56.726 MWh&";
uint8_t topic[] = "/sample/v2/7210086/1466572820";

uint8_t mqtt_message[MQTT_MESSAGE_L];
int mqtt_message_l;

uint8_t buffer[MQTT_MESSAGE_L];
int buffer_l;

int main(int argc, const char * argv[]) {
    
    // init
    init_aes_hmac_combined(master_key, sizeof(master_key));
    
    // encrypt
    mqtt_message_l = x_encrypt_aes_hmac_combined(mqtt_message, topic, strlen(topic) + 1, cleartext, strlen(cleartext) + 1);

    // decrypt
    memset(buffer, 0, sizeof(buffer));
    mqtt_message_l = x_decrypt_aes_hmac_combined(buffer, topic, strlen(topic) + 1, mqtt_message, mqtt_message_l);

    printf("----\n");
    sha256_ctx_t context;
    
    uint8_t a[] = "aaa";
    uint8_t b[] = "bbb";
    uint8_t ab[] = "aaabbb";

    sha256_init(&context);
    sha256_update(&context, a, strlen(a) + 1);
    sha256_update(&context, b, strlen(b) + 1);
    sha256_final(&context, buffer);
    printf("aaa+bbb sha256: ");
    for (int i = 0; i < 32; i++) {
        printf("%02x", buffer[i]);
    }
    printf("\n");
    
    sha256_init(&context);
    sha256_update(&context, ab, strlen(ab));
    sha256_final(&context, buffer);
    printf("aaabbb sha256: ");
    for (int i = 0; i < 32; i++) {
        printf("%02x", buffer[i]);
    }
    printf("\n");
    
    return 0;
}
