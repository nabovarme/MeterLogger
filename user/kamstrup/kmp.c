//
//  KMP.m
//  MeterLogger
//
//  Created by stoffer on 28/05/14.
//  Copyright (c) 2014 Johannes Gaardsted Jørgensen <johannesgj@gmail.com> + Kristoffer Ek <stoffer@skulp.net>. All rights reserved.
//  This program is distributed under the terms of the GNU General Public License

/*
	self.registerIDTable = @{@1003: @"Current date (YYMMDD)",
 @60:   @"Heat energy",	 // @"Energy register 1: Heat energy"
 @94:   @"Energy register 2: Control energy",
 @63:   @"Energy register 3: Cooling energy",
 @61:   @"Energy register 4: Flow energy",
 @62:   @"Energy register 5: Return flow energy",
 @95:   @"Energy register 6: Tap water energy",
 @96:   @"Energy register 7: Heat energy Y",
 @97:   @"Energy register 8: [m3 • T1]",
 @110:  @"Energy register 9: [m3 • T2]",
 @64:   @"Tariff register 2",
 @65:   @"Tariff register 3",
 @68:   @"Volume register V1",
 @69:	@"Volume register V2",
 @84:	@"Input register VA",
 @85:	@"Input register VB",
 @72:	@"Mass register",   // @"Mass register V1"
 @73:	@"Mass register V2",
 @1004:	@"Operational hour counter",
 @113:	@"Info-event counter",
 @1002:	@"Current time (hhmmss)",
 @99:	@"Infocode register, current",
 @86:	@"Current flow temperature",
 @87:	@"Current return flow temperature",
 @88:	@"Current temperature T3",
 @122:	@"Current temperature T4",
 @89:	@"Current temperature difference",
 @91:	@"Pressure in flow",
 @92:	@"Pressure in return flow",
 @74:	@"Current flow in flow",
 @75:	@"Current flow in return flow",
 @80:	@"Current power calculated on the basis of V1-T1-T2",
 @123:	@"Date for max. this year",
 @124:	@"Max. value this year",
 @125:	@"Date for min. this year",
 @126:	@"Min. value this year",
 @127:	@"Date for max. this year",
 @128:	@"Max. value this year",
 @129:	@"Date for min. this year",
 @130:	@"Min. value this year",
 @138:	@"Date for max. this year",
 @139:	@"Max. value this year",
 @140:	@"Date for min. this month",
 @141:	@"Min. value this month",
 @142:	@"Date for max. this month",
 @143:	@"Max. value this month",
 @144:	@"Date for min. this month",
 @145:	@"Min. value this month",
 @146:	@"Year-to-date average for T1",
 @147:	@"Year-to-date average for T2",
 @149:	@"Month-to-date average for T1",
 @150:	@"Month-to-date average for T2",
 @66:	@"Tariff limit 2",
 @67:	@"Tariff limit 3",
 @98:	@"Target date (reading date)",
 @152:	@"Program no. ABCCCCCC",
 @153:	@"Config no. DDDEE",
 @168:	@"Config no. FFGGMN",
 @1001:	@"Serial no.",		  // @"Serial no. (unique number for each meter)"
 @112:	@"Customer number (8 most important digits)",
 @1010:	@"Customer number (8 less important digits)",
 @114:	@"Meter no. for VA",
 @104:	@"Meter no. for VB",
 @1005:	@"Software edition",
 @154:	@"Software check sum",
 @155:	@"High-resolution energy register for testing purposes",
 @157:	@"ID number for top module ( only mc 601 )",
 @158:	@"ID number for base module",
 @175:	@"Error hour counter",
 @234:  @"Liter/imp value for input A",
 @235:	@"Liter/imp value for input B"};
 */

#include <esp8266.h>
#include <utils.h>

#include "tinyprintf.h"
#include "kmp.h"

#define KMP_START_BYTE_IDX  0
#define KMP_DST_IDX         1
#define KMP_CID_IDX         2
#define KMP_DATA_IDX        3

unsigned char *kmp_frame;
unsigned int kmp_frame_length;
unsigned int kmp_data_length;

// kmp response struct
kmp_response_t *kmp_response;


