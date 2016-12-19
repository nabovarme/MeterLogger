#include <stdint.h>
#include <string.h>

#include <esp8266.h>
#include "utils.h"
#include "led.h"
#include "tinyprintf.h"
#include "en61107.h"

ICACHE_FLASH_ATTR
bool parse_en61107_frame(en61107_response_t *response, char *frame, unsigned int frame_length) {
	unsigned int i, j;
	uint8_t bcc;
	uint8_t calculated_bcc;

#ifndef MC_66B
	const char *separator = "\x0D\x0A";
#else
	const char *separator = ")";
#endif
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
						// detect meter type and protocol from returned meter type
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
#ifndef MC_66B
						length = pos - register_list_string_ptr;
#else
						length = pos - register_list_string_ptr + 1;	// + 1 becouse we need the last ')' in further parsing
#endif
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
#ifndef MC_66B
						register_list_string_ptr += length + 2;
#else
						register_list_string_ptr += length;		// DEBUG: separator not two chars: 0x0D 0x0A
#endif
					}

#ifndef MC_66B
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
#endif

					return true;
				}
			}
		}
	}
	return false;
}

ICACHE_FLASH_ATTR
bool parse_mc66cde_standard_data_1_frame(en61107_response_t *response, char *frame, unsigned int frame_length) {
	char *p;
	int i = 0;
	char decimal_str[EN61107_VALUE_L + 1];	// 1 char more for .

	p = strtok(frame, " ");	// returns null terminated string
	while (p != NULL) {
		switch (i) {
			case 0:
				divide_str_by_10(p, decimal_str);
				tfp_snprintf(response->effect1.value, EN61107_VALUE_L, "%s", decimal_str);
				tfp_snprintf(response->effect1.unit, EN61107_UNIT_L, "%s", "kW");
				break;
			case 3:
				divide_str_by_100(p, decimal_str);
				tfp_snprintf(response->t1.value, EN61107_VALUE_L, "%s", decimal_str);
				tfp_snprintf(response->t1.unit, EN61107_UNIT_L, "%s", "C");
				break;
			case 4:
				divide_str_by_100(p, decimal_str);
				tfp_snprintf(response->t2.value, EN61107_VALUE_L, "%s", decimal_str);
				tfp_snprintf(response->t2.unit, EN61107_UNIT_L, "%s", "C");
				break;
			case 5:
				divide_str_by_100(p, decimal_str);
				tfp_snprintf(response->tdif.value, EN61107_VALUE_L, "%s", decimal_str);
				tfp_snprintf(response->tdif.unit, EN61107_UNIT_L, "%s", "C");
				break;
			case 7:
				cleanup_decimal_str(p, decimal_str, strlen(p));
				tfp_snprintf(response->flow1.value, EN61107_VALUE_L, "%s", decimal_str);
				tfp_snprintf(response->flow1.unit, EN61107_UNIT_L, "%s", "l/h");
				break;
		}
		i++;
		p = strtok(NULL, " ");
	};

	return true;
}

ICACHE_FLASH_ATTR
bool parse_mc66cde_standard_data_2_frame(en61107_response_t *response, char *frame, unsigned int frame_length) {
	char *p;
	int i = 0;
	char value[4];

	p = strtok(frame, " ");
	while (p != NULL) {
		switch (i) {
			case 7:	// ABCCC
				// A
				memset(value, 0, sizeof(value));
				memcpy(value, p + 2, 1);
				response->meter_program.a = atoi(value);

				// B
				memset(value, 0, sizeof(value));
				memcpy(value, p + 2 + 1, 1);
				response->meter_program.b = atoi(value);

				// CCC
				memset(value, 0, sizeof(value));
				memcpy(value, p + 2 + 2, 3);
				response->meter_program.ccc = atoi(value);
				break;
			case 8:	// DDEFFGG
				// DD
				memset(value, 0, sizeof(value));
				memcpy(value, p, 2);
				response->meter_program.dd = atoi(value);

				// E
				memset(value, 0, sizeof(value));
				memcpy(value, p + 2, 1);
				response->meter_program.e = atoi(value);

				// FF
				memset(value, 0, sizeof(value));
				memcpy(value, p + 3, 2);
				response->meter_program.ff = atoi(value);

				// GG
				memset(value, 0, sizeof(value));
				memcpy(value, p + 5, 2);
				response->meter_program.gg = atoi(value);
				break;
		}
		i++;
		p = strtok(NULL, " ");	// returns null terminated string
	};

	return true;
}

ICACHE_FLASH_ATTR
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

ICACHE_FLASH_ATTR
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

