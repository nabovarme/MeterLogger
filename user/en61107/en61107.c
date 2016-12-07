#include <stdint.h>
#include <string.h>

#include <esp8266.h>
#include "en61107.h"

ICACHE_FLASH_ATTR
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
					memset(response, 0, sizeof(en61107_response_t));
					memset(meter_type_string, 0, EN61107_REGISTER_L);	// clear meter_type_string (null terminalte)
					pos = strstr(frame, stx);			   // find position of stx char
					if (pos != NULL) {							  // if found stx char...
						length = pos - frame;			   // ...save meter_type string
						// DEBUG: check if length - 3 is >= 0
						memcpy(response->meter_type, frame + 1, length - 3);
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
					memset(rid_value_unit_string, 0, EN61107_REGISTER_L);
					memset(rid, 0, EN61107_RID_L);
					register_list_string_ptr = register_list_string;		// force pointer arithmetics
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
							cleanup_decimal_str(rid_value_unit_string_ptr, decimal_str);
							en61107_response_set_value(response, rid, decimal_str, value_string_length);
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
						cleanup_decimal_str(rid_value_unit_string_ptr, decimal_str);
						en61107_response_set_value(response, rid, decimal_str, value_string_length);
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

ICACHE_FLASH_ATTR
bool parse_mc66cde_inst_values_frame(en61107_response_t *response, char *frame) {
	char *p;
	int i = 0;
	char decimal_str[EN61107_VALUE_L + 1];	// 1 char more for .

	p = strtok(frame, " ");
	while (p != NULL) {
		switch (i) {
			case 0:
				divide_str_by_100(p, decimal_str);
				strncpy(response->t1.value, decimal_str, EN61107_VALUE_L);
				strncpy(response->t1.unit, "C", EN61107_UNIT_L);
				break;
			case 1:
				divide_str_by_100(p, decimal_str);
				strncpy(response->t3.value, decimal_str, EN61107_VALUE_L);
				strncpy(response->t3.unit, "C", EN61107_UNIT_L);
				break;
			case 2:
				divide_str_by_100(p, decimal_str);
				strncpy(response->t2.value, decimal_str, EN61107_VALUE_L);
				strncpy(response->t2.unit, "C", EN61107_UNIT_L);
				break;
			case 5:
				divide_str_by_100(p, decimal_str);
				strncpy(response->flow1.value, decimal_str, EN61107_VALUE_L);
				strncpy(response->flow1.unit, "l/h", EN61107_UNIT_L);
				break;
			case 7:
				divide_str_by_100(p, decimal_str);
				strncpy(response->effect1.value, decimal_str, EN61107_VALUE_L);
				strncpy(response->effect1.unit, "kW", EN61107_UNIT_L);
				break;
		}
		i++;
		p = strtok(NULL, " ");
	}
	return true;
}

ICACHE_FLASH_ATTR
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

ICACHE_FLASH_ATTR
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