ICACHE_FLASH_ATTR
unsigned int kmp_get_type(unsigned char *frame) {
    uint16_t crc16;
    
    // clear frame
    kmp_frame = frame;
    memset(kmp_frame, 0x00, KMP_FRAME_L);
    kmp_frame_length = 0;
    kmp_data_length = 0;

    // start byte
    kmp_frame[KMP_START_BYTE_IDX] = 0x80;
    
    // data
    kmp_frame[KMP_DST_IDX] = 0x3f;
    kmp_frame[KMP_CID_IDX] = 0x01;
    kmp_frame_length = 3;
    kmp_data_length = 2;
    
    // put crc 16 in frame
    crc16 = kmp_crc16();
    kmp_frame[kmp_frame_length] = crc16 >> 8;       // high bits of crc16
    kmp_frame[kmp_frame_length + 1] = crc16 & 0xff; // low bits
    kmp_frame_length += 2;
    kmp_data_length +=2;
    
    // stuff data
    kmp_byte_stuff();
    
    // stop byte
    kmp_frame[kmp_frame_length] = 0x0d;
    kmp_frame_length++;
   
    return kmp_frame_length;
}

ICACHE_FLASH_ATTR
unsigned int kmp_get_serial(unsigned char *frame) {
    uint16_t crc16;
    
    // clear frame
    kmp_frame = frame;
    memset(kmp_frame, 0x00, KMP_FRAME_L);
    kmp_frame_length = 0;
    kmp_data_length = 0;
    
    // start byte
    kmp_frame[KMP_START_BYTE_IDX] = 0x80;
    
    // data
    kmp_frame[KMP_DST_IDX] = 0x3f;
    kmp_frame[KMP_CID_IDX] = 0x02;
    kmp_frame_length = 3;
    kmp_data_length = 2;
    
    // put crc 16 in frame
    crc16 = kmp_crc16();
    kmp_frame[kmp_frame_length] = crc16 >> 8;       // high bits of crc16
    kmp_frame[kmp_frame_length + 1] = crc16 & 0xff; // low bits
    kmp_frame_length += 2;
    kmp_data_length += 2;
    
    // stuff data
    kmp_byte_stuff();
    
    // stop byte
    kmp_frame[kmp_frame_length] = 0x0d;
    kmp_frame_length++;
    
    return kmp_frame_length;
}

ICACHE_FLASH_ATTR
unsigned int kmp_set_clock(unsigned char *frame, uint64_t unix_time) {
    // DEBUG: not implemented
    return 0;
}

ICACHE_FLASH_ATTR
unsigned int kmp_get_register(unsigned char *frame, uint16_t *register_list, uint16_t register_list_length) {
    unsigned int i;
    uint8_t register_high;
    uint8_t register_low;
    uint16_t crc16;

    if (register_list_length > 8) {
        // maximal number of 8 registers can be read with one request, last ones ommitted
        register_list_length = 8;
    }

    // clear frame
    kmp_frame = frame;
    memset(kmp_frame, 0x00, KMP_FRAME_L);
    kmp_frame_length = 0;
    kmp_data_length = 0;
    
    // start byte
    kmp_frame[KMP_START_BYTE_IDX] = 0x80;

    // data
    kmp_frame[KMP_DST_IDX] = 0x3f;
    kmp_frame[KMP_CID_IDX] = 0x10;

    // number of registers
    kmp_frame[KMP_DATA_IDX] = register_list_length;
    kmp_frame_length = 4;
    kmp_data_length = 3;
    
    // registers
    for (i = 0; i < register_list_length; i++) {
        register_high = (uint8_t)(register_list[i] >> 8);
        register_low = (uint8_t)(register_list[i] & 0xff);
        kmp_frame[KMP_DATA_IDX + 2 * i + 1] = register_high;
        kmp_frame[KMP_DATA_IDX + 2 * i + 2] = register_low;
    }
    kmp_frame_length += 2 * i;
    kmp_data_length += 2 * i;

    // put crc 16 in frame
    crc16 = kmp_crc16();
    kmp_frame[kmp_frame_length] = crc16 >> 8;       // high bits of crc16
    kmp_frame[kmp_frame_length + 1] = crc16 & 0xff; // low bits
    kmp_frame_length += 2;

    // stuff data
    kmp_byte_stuff();

    // stop byte
    kmp_frame[kmp_frame_length] = 0x0d;
    kmp_frame_length++;

    return kmp_frame_length;
}

#pragma mark - KMP Decoder

