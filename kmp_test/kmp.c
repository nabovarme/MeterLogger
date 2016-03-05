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

#include <string.h>
#include <math.h>
#include "kmp.h"

#define KMP_START_BYTE_IDX  0
#define KMP_DST_IDX         1
#define KMP_CID_IDX         2
#define KMP_DATA_IDX        3

#define KMP_CRC16_TABLE_L	256

unsigned char *kmp_frame;
unsigned int kmp_frame_length;
unsigned int kmp_data_length;

// kmp response struct
kmp_response_t *kmp_response;


// crc table
uint16_t kmp_crc16_table[KMP_CRC16_TABLE_L] = {
	0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
	0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef,
	0x1231, 0x0210, 0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6,
	0x9339, 0x8318, 0xb37b, 0xa35a, 0xd3bd, 0xc39c, 0xf3ff, 0xe3de,
	0x2462, 0x3443, 0x0420, 0x1401, 0x64e6, 0x74c7, 0x44a4, 0x5485,
	0xa56a, 0xb54b, 0x8528, 0x9509, 0xe5ee, 0xf5cf, 0xc5ac, 0xd58d,
	0x3653, 0x2672, 0x1611, 0x0630, 0x76d7, 0x66f6, 0x5695, 0x46b4,
	0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d, 0xc7bc,
	0x48c4, 0x58e5, 0x6886, 0x78a7, 0x0840, 0x1861, 0x2802, 0x3823,
	0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b,
	0x5af5, 0x4ad4, 0x7ab7, 0x6a96, 0x1a71, 0x0a50, 0x3a33, 0x2a12,
	0xdbfd, 0xcbdc, 0xfbbf, 0xeb9e, 0x9b79, 0x8b58, 0xbb3b, 0xab1a,
	0x6ca6, 0x7c87, 0x4ce4, 0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41,
	0xedae, 0xfd8f, 0xcdec, 0xddcd, 0xad2a, 0xbd0b, 0x8d68, 0x9d49,
	0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70,
	0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a, 0x9f59, 0x8f78,
	0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e, 0xe16f,
	0x1080, 0x00a1, 0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067,
	0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c, 0xe37f, 0xf35e,
	0x02b1, 0x1290, 0x22f3, 0x32d2, 0x4235, 0x5214, 0x6277, 0x7256,
	0xb5ea, 0xa5cb, 0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d,
	0x34e2, 0x24c3, 0x14a0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
	0xa7db, 0xb7fa, 0x8799, 0x97b8, 0xe75f, 0xf77e, 0xc71d, 0xd73c,
	0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657, 0x7676, 0x4615, 0x5634,
	0xd94c, 0xc96d, 0xf90e, 0xe92f, 0x99c8, 0x89e9, 0xb98a, 0xa9ab,
	0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1, 0x3882, 0x28a3,
	0xcb7d, 0xdb5c, 0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a,
	0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1, 0x1ad0, 0x2ab3, 0x3a92, 
	0xfd2e, 0xed0f, 0xdd6c, 0xcd4d, 0xbdaa, 0xad8b, 0x9de8, 0x8dc9, 
	0x7c26, 0x6c07, 0x5c64, 0x4c45, 0x3ca2, 0x2c83, 0x1ce0, 0x0cc1, 
	0xef1f, 0xff3e, 0xcf5d, 0xdf7c, 0xaf9b, 0xbfba, 0x8fd9, 0x9ff8, 
	0x6e17, 0x7e36, 0x4e55, 0x5e74, 0x2e93, 0x3eb2, 0x0ed1, 0x1ef0
};


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

unsigned int kmp_set_clock(unsigned char *frame, uint64_t unix_time) {
    // DEBUG: not implemented
    return 0;
}

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
            return 1;
        }
        else {
            // Kamstrup: device said: no valid reply from kamstrup meter
            return 0;
        }
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
        }
        else if (kmp_frame[KMP_CID_IDX] == 0x02) {
            // kmp_get_serial
            kmp_response->kmp_response_serial = (kmp_frame[KMP_DATA_IDX + 0] << 24) + (kmp_frame[KMP_DATA_IDX + 1] << 16) + (kmp_frame[KMP_DATA_IDX + 2] << 8) + kmp_frame[KMP_DATA_IDX + 3];
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
            }
        }
        else if (kmp_frame[KMP_CID_IDX] == 0x11) {
            // kmp_put_register
            //range = NSMakeRange(2, data.length - 2);
            //NSLog(@"%@", [data subdataWithRange:range]);
        }
//
    }
    else if (kmp_frame[kmp_frame_length - 1] == 0x06) {
        // kmp_set_clock no CRC
        return 1;
    }
    
    return 0;
}

#pragma mark - Helper methods

uint16_t kmp_crc16() {
    uint16_t crc16;
	int i;
		
	crc16 = 0;
	for (i = KMP_DST_IDX; i < (kmp_data_length + KMP_DST_IDX); i++) {
        crc16 = (crc16 << 8) ^ kmp_crc16_table[((crc16 >> 8) ^ kmp_frame[i]) & 0x00FF];
    }
    return crc16;
}

int kmp_pow(int a, int b) {
    int i;
    int result = a;
    for (i = 1; i < b; i++) {
        result *= a;
    }
    return result;
}

double kmp_value_to_double(int32_t value, uint8_t si_ex) {
    double res;
    int8_t sign_i = (si_ex & 0x80) >> 7;
    int8_t sign_e = (si_ex & 0x40) >> 6;
    int8_t exponent = (si_ex & 0x3f);
    
    // powf(-1, (double)sign_i) * value * powf(10, (powf(-1, (double)sign_e) * exponent));
    if (sign_i) {
        if (sign_e) {
            res = -1 * value / kmp_pow(10, exponent);
        }
        else {
            res = -1 * value * kmp_pow(10, exponent);
        }
    }
    else {
        if (sign_e) {
            res = value / (double)kmp_pow(10, exponent);
        }
        else {
            res = value * (double)kmp_pow(10, exponent);
        }
    }
    
    return res;
}

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

