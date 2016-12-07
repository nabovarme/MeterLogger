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
#include <stdbool.h>
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
    char value[EN61107_VALUE_L];
    char unit[EN61107_UNIT_L];
} en61107_response_register_t;

typedef struct {
    char serial[EN61107_SERIAL_L];
    char meter_type[EN61107_METER_TYPE_L];
    en61107_response_register_t t1;
    en61107_response_register_t t2;
    en61107_response_register_t t3;
    en61107_response_register_t flow1;
    en61107_response_register_t effect1;
    en61107_response_register_t hr;
    en61107_response_register_t v1;
    en61107_response_register_t e1;
} en61107_response_t;

en61107_response_t en61107_response;

//char data[EN61107_FRAME_L] = "\x2F\x4B\x41\x4D\x20\x4D\x43\x0D\x0A\x02\x30\x2E\x30\x28\x30\x30\x30\x30\x30\x30\x30\x30\x30\x34\x30\x29\x0D\x0A\x36\x2E\x38\x28\x30\x30\x30\x30\x30\x2E\x30\x30\x2A\x4D\x57\x68\x29\x0D\x0A\x36\x2E\x32\x36\x28\x30\x30\x30\x30\x30\x30\x2E\x30\x2A\x6D\x33\x29\x0D\x0A\x36\x2E\x33\x31\x28\x30\x30\x35\x33\x31\x38\x39\x2A\x68\x29\x21\x0D\x0A\x03\x0e";

char data[EN61107_FRAME_L] = "\x2f\x4b\x41\x4d\x20\x4d\x43\x0d\x0a\x02\x30\x2e\x30\x28\x30\x30\x30\x30\x30\x30\x30\x30\x30\x34\x30\x29\x0d\x0a\x36\x2e\x38\x28\x30\x30\x30\x30\x30\x2e\x30\x30\x2a\x4d\x57\x68\x29\x0d\x0a\x36\x2e\x32\x36\x28\x30\x30\x30\x30\x30\x30\x2e\x30\x2a\x6d\x33\x29\x0d\x0a\x36\x2e\x33\x31\x28\x30\x30\x35\x36\x32\x39\x36\x2a\x68\x29\x21\x0d\x0a\x03\x10";

unsigned int data_length = 86;

void en61107_response_set_value(en61107_response_t *response, char *rid, char *value, unsigned int value_length) {
    if (strncmp(rid, "6.8", EN61107_RID_L) == 0) {
        // energy
        strncpy(response->e1.value, value, value_length);
    }
    else if (strncmp(rid, "6.26", EN61107_RID_L) == 0) {
        // volume
        strncpy(response->v1.value, value, value_length);
    }
    else if (strncmp(rid, "6.31", EN61107_RID_L) == 0) {
        // hours
        strncpy(response->hr.value, value, value_length);
    }
}

void en61107_response_set_unit(en61107_response_t *response, char *rid, char *unit, unsigned int unit_length) {
    if (strncmp(rid, "6.8", EN61107_RID_L) == 0) {
        // energy
        strncpy(response->e1.unit, unit, unit_length);
    }
    else if (strncmp(rid, "6.26", EN61107_RID_L) == 0) {
        // volume
        strncpy(response->v1.unit, unit, unit_length);
    }
    else if (strncmp(rid, "6.31", EN61107_RID_L) == 0) {
        // hours
        strncpy(response->hr.unit, unit, unit_length);
    }
}