ICACHE_FLASH_ATTR
int kmp_decode_frame(unsigned char *frame, unsigned char frame_length, kmp_response_t *response) {
    uint16_t kmp_frame_crc16;
    uint16_t crc16;
    unsigned int i;
    unsigned int kmp_register_idx;

    kmp_frame = frame;
    kmp_frame_length = frame_length;
    
    kmp_response = response;
    memset(kmp_response, 0x00, sizeof(kmp_response_t));
    
    if (kmp_frame_length == 0) {
        return 0;
    }
    
    if (kmp_frame_length == 1) {
        // no data returned from Kamstrup meter
        if (kmp_frame[kmp_frame_length - 1] == 0x06) {
            return 0;
        }
        else {
            // Kamstrup: device said: no valid reply from kamstrup meter
            return 0;
        }
    }
	
	if (kmp_frame_length < 6) {
		// valid packets are at least 1) start byte, 2) dst, 3) cid, 4) crc high, 5) crc low and 6) stop byte
		return 0;
	}
    
    if (kmp_frame[kmp_frame_length - 1] == 0x0d) {
        // end of data - get params from frame
        // no need to set start and stop bytes
        
        // unstuff data
        kmp_byte_unstuff();
		
        // calculate crc
        kmp_data_length = kmp_frame_length - 4; // not included 1) start_byte, 2) crc high, 3) crc low and 4) stop byte
        crc16 = kmp_crc16();
        
        // get crc from frame
        kmp_frame_crc16 = (kmp_frame[kmp_frame_length - 2] | kmp_frame[kmp_frame_length - 3] << 8);
        if (kmp_frame_crc16 == crc16) {
            //crc ok
        }
        else {
            //crc error
            return -1;
        }

        // decode application layer
		
        if (kmp_frame[KMP_CID_IDX] == 0x01) {
            // kmp_get_type
            kmp_response->kmp_response_meter_type = (kmp_frame[KMP_DATA_IDX + 0] << 8) + kmp_frame[KMP_DATA_IDX + 1];
            kmp_response->kmp_response_sw_revision = (kmp_frame[KMP_DATA_IDX + 2] << 8) + kmp_frame[KMP_DATA_IDX + 3];
			return 1;
        }
        else if (kmp_frame[KMP_CID_IDX] == 0x02) {
            // kmp_get_serial
            kmp_response->kmp_response_serial = (kmp_frame[KMP_DATA_IDX + 0] << 24) + (kmp_frame[KMP_DATA_IDX + 1] << 16) + (kmp_frame[KMP_DATA_IDX + 2] << 8) + kmp_frame[KMP_DATA_IDX + 3];
			return 1;
        }
        else if (kmp_frame[KMP_CID_IDX] == 0x10) {
            // kmp_get_register
            if (kmp_data_length > 2) {
                for (i = 0; i < ((kmp_data_length - 2) / 9); i++) {	 // 9 bytes per register. BUG here if length != 4?
                    kmp_register_idx = 9 * i + KMP_DATA_IDX;
                    
                    // rid
                    kmp_response->kmp_response_register_list[i].rid = (kmp_frame[kmp_register_idx + 0] << 8) + kmp_frame[kmp_register_idx + 1];
                    
                    // unit
                    kmp_response->kmp_response_register_list[i].unit = kmp_frame[kmp_register_idx + 2];
                    
                    // length
                    kmp_response->kmp_response_register_list[i].length = kmp_frame[kmp_register_idx + 3];
                    
                    // si_ex
                    kmp_response->kmp_response_register_list[i].si_ex = kmp_frame[kmp_register_idx + 4];
                    
                    // value
                    kmp_response->kmp_response_register_list[i].value = (kmp_frame[kmp_register_idx + 5] << 24) + (kmp_frame[kmp_register_idx + 6] << 16) + (kmp_frame[kmp_register_idx + 7] << 8) + kmp_frame[kmp_register_idx + 8];
                }
                return 1;
            }
            else {
                // No registers in reply
				return 0;
            }
        }
        else if (kmp_frame[KMP_CID_IDX] == 0x11) {
            // kmp_put_register
            //range = NSMakeRange(2, data.length - 2);
            //NSLog(@"%@", [data subdataWithRange:range]);
			return 1;
        }
//
    }
    else if (kmp_frame[kmp_frame_length - 1] == 0x06) {
        // kmp_set_clock no CRC
        return 0;
    }
    
    return 0;
}

#pragma mark - Helper methods

ICACHE_FLASH_ATTR
uint16_t kmp_crc16() {
	//uint16_t crc16;
	//int i;

	//crc16 = 0;
	//for (i = KMP_DST_IDX; i < (kmp_data_length + KMP_DST_IDX); i++) {
	//    crc16 = (crc16 << 8) ^ kmp_crc16_table[((crc16 >> 8) ^ kmp_frame[i]) & 0x00FF];
	//}
	//return crc16;

	return ccit_crc16(kmp_frame + KMP_DST_IDX, kmp_data_length);
}

