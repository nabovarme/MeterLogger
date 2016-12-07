#include <stdint.h>
#include <string.h>

#include <esp8266.h>
#include "en61107.h"

ICACHE_FLASH_ATTR
bool parse_en61107_frame(en61107_response_t *response, char *frame, unsigned int frame_length) {
	/*
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
    if (!frame_length) {
        return false;
    }
    
    bcc = frame[frame_length - 1];
    calculated_bcc = 0;
    for (i = 0; i < frame_length; i++) {
        //printf("#%d %c (0x%x)\n", i, frame[i], frame[i]);
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
                        memcpy(response->en61107_response_meter_type, frame + 1, length - 3);
                        //printf("meter type: %s\n", response->en61107_response_meter_type);
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
                        //printf(">%s<\n", rid_value_unit_string);
                        rid_value_unit_string_ptr = rid_value_unit_string;  // force pointer arithmetics

                        // parse register number, value and unit unless rid == 0
                        pos = strstr(rid_value_unit_string, "(");
                        if (pos != NULL) {
                            rid_string_length = pos - rid_value_unit_string;
                            memcpy(response->en61107_response_register_list[j].rid, rid_value_unit_string, rid_string_length);
                            rid_value_unit_string_ptr += rid_string_length + 1;
                        }
                        pos = strstr(rid_value_unit_string_ptr, "*");
                        if (pos != NULL) {
                            value_string_length = pos - rid_value_unit_string_ptr;
                            memcpy(response->en61107_response_register_list[j].value, rid_value_unit_string_ptr, value_string_length);
                            rid_value_unit_string_ptr += value_string_length + 1;
                        }
                        pos = strstr(rid_value_unit_string_ptr, ")");
                        if (pos != NULL) {
                            if (strncmp(response->en61107_response_register_list[j].rid, "0", 1) == 0) {
                                // serial number, no unit
                                serial_string_length = pos - rid_value_unit_string_ptr;
                                memcpy(response->en61107_response_serial, rid_value_unit_string_ptr, serial_string_length);
                                //printf("rid: %s serial: %s\n", response->en61107_response_register_list[j].rid, response->en61107_response_serial);
                            }
                            else {
                                unit_string_length = pos - rid_value_unit_string_ptr;
                                memcpy(response->en61107_response_register_list[j].unit, rid_value_unit_string_ptr, unit_string_length);
                                //printf("rid: %s value: %s unit: %s\n", response->en61107_response_register_list[j].rid, response->en61107_response_register_list[j].value, response->en61107_response_register_list[j].unit);
                                j++;

                            }
                        }

                        register_list_string_ptr += length + 2;
                    }

                    return true;
                }
            }
        }
    }
    return false;
	*/
}

ICACHE_FLASH_ATTR
bool parse_mc66cde_inst_values_frame(en61107_response_t *response, char *frame) {
	char *p;
	int i = 0;

	p = strtok(frame, " ");
	while (p != NULL) {
		switch (i) {
			case 0:
				strncpy(response->t1.value, p, EN61107_VALUE_L);
				strncpy(response->t1.unit, "C", EN61107_UNIT_L);
				break;
			case 1:
				strncpy(response->t3.value, p, EN61107_VALUE_L);
				strncpy(response->t3.unit, "C", EN61107_UNIT_L);
				break;
			case 2:
				strncpy(response->t2.value, p, EN61107_VALUE_L);
				strncpy(response->t2.unit, "C", EN61107_UNIT_L);
				break;
			case 5:
				strncpy(response->flow1.value, p, EN61107_VALUE_L);
				strncpy(response->flow1.unit, "l/h", EN61107_UNIT_L);
				break;
			case 7:
				strncpy(response->effect1.value, p, EN61107_VALUE_L);
				strncpy(response->effect1.unit, "kW", EN61107_UNIT_L);
				break;
		}
		i++;
		p = strtok(NULL, " ");
	}
	return true;
}

