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

#define tfp_snprintf snprintf

#define EN61107_FRAME_L 1024
#define EN61107_REGISTER_L 32
#define EN61107_RID_L 6
#define EN61107_UNIT_L 5
#define EN61107_VALUE_L 10
#define EN61107_CUSTOMER_NO_L 13
#define EN61107_METER_TYPE_L 8

typedef struct {
    char value[EN61107_VALUE_L];
    char unit[EN61107_UNIT_L];
} en61107_response_register_t;

typedef struct {
    uint8_t a;
    uint8_t b;
    uint32_t ccc;
    uint16_t dd;
    uint8_t e;
    uint16_t ff;
    uint16_t gg;
} en61107_meter_program_t;

typedef struct {
    char customer_no[EN61107_CUSTOMER_NO_L];
    char meter_type[EN61107_METER_TYPE_L];
    en61107_meter_program_t meter_program;
    en61107_response_register_t t1;
    en61107_response_register_t t2;
    en61107_response_register_t t3;
    en61107_response_register_t tdif;
    en61107_response_register_t flow1;
    en61107_response_register_t effect1;
    en61107_response_register_t hr;
    en61107_response_register_t v1;
    en61107_response_register_t e1;
} en61107_response_t;

typedef enum {
    UART_STATE_NONE,
    UART_STATE_STANDARD_DATA_2,
    UART_STATE_UNKNOWN_1,
    UART_STATE_UNKNOWN_2,
    UART_STATE_SERIAL_BYTE_1,
    UART_STATE_SERIAL_BYTE_2,
    UART_STATE_SERIAL_BYTE_3,
    UART_STATE_SERIAL_BYTE_4,
    UART_STATE_SERIAL_BYTE_5,
    UART_STATE_SERIAL_BYTE_6,
    UART_STATE_UNKNOWN_3,
    UART_STATE_UNKNOWN_4,
    UART_STATE_EN61107,
    UART_STATE_INST_VALUES
} en61107_uart_state_t;

en61107_response_t en61107_response;

//char data[EN61107_FRAME_L] = "\x2F\x4B\x41\x4D\x20\x4D\x43\x0D\x0A\x02\x30\x2E\x30\x28\x30\x30\x30\x30\x30\x30\x30\x30\x30\x34\x30\x29\x0D\x0A\x36\x2E\x38\x28\x30\x30\x30\x30\x30\x2E\x30\x30\x2A\x4D\x57\x68\x29\x0D\x0A\x36\x2E\x32\x36\x28\x30\x30\x30\x30\x30\x30\x2E\x30\x2A\x6D\x33\x29\x0D\x0A\x36\x2E\x33\x31\x28\x30\x30\x35\x33\x31\x38\x39\x2A\x68\x29\x21\x0D\x0A\x03\x0e";

char data[EN61107_FRAME_L] = "\x2f\x4b\x41\x4d\x20\x4d\x43\x0d\x0a\x02\x30\x2e\x30\x28\x30\x30\x30\x30\x30\x30\x30\x30\x30\x34\x30\x29\x0d\x0a\x36\x2e\x38\x28\x30\x30\x30\x30\x30\x2e\x30\x30\x2a\x4d\x57\x68\x29\x0d\x0a\x36\x2e\x32\x36\x28\x30\x30\x30\x30\x30\x30\x2e\x30\x2a\x6d\x33\x29\x0d\x0a\x36\x2e\x33\x31\x28\x30\x30\x35\x36\x32\x39\x36\x2a\x68\x29\x21\x0d\x0a\x03\x10";

unsigned int data_length = 86;

char data2[] = "0002251 -002735 0001820 0000000 0000000 0000020 0000000 0000000 \r";
int data2_length = 66;

unsigned int decimal_number_length(int n) {
    int digits;
    
    digits = n < 0;	//count "minus"
    do {
        digits++;
    } while (n /= 10);
    
    return digits;
}

