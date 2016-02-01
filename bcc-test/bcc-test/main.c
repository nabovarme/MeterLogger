//
//  main.c
//  bcc-test
//
//  Created by stoffer on 29/11/2015.
//  Copyright © 2015 stoffer. All rights reserved.
//

//  0x0000 : 2F 4B 41 4D 20 4D 43 0D 0A 02 30 2E 30 28 30 30 : /KAM.MC...0.0(00
//  0x0010 : 30 30 30 30 30 30 30 34 30 29 0D 0A 36 2E 38 28 : 000000040)..6.8(
//  0x0020 : 30 30 30 30 30 2E 30 30 2A 4D 57 68 29 0D 0A 36 : 00000.00*MWh)..6
//  0x0030 : 2E 32 36 28 30 30 30 30 30 30 2E 30 2A 6D 33 29 : .26(000000.0*m3)
//  0x0040 : 0D 0A 36 2E 33 31 28 30 30 35 33 31 38 39 2A 68 : ..6.31(0053189*h
//  0x0050 : 29 21 0D 0A 03 00 00 00 00 00 00 00 00 00 00 00 : )!..............


#include <stdint.h>
#include <stdio.h>
#include <strings.h>

#define EN61107_FRAME_L 256
#define EN61107_REGISTER_L 32
#define EN61107_RID_L 6
#define EN61107_UNIT_L 5
#define EN61107_VALUE_L 10
#define EN61107_SERIAL_L 13
#define EN61107_METER_TYPE_L 8

typedef struct {
    char rid[EN61107_RID_L];
    char unit[EN61107_UNIT_L];
    char value[EN61107_VALUE_L];
} en61107_response_register_t;

typedef en61107_response_register_t en61107_response_register_list_t[8];   // max 8 registers per request

typedef struct {
    char en61107_response_serial[EN61107_SERIAL_L];
    char en61107_response_meter_type[EN61107_METER_TYPE_L];
    en61107_response_register_list_t en61107_response_register_list;
} en61107_response_t;

en61107_response_t en_61107_response;

char data[EN61107_FRAME_L] = "\x2F\x4B\x41\x4D\x20\x4D\x43\x0D\x0A\x02\x30\x2E\x30\x28\x30\x30\x30\x30\x30\x30\x30\x30\x30\x34\x30\x29\x0D\x0A\x36\x2E\x38\x28\x30\x30\x30\x30\x30\x2E\x30\x30\x2A\x4D\x57\x68\x29\x0D\x0A\x36\x2E\x32\x36\x28\x30\x30\x30\x30\x30\x30\x2E\x30\x2A\x6D\x33\x29\x0D\x0A\x36\x2E\x33\x31\x28\x30\x30\x35\x33\x31\x38\x39\x2A\x68\x29\x21\x0D\x0A\x03\x0e";

unsigned int data_length = 86;

int parse_en61107_frame(char *en61107_frame, unsigned int en61107_frame_length) {
    unsigned int i, j;
    uint8_t bcc;
    uint8_t calculated_bcc;
    
    const char *separator = "\x0D\x0A";
    const char *stx = "\x02";
    const char *etx = "\x21\x0D\x0A\x03";
    char meter_type_string[EN61107_REGISTER_L];
    char rid_value_unit_string[EN61107_REGISTER_L];
    char register_list_string[EN61107_FRAME_L];
    char *register_list_string_ptr;
    char *rid_value_unit_string_ptr;
    char *pos;
    size_t length;
    size_t rid_string_length;
    size_t value_string_length;
    size_t unit_string_length;
    size_t serial_string_length;
    
    
    // sanity check
    if (!en61107_frame_length) {
        return 0;
    }
    
    bcc = en61107_frame[en61107_frame_length - 1];
    calculated_bcc = 0;
    for (i = 0; i < data_length; i++) {
        //printf("#%d %c (0x%x)\n", i, en61107_frame[i], en61107_frame[i]);
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
                    // parse meter_type
                    memset(&en_61107_response, 0, sizeof(en_61107_response));
                    memset(meter_type_string, 0, EN61107_REGISTER_L);    // clear meter_type_string (null terminalte)
                    pos = strstr(en61107_frame, stx);               // find position of stx char
                    if (pos != NULL) {                              // if found stx char...
                        length = pos - en61107_frame;               // ...save meter_type string
                        // DEBUG: check if length - 3 is >= 0
                        memcpy(en_61107_response.en61107_response_meter_type, en61107_frame + 1, length - 3);
                        printf("meter type: %s\n", en_61107_response.en61107_response_meter_type);
                        en61107_frame += length + 3;
                    }
                    
                    // parse to etx and save to register_list_string
                    memset(register_list_string, 0, EN61107_FRAME_L);
                    pos = strstr(en61107_frame, etx);               // find position of etx
                    if (pos != NULL) {
                        length = pos - en61107_frame;
                        memcpy(register_list_string, en61107_frame, length);
                    }

                    // parse values
                    j = 0;
                    memset(rid_value_unit_string, 0, EN61107_REGISTER_L);
                    register_list_string_ptr = register_list_string;        // force pointer arithmetics
                    while ((pos = strstr(register_list_string_ptr, separator)) != NULL) {
                        length = pos - register_list_string_ptr;
                        memcpy(rid_value_unit_string, register_list_string_ptr, length);
                        //printf(">%s<\n", rid_value_unit_string);
                        rid_value_unit_string_ptr = rid_value_unit_string;  // force pointer arithmetics

                        // parse register number, value and unit unless rid == 0
                        pos = strstr(rid_value_unit_string, "(");
                        if (pos != NULL) {
                            rid_string_length = pos - rid_value_unit_string;
                            memcpy(en_61107_response.en61107_response_register_list[j].rid, rid_value_unit_string, rid_string_length);
                            rid_value_unit_string_ptr += rid_string_length + 1;
                        }
                        pos = strstr(rid_value_unit_string_ptr, "*");
                        if (pos != NULL) {
                            value_string_length = pos - rid_value_unit_string_ptr;
                            memcpy(en_61107_response.en61107_response_register_list[j].value, rid_value_unit_string_ptr, value_string_length);
                            rid_value_unit_string_ptr += value_string_length + 1;
                        }
                        pos = strstr(rid_value_unit_string_ptr, ")");
                        if (pos != NULL) {
                            if (strncmp(en_61107_response.en61107_response_register_list[j].rid, "0", 1) == 0) {
                                // serial number, no unit
                                serial_string_length = pos - rid_value_unit_string_ptr;
                                memcpy(en_61107_response.en61107_response_serial, rid_value_unit_string_ptr, serial_string_length);
                                printf("rid: %s serial: %s\n", en_61107_response.en61107_response_register_list[j].rid, en_61107_response.en61107_response_serial);
                            }
                            else {
                                unit_string_length = pos - rid_value_unit_string_ptr;
                                memcpy(en_61107_response.en61107_response_register_list[j].unit, rid_value_unit_string_ptr, unit_string_length);
                                printf("rid: %s value: %s unit: %s\n", en_61107_response.en61107_response_register_list[j].rid, en_61107_response.en61107_response_register_list[j].value, en_61107_response.en61107_response_register_list[j].unit);
                                j++;

                            }
                        }

                        register_list_string_ptr += length + 2;
                    }

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