bool parse_en61107_frame(en61107_response_t *response, char *frame, unsigned int frame_length) {
    unsigned int i, j;
    uint8_t bcc;
    uint8_t calculated_bcc;
    
    const char *separator = "\x0D\x0A";
    const char *stx = "\x02";
    const char *etx = "\x21\x0D\x0A\x03";
    char rid[EN61107_RID_L];
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
    if (!frame_length) {
        return 0;
    }
    
    bcc = frame[frame_length - 1];
    calculated_bcc = 0;
    for (i = 0; i < frame_length; i++) {
        if (frame[i] == 0x02) {
            // stx
            calculated_bcc = 0;
        }
        else {
            calculated_bcc += frame[i];
            calculated_bcc &= 0x7f;  // 7 bit value
            
            if (frame[i] == 0x03) {
                // etx
                if (bcc == calculated_bcc) {
                    // crc ok
                    // parse meter_type
                    memset(response, 0, sizeof(en61107_response_t));
                    memset(meter_type_string, 0, EN61107_REGISTER_L);    // clear meter_type_string (null terminalte)
                    pos = strstr(frame, stx);               // find position of stx char
                    if (pos != NULL) {                              // if found stx char...
                        length = pos - frame;               // ...save meter_type string
                        // DEBUG: check if length - 3 is >= 0
                        memcpy(response->meter_type, frame + 1, length - 3);
                        frame += length + 3;
                    }
                    
                    // parse to etx and save to register_list_string
                    memset(register_list_string, 0, EN61107_FRAME_L);
                    pos = strstr(frame, etx);               // find position of etx
                    if (pos != NULL) {
                        length = pos - frame;
                        memcpy(register_list_string, frame, length);
                    }

                    // parse values
                    j = 0;
                    memset(rid_value_unit_string, 0, EN61107_REGISTER_L);
                    register_list_string_ptr = register_list_string;        // force pointer arithmetics
                    while ((pos = strstr(register_list_string_ptr, separator)) != NULL) {
                        length = pos - register_list_string_ptr;
                        memcpy(rid_value_unit_string, register_list_string_ptr, length);
                        rid_value_unit_string_ptr = rid_value_unit_string;  // force pointer arithmetics

                        // parse register number, value and unit unless rid == 0
                        pos = strstr(rid_value_unit_string, "(");
                        if (pos != NULL) {
                            rid_string_length = pos - rid_value_unit_string;
                            memcpy(rid, rid_value_unit_string, rid_string_length);
                            rid_value_unit_string_ptr += rid_string_length + 1;
                        }
                        pos = strstr(rid_value_unit_string_ptr, "*");
                        if (pos != NULL) {
                            value_string_length = pos - rid_value_unit_string_ptr;
                            en61107_response_set_value(response, rid, rid_value_unit_string_ptr, value_string_length);
                            rid_value_unit_string_ptr += value_string_length + 1;
                        }
                        pos = strstr(rid_value_unit_string_ptr, ")");
                        if (pos != NULL) {
                            if (strncmp(rid, "0", 1) == 0) {
                                // serial number, no unit
                                serial_string_length = pos - rid_value_unit_string_ptr;
                                memcpy(response->serial, rid_value_unit_string_ptr, serial_string_length);
                            }
                            else {
                                unit_string_length = pos - rid_value_unit_string_ptr;
                                en61107_response_set_unit(response, rid, rid_value_unit_string_ptr, unit_string_length);
                                j++;

                            }
                        }

                        register_list_string_ptr += length + 2;
                    }
                    
                    
                    // handle the last
                    length = strlen(register_list_string_ptr);
                    memcpy(rid_value_unit_string, register_list_string_ptr, length);
                    rid_value_unit_string_ptr = rid_value_unit_string;  // force pointer arithmetics
                    
                    // parse register number, value and unit unless rid == 0
                    pos = strstr(rid_value_unit_string, "(");
                    if (pos != NULL) {
                        rid_string_length = pos - rid_value_unit_string;
                        memcpy(rid, rid_value_unit_string, rid_string_length);
                        rid_value_unit_string_ptr += rid_string_length + 1;
                    }
                    pos = strstr(rid_value_unit_string_ptr, "*");
                    if (pos != NULL) {
                        value_string_length = pos - rid_value_unit_string_ptr;
                        en61107_response_set_value(response, rid, rid_value_unit_string_ptr, value_string_length);
                        rid_value_unit_string_ptr += value_string_length + 1;
                    }
                    pos = strstr(rid_value_unit_string_ptr, ")");
                    if (pos != NULL) {
                        if (strncmp(rid, "0", 1) == 0) {
                            // serial number, no unit
                            serial_string_length = pos - rid_value_unit_string_ptr;
                            memcpy(response->serial, rid_value_unit_string_ptr, serial_string_length);
                        }
                        else {
                            unit_string_length = pos - rid_value_unit_string_ptr;
                            en61107_response_set_unit(response, rid, rid_value_unit_string_ptr, unit_string_length);
                        }
                    }

                    return true;
                }
            }
        }
    }
    return false;
}


int main(int argc, const char * argv[]) {
    if (parse_en61107_frame(&en61107_response, data, data_length)) {
        printf("bcc ok\n");
        
        printf("serial: %s\n", en61107_response.serial);
        printf("meter type: %s\n", en61107_response.meter_type);
        printf("e1: %s %s\n", en61107_response.e1.value, en61107_response.e1.unit);
        printf("v1: %s %s\n", en61107_response.v1.value, en61107_response.v1.unit);
        printf("hr: %s %s\n", en61107_response.hr.value, en61107_response.v1.unit);
    }
    else {
        printf("bcc error\n");
    }
    return 0;
}