void cleanup_decimal_str(char *decimal_str, char *cleaned_up_str, unsigned int length) {
    uint32_t value_int, value_frac;
    char *pos;
    uint8_t decimals = 0;
    uint8_t prepend_zeroes;
    char zeroes[8];
    
    memcpy(cleaned_up_str, decimal_str, length);
    cleaned_up_str[length] = 0;	// null terminate
    
    pos = strstr(cleaned_up_str, ".");
    if (pos == NULL) {
        // non fractional number
        value_int = atoi(cleaned_up_str);
        tfp_snprintf(cleaned_up_str, length, "%u", value_int);
    }
    else {
        // parse frac
        while ((*(pos + 1 + decimals) >= '0') && (*(pos + 1 + decimals) <= '9')) {
            // count the decimals
            decimals++;
        }
        value_frac = atoi(pos + 1);
        prepend_zeroes = decimals - decimal_number_length(atoi(pos + 1));
        
        zeroes[0] = 0;	// null terminate
        while (prepend_zeroes--) {
            strcat(zeroes, "0");
        }
        
        // parse int
        strncpy(cleaned_up_str, decimal_str, (pos - cleaned_up_str));
        cleaned_up_str[cleaned_up_str - pos] = 0;	// null terminate
        value_int = atoi(cleaned_up_str);
        
        tfp_snprintf(cleaned_up_str, length, "%u.%s%u", value_int, zeroes, value_frac);
    }
}

void divide_str_by_10(char *str, char *decimal_str) {
    int32_t result_int, result_frac;
    int32_t value_int;
    
    value_int = atoi(str);
    
    // ...divide by 10 and prepare decimal string
    result_int = (int32_t)(value_int / 10);
    if (result_int < 0) {
        result_frac = -1 * (value_int % 10);
    }
    else {
        result_frac = value_int % 100;
    }
    
    tfp_snprintf(decimal_str, 8, "%d.%01u", result_int, result_frac);
}

void divide_str_by_100(char *str, char *decimal_str) {
    int32_t result_int, result_frac;
    int32_t value_int;
    
    value_int = atoi(str);
    
    // ...divide by 100 and prepare decimal string
    result_int = (int32_t)(value_int / 100);
    if (result_int < 0) {
        result_frac = -1 * (value_int % 100);
    }
    else {
        result_frac = value_int % 100;
    }
    
    tfp_snprintf(decimal_str, 8, "%d.%02u", result_int, result_frac);
}

void en61107_response_set_value(en61107_response_t *response, char *rid, char *value, unsigned int value_length) {
    char value_copy[EN61107_VALUE_L];
    
    // make null terminated string - value_length is not including null termination
    memset(value_copy, 0, EN61107_VALUE_L);
    if (value_length >= EN61107_VALUE_L) {
        value_length = EN61107_VALUE_L - 1;
        
    }
    memcpy(value_copy, value, value_length);
    value_copy[value_length] = 0;
    value_length++;
    
    if (strncmp(rid, "6.8", EN61107_RID_L) == 0) {
        // energy
        tfp_snprintf(response->e1.value, value_length, "%s", value_copy);
    }
    else if (strncmp(rid, "6.26", EN61107_RID_L) == 0) {
        // volume
        tfp_snprintf(response->v1.value, value_length, "%s", value_copy);
    }
    else if (strncmp(rid, "6.31", EN61107_RID_L) == 0) {
        // hours
        tfp_snprintf(response->hr.value, value_length, "%s", value_copy);
    }
}