ICACHE_FLASH_ATTR
double kmp_value_to_double(int32_t value, uint8_t si_ex) {
    double result;
    int8_t sign_i = (si_ex & 0x80) >> 7;
    int8_t sign_e = (si_ex & 0x40) >> 6;
    int8_t exponent = (si_ex & 0x3f);
    
    // powf(-1, (double)sign_i) * value * powf(10, (powf(-1, (double)sign_e) * exponent));
    if (sign_i) {
        if (sign_e) {
            result = -1 * value / int_pow(10, exponent);
        }
        else {
            result = -1 * value * int_pow(10, exponent);
        }
    }
    else {
        if (sign_e) {
            result = value / (double)int_pow(10, exponent);
        }
        else {
            result = value * (double)int_pow(10, exponent);
        }
    }
    
    return result;
}

ICACHE_FLASH_ATTR
void kmp_value_to_string(int32_t value, uint8_t si_ex, unsigned char *value_string) {
    double result;
    uint32_t result_int, result_frac;
    int8_t sign_i = (si_ex & 0x80) >> 7;
    int8_t sign_e = (si_ex & 0x40) >> 6;
    int8_t exponent = (si_ex & 0x3f);
	uint32_t factor;
	unsigned char leading_zeroes[16];
	unsigned int i;
	
	factor = int_pow(10, exponent);
    if (sign_i) {
        if (sign_e) {
            result = value / int_pow(10, exponent);
            result_int = (int32_t)result;
			result_frac = value - result_int * int_pow(10, exponent);
            
			// prepare decimal string
			strcpy(leading_zeroes, "");
			for (i = 0; i < (exponent - decimal_number_length(result_frac)); i++) {
				strcat(leading_zeroes, "0");
			}
			tfp_snprintf(value_string, 23 + i, "-%u.%s%u", result_int, leading_zeroes, result_frac);
        }
        else {
            tfp_snprintf(value_string, 12, "-%u", value * factor);
        }
    }
    else {
        if (sign_e) {
            result = value / int_pow(10, exponent);
            result_int = (int32_t)result;
			result_frac = value - result_int * int_pow(10, exponent);
            
			// prepare decimal string
			strcpy(leading_zeroes, "");
			for (i = 0; i < (exponent - decimal_number_length(result_frac)); i++) {
				strcat(leading_zeroes, "0");
			}
			tfp_snprintf(value_string, 22 + i, "%u.%s%u", result_int, leading_zeroes, result_frac);
        }
        else {
            tfp_snprintf(value_string, 11, "%u", value * factor);
        }
    }
}

ICACHE_FLASH_ATTR
void kmp_unit_to_string(uint8_t unit, unsigned char *unit_string) {
    switch (unit) {
        case 0x01:
            strcpy(unit_string, "Wh");
            break;
        case 0x02:
            strcpy(unit_string, "kWh");
            break;
        case 0x03:
            strcpy(unit_string, "MWh");
            break;
        case 0x08:
            strcpy(unit_string, "Gj");
            break;
        case 0x0c:
            strcpy(unit_string, "Gcal");
            break;
        case 0x16:
            strcpy(unit_string, "kW");
            break;
        case 0x17:
            strcpy(unit_string, "MW");
            break;
        case 0x25:
            strcpy(unit_string, "C");
            break;
        case 0x26:
            strcpy(unit_string, "K");
            break;
        case 0x27:
            strcpy(unit_string, "l");
            break;
        case 0x28:
            strcpy(unit_string, "m3");
            break;
        case 0x29:
            strcpy(unit_string, "l/h");
            break;
        case 0x2a:
            strcpy(unit_string, "m3/h");
            break;
        case 0x2b:
            strcpy(unit_string, "m3xC");
            break;
        case 0x2c:
            strcpy(unit_string, "ton");
            break;
        case 0x2d:
            strcpy(unit_string, "ton/h");
            break;
        case 0x2e:
            strcpy(unit_string, "h");
            break;
        case 0x2f:
            strcpy(unit_string, "clock");
            break;
        case 0x30:
            strcpy(unit_string, "date1");
            break;
        case 0x32:
            strcpy(unit_string, "date3");
            break;
        case 0x33:
            strcpy(unit_string, "number");
            break;
        case 0x34:
            strcpy(unit_string, "bar");
            break;
            
        default:
            break;
    }
}

