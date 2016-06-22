


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

uint8_t mqtt_message[MQTT_MESSAGE_L];
int mqtt_message_l;

uint8_t buffer[MQTT_MESSAGE_L];
int buffer_l;

int main(int argc, const char * argv[]) {
    // init
    init_aes_hmac_combined(master_key, sizeof(master_key));
    
    // encrypt
    mqtt_message_l = encrypt_aes_hmac_combined(mqtt_message, cleartext, strlen(cleartext) + 1);

    // decrypt
    memset(buffer, 0, sizeof(buffer));
    mqtt_message_l = decrypt_aes_hmac_combined(buffer, mqtt_message, mqtt_message_l);

//    test_decrypt_cbc();
    return 0;
}