void en61107_response_set_unit(en61107_response_t *response, char *rid, char *unit, unsigned int unit_length) {
    char unit_copy[EN61107_UNIT_L];
    
    // make null terminated string - unit_length is not including null termination
    memset(unit_copy, 0, EN61107_UNIT_L);
    if (unit_length >= EN61107_UNIT_L) {
        unit_length = EN61107_UNIT_L - 1;
        
    }
    memcpy(unit_copy, unit, unit_length);
    unit_copy[unit_length] = 0;
    unit_length++;
    
    if (strncmp(rid, "6.8", EN61107_RID_L) == 0) {
        // energy
        tfp_snprintf(response->e1.unit, unit_length, "%s", unit_copy);
    }
    else if (strncmp(rid, "6.26", EN61107_RID_L) == 0) {
        // volume
        tfp_snprintf(response->v1.unit, unit_length, "%s", unit_copy);
    }
    else if (strncmp(rid, "6.31", EN61107_RID_L) == 0) {
        // hours
        tfp_snprintf(response->hr.unit, unit_length, "%s", unit_copy);
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
    char rid_value_unit_string[EN61107_REGISTER_L];
    char register_list_string[EN61107_FRAME_L];
    char *register_list_string_ptr;
    char *rid_value_unit_string_ptr;
    char *pos;
    size_t length;
    size_t rid_string_length;
    size_t value_string_length;
    size_t unit_string_length;
    size_t customer_no_string_length;
    
    char decimal_str[EN61107_VALUE_L + 1];	// 1 char more for .
    
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
                    memset(response->meter_type, 0, EN61107_METER_TYPE_L);	// clear meter_type_string (null terminalte)
                    pos = strstr(frame, stx);			   // find position of stx char
                    if (pos != NULL) {							  // if found stx char...
                        length = pos - frame;			   // ...save meter_type string
                        // DEBUG: check if length - 3 is >= 0
                        memcpy(response->meter_type, frame + 1, length - 3);	// DEBUG: check bounds
                        frame += length + 3;
                    }
                    
                    // parse to etx and save to register_list_string
                    memset(register_list_string, 0, EN61107_FRAME_L);
                    pos = strstr(frame, etx);			   // find position of etx
                    if (pos != NULL) {
                        length = pos - frame;
                        memcpy(register_list_string, frame, length);
                    }
                    
                    // parse values
                    j = 0;
                    register_list_string_ptr = register_list_string;		// force pointer arithmetics
                    while ((pos = strstr(register_list_string_ptr, separator)) != NULL) {
                        length = pos - register_list_string_ptr;
                        memset(rid_value_unit_string, 0, EN61107_REGISTER_L);	// zero (null terminate)
                        memcpy(rid_value_unit_string, register_list_string_ptr, length);
                        rid_value_unit_string_ptr = rid_value_unit_string;  // force pointer arithmetics
                        
                        // parse register number, value and unit unless rid == 0
                        pos = strstr(rid_value_unit_string, "(");
                        if (pos != NULL) {
                            rid_string_length = pos - rid_value_unit_string;
                            memset(rid, 0, EN61107_RID_L);					// zero (null terminate)
                            memcpy(rid, rid_value_unit_string, rid_string_length);
                            rid_value_unit_string_ptr += rid_string_length + 1;
                        }
                        pos = strstr(rid_value_unit_string_ptr, "*");
                        if (pos != NULL) {
                            value_string_length = pos - rid_value_unit_string_ptr;
                            cleanup_decimal_str(rid_value_unit_string_ptr, decimal_str, value_string_length);
                            en61107_response_set_value(response, rid, decimal_str, strlen(decimal_str));
                            rid_value_unit_string_ptr += value_string_length + 1;
                        }
                        pos = strstr(rid_value_unit_string_ptr, ")");
                        if (pos != NULL) {
                            if (strncmp(rid, "0", 1) == 0) {
                                // customer number, no unit
                                customer_no_string_length = pos - rid_value_unit_string_ptr;
                                memset(response->customer_no, 0, EN61107_CUSTOMER_NO_L);	// zero (null terminate)
                                memcpy(response->customer_no, rid_value_unit_string_ptr, customer_no_string_length);
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
                    memset(rid_value_unit_string, 0, EN61107_REGISTER_L);	// zero (null terminate)
                    memcpy(rid_value_unit_string, register_list_string_ptr, length);
                    rid_value_unit_string_ptr = rid_value_unit_string;  // force pointer arithmetics
                    
                    // parse register number, value and unit unless rid == 0
                    pos = strstr(rid_value_unit_string, "(");
                    if (pos != NULL) {
                        rid_string_length = pos - rid_value_unit_string;
                        memset(rid, 0, EN61107_RID_L);					// zero (null terminate)
                        memcpy(rid, rid_value_unit_string, rid_string_length);
                        rid_value_unit_string_ptr += rid_string_length + 1;
                    }
                    pos = strstr(rid_value_unit_string_ptr, "*");
                    if (pos != NULL) {
                        value_string_length = pos - rid_value_unit_string_ptr;
                        cleanup_decimal_str(rid_value_unit_string_ptr, decimal_str, value_string_length);
                        en61107_response_set_value(response, rid, decimal_str, strlen(decimal_str));
                        rid_value_unit_string_ptr += value_string_length + 1;
                    }
                    pos = strstr(rid_value_unit_string_ptr, ")");
                    if (pos != NULL) {
                        if (strncmp(rid, "0", 1) == 0) {
                            // customer number, no unit
                            customer_no_string_length = pos - rid_value_unit_string_ptr;
                            memset(response->customer_no, 0, EN61107_CUSTOMER_NO_L);	// zero (null terminate)
                            memcpy(response->customer_no, rid_value_unit_string_ptr, customer_no_string_length);
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

bool parse_mc66cde_inst_values_frame(en61107_response_t *response, char *frame, unsigned int frame_length) {
    char *p;
    int i = 0;
    char decimal_str[EN61107_VALUE_L + 1];	// 1 char more for .
    
    // for calculation of tdif
    int32_t t1 = 0;
    int32_t t2 = 0;
    char tdif[EN61107_VALUE_L];
    int32_t tdif_int;
    
    p = strtok(frame, " ");	// returns null terminated string
    while (p != NULL) {
        switch (i) {
            case 0:
                //strncpy(t1, p, EN61107_VALUE_L);	// for later calculation of tdif
                t1 = atoi(p);
                divide_str_by_100(p, decimal_str);
                tfp_snprintf(response->t1.value, EN61107_VALUE_L, "%s", decimal_str);
                tfp_snprintf(response->t1.unit, EN61107_UNIT_L, "%s", "C");
                break;
            case 1:
                divide_str_by_100(p, decimal_str);	// t3 resolution of 0.01°C.
                tfp_snprintf(response->t3.value, EN61107_VALUE_L, "%s", decimal_str);
                tfp_snprintf(response->t3.unit, EN61107_UNIT_L, "%s", "C");
                break;
            case 2:
                //strncpy(t2, p, EN61107_VALUE_L);	// for later calculation of tdif
                t2 = atoi(p);
                divide_str_by_100(p, decimal_str);
                tfp_snprintf(response->t2.value, EN61107_VALUE_L, "%s", decimal_str);
                tfp_snprintf(response->t2.unit, EN61107_UNIT_L, "%s", "C");
                break;
            case 5:
                cleanup_decimal_str(p, decimal_str, strlen(p));
                tfp_snprintf(response->flow1.value, EN61107_VALUE_L, "%s", decimal_str);
                tfp_snprintf(response->flow1.unit, EN61107_UNIT_L, "%s", "l/h");
                break;
            case 7:
                divide_str_by_10(p, decimal_str);
                tfp_snprintf(response->effect1.value, EN61107_VALUE_L, "%s", decimal_str);
                tfp_snprintf(response->effect1.unit, EN61107_UNIT_L, "%s", "kW");
                break;
        }
        i++;
        p = strtok(NULL, " ");
    };
    
    // calculate tdif
    tdif_int = t1 - t2;
    if (tdif_int < 0) {
        // dont send negative tdif
        tdif_int = 0;
    }
    
    tfp_snprintf(tdif, EN61107_VALUE_L, "%u", tdif_int);
    divide_str_by_100(tdif, decimal_str);
    strncpy(response->tdif.value, decimal_str, EN61107_VALUE_L);
    strncpy(response->tdif.unit, "C", EN61107_UNIT_L);
    
    return true;
}

int main(int argc, const char * argv[]) {
    if (parse_en61107_frame(&en61107_response, data, data_length)) {
        printf("bcc ok\n");
        
        printf("serial: %s\n", en61107_response.customer_no);
        printf("meter type: %s\n", en61107_response.meter_type);
        printf("e1: %s %s\n", en61107_response.e1.value, en61107_response.e1.unit);
        printf("v1: %s %s\n", en61107_response.v1.value, en61107_response.v1.unit);
        printf("hr: %s %s\n", en61107_response.hr.value, en61107_response.v1.unit);
    }
    else {
        printf("bcc error\n");
    }
    
    parse_mc66cde_inst_values_frame(&en61107_response, data2, data2_length);
    printf("flow1: %s %s\n", en61107_response.flow1.value, en61107_response.flow1.unit);
    return 0;
}