/*
-(NSData *)kmpDateWithDate:(NSDate *)theDate {
	NSCalendar* calendar = [NSCalendar currentCalendar];
	NSDateComponents* components = [calendar components:NSYearCalendarUnit|NSMonthCalendarUnit|NSDayCalendarUnit fromDate:theDate];
	
	unsigned int year = (int)(components.year - 2000);
	unsigned int month = (int)(components.month);
	unsigned int day = (int)(components.day);
	
	NSString *dateString = [NSString stringWithFormat:@"%02d%02d%02d", year, month, day];
	NSString *hexDate = [NSString stringWithFormat:@"%08x", dateString.intValue];
	NSLog(@"%@", hexDate);

	NSMutableData *result = [[NSMutableData alloc] init];
	unsigned int i;
	for (i = 0; i < 4; i++) {
		NSRange range = NSMakeRange(2 * i, 2);
		NSString* hexValue = [hexDate substringWithRange:range];
		NSScanner* scanner = [NSScanner scannerWithString:hexValue];
		unsigned int intValue;
		[scanner scanHexInt:&intValue];
		unsigned char uc = (unsigned char) intValue;
		[result appendBytes:&uc length:1];
	}
	return result;
}

-(NSData *)kmpTimeWithDate:(NSDate *)theDate {
	NSCalendar* calendar = [NSCalendar currentCalendar];
	NSDateComponents* components = [calendar components:NSHourCalendarUnit|NSMinuteCalendarUnit|NSSecondCalendarUnit fromDate:theDate];
	
	unsigned int hour = (int)(components.hour);
	unsigned int minute = (int)(components.minute);
	unsigned int second = (int)(components.second);
	
	NSString *dateString = [NSString stringWithFormat:@"%02d%02d%02d", hour, minute, second];
	NSString *hexDate = [NSString stringWithFormat:@"%08x", dateString.intValue];
	NSLog(@"%@", hexDate);
	
	NSMutableData *result = [[NSMutableData alloc] init];
	unsigned int i;
	for (i = 0; i < 4; i++) {
		NSRange range = NSMakeRange(2 * i, 2);
		NSString* hexValue = [hexDate substringWithRange:range];
		NSScanner* scanner = [NSScanner scannerWithString:hexValue];
		unsigned int intValue;
		[scanner scanHexInt:&intValue];
		unsigned char uc = (unsigned char) intValue;
		[result appendBytes:&uc length:1];
	}
	return result;
}

-(NSDate *)dateWithKmpDate:(NSData *)theData {
	return [NSDate date];
}

-(NSDate *)dateWithKmpTime:(NSData *)theData {
	return [NSDate date];
}

*/

ICACHE_FLASH_ATTR
void kmp_byte_stuff() {
    unsigned char stuffed_data[KMP_FRAME_L];
    unsigned int i;
    unsigned int j = 0;

    memset(stuffed_data, 0x00, KMP_FRAME_L);

    for (i = KMP_DST_IDX; i < (kmp_frame_length); i++) {
		if ((kmp_frame[i] == 0x80) || (kmp_frame[i] == 0x40) || (kmp_frame[i] == 0x0d) || (kmp_frame[i] == 0x06) || (kmp_frame[i] == 0x1b)) {
            stuffed_data[j++] = 0x1b;
            stuffed_data[j++] = kmp_frame[i] ^ 0xff;
		}
		else {
            stuffed_data[j++] = kmp_frame[i];
		}
	}
    memcpy(kmp_frame + KMP_DST_IDX, stuffed_data, j);
    kmp_frame_length = j + KMP_DST_IDX;
    kmp_data_length = j;
}

ICACHE_FLASH_ATTR
void kmp_byte_unstuff() {
    unsigned char unstuffed_data[KMP_FRAME_L];
    unsigned int i;
    unsigned int j = 0;
    
    for (i = KMP_DST_IDX; i < kmp_frame_length; i++) {
        if (kmp_frame[i] == 0x1b) {		  // byte unstuffing special char
            unstuffed_data[j++] = kmp_frame[i + 1] ^ 0xff;
            i++;
         }
        else {
            unstuffed_data[j++] = kmp_frame[i];
        }
    }
    memcpy(kmp_frame + KMP_DST_IDX, unstuffed_data, j);
    kmp_frame_length = j + KMP_DST_IDX;
    kmp_data_length = j;
}
