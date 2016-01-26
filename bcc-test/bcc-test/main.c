//
//  main.c
//  bcc-test
//
//  Created by stoffer on 29/11/2015.
//  Copyright © 2015 stoffer. All rights reserved.
//

#include <stdio.h>
#include <strings.h>

#define EN61107_FRAME_L 256
#define EN61107_REGISTER_L 32

char data[EN61107_FRAME_L] = "\x2F\x4B\x41\x4D\x20\x4D\x43\x0D\x0A\x02\x30\x2E\x30\x28\x30\x30\x30\x30\x30\x30\x30\x30\x30\x34\x30\x29\x0D\x0A\x36\x2E\x38\x28\x30\x30\x30\x30\x30\x2E\x30\x30\x2A\x4D\x57\x68\x29\x0D\x0A\x36\x2E\x32\x36\x28\x30\x30\x30\x30\x30\x30\x2E\x30\x2A\x6D\x33\x29\x0D\x0A\x36\x2E\x33\x31\x28\x30\x30\x35\x33\x31\x38\x39\x2A\x68\x29\x21\x0D\x0A\x03\x0e";

unsigned int data_length = 86;

int parse_en61107_frame(char *en61107_frame, unsigned int en61107_frame_length) {
    unsigned int i;
    uint8_t bcc;
    uint8_t calculated_bcc;
    
    const char *separator = "\x0D\x0A";
    const char *stx = "\x02";
    const char *etx = "\x21\x0D\x0A\x03";
    char value_string[EN61107_REGISTER_L];
    char model_string[EN61107_REGISTER_L];
    char registers_string[EN61107_FRAME_L];
    char *registers_string_ptr;
    char *pos;
    unsigned int length;
    
    // sanity check
    if (!en61107_frame_length) {
        return 0;
    }
    
    bcc = en61107_frame[en61107_frame_length - 1];
    calculated_bcc = 0;
    for (i = 0; i < data_length; i++) {
        printf("#%d %c (0x%x)\n", i, en61107_frame[i], en61107_frame[i]);
        if (en61107_frame[i] == 0x02) {
            // stx
            calculated_bcc = 0;
        }
        else {
            calculated_bcc += en61107_frame[i];
            calculated_bcc &= 0x7f;  // 7 bit value
            
            if (en61107_frame[i] == 0x03) {
                // etx
                if (bcc == calculated_bcc) {
                    // crc ok
                    memset(model_string, 0, EN61107_REGISTER_L);    // clear model_string (null terminalte)
                    pos = strstr(en61107_frame, stx);               // find position of stx char
                    if (pos != NULL) {                              // if found stx char
                        length = pos - en61107_frame;
                        memcpy(model_string, en61107_frame, length - 2);
                        printf(">%s<\n", model_string);
                        en61107_frame += length + 3;
                    }
                    
                    memset(registers_string, 0, EN61107_FRAME_L);
                    pos = strstr(en61107_frame, etx);
                    if (pos != NULL) {
                        length = pos - en61107_frame;
                        memcpy(registers_string, en61107_frame, length);
                        printf(">%s<\n", registers_string);
                        //en61107_frame += length + 2;
                    }

                    memset(value_string, 0, EN61107_REGISTER_L);
                    registers_string_ptr = registers_string;
                    do {
                        pos = strstr(registers_string_ptr, separator);
                        if (pos != NULL) {
                            length = pos - registers_string_ptr;
                            memcpy(value_string, registers_string_ptr, length);
                            printf(">%s<\n", value_string);

                            registers_string_ptr += length + 2;
                        }
                    }
                    while (pos != NULL);
                    //length = strstr(en61107_frame, separator) - en61107_frame;
                    //memcpy(value_string, en61107_frame, length);
                    return 1;
                }
            }
        }
    }
    return 0;
}


int main(int argc, const char * argv[]) {
    if (parse_en61107_frame(data, data_length)) {
        printf("bcc ok\n");
    }
    else {
        printf("bcc error\n");
    }
    return 0;
}

