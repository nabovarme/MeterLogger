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
	
	self.registerUnitsTable = @{@0x01: @"Wh",
 @0x02: @"kWh",
 @0x03: @"MWh",
 @0x08: @"Gj",
 @0x0c: @"Gcal",
 @0x16: @"kW",
 @0x17: @"MW",
 @0x25: @"C",
 @0x26: @"K",
 @0x27: @"l",
 @0x28: @"m3",
 @0x29: @"l/h",
 @0x2a: @"m3/h",
 @0x2b: @"m3xC",
 @0x2c: @"ton",
 @0x2d: @"ton/h",
 @0x2e: @"h",
 @0x2f: @"clock",
 @0x30: @"date1",
 @0x32: @"date3",
 @0x33: @"number",
 @0x34: @"bar"};

 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "kmp.h"

#define KMP_CRC16_TABLE_L	256

bool kmp_frame_received;
bool kmp_error_receiving;

typedef struct kmp_frame_t {
    uint8_t start_byte;
    uint8_t dst;
    uint8_t cid;
    unsigned char data[KMP_DATA_L];
    uint16_t data_length;
    uint16_t crc16;
    uint8_t stop_byte;
} kmp_frame_t;

kmp_frame_t kmp_frame;


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

void kmp_init() {
	memset(kmp_frame.data, 0x00, KMP_DATA_L);
	kmp_frame.data_length = 0;
	kmp_frame_received = false;
	kmp_error_receiving = false;
}

void kmp_get_type() {
    // start byte
    kmp_frame.start_byte = 0x80;
    
    // data
    kmp_frame.dst = 0x3f;
    kmp_frame.cid = 0x01;
    kmp_frame.data_length = 0;
    
    // put crc 16 in frame
    kmp_crc16();
    
    // stuff data
    kmp_byte_stuff();
    
    // stop byte
    kmp_frame.stop_byte = 0x0d;
    
    // serialize it
    // TO-DO
}

void kmp_get_serial_no() {
	// start byte
	kmp_frame.start_byte = 0x80;
	
	// data
    kmp_frame.dst = 0x3f;
    kmp_frame.cid = 0x02;
    kmp_frame.data_length = 0;
	
	// put crc 16 in frame
	kmp_crc16();
	
	// stuff data
    kmp_byte_stuff();
	
	// stop byte
	kmp_frame.stop_byte = 0x0d;
    
    // serialize it
    // TO-DO
}

/*

void setClock:(NSDate *)theDate {
	// start byte
	self.frame = [[NSMutableData alloc] initWithBytes:(unsigned char[]){0x80} length:1];
	
	// data
	NSMutableData *data = [[NSMutableData alloc] initWithBytes:(unsigned char[]){0x3f, 0x09} length:2];
	
	NSMutableData *kmpDateTime = [[NSMutableData alloc] init];
	[kmpDateTime appendData:[self kmpDateWithDate:theDate]];
	[kmpDateTime appendData:[self kmpTimeWithDate:theDate]];
	[data appendData:kmpDateTime];
	NSLog(@"%@", data);
	
	// append crc 16 to data
	[data appendData:[self crc16ForData:data]];
	
	// stuff data
	data = [[self kmpByteStuff:data] mutableCopy];
	
	// create frame
	[self.frame appendData:data];
	[self.frame appendData:[[NSMutableData alloc] initWithBytes:(unsigned char[]){0x0d} length:1]];
	NSLog(@"%@", self.frame);
}

-(void)prepareFrameWithRegistersFromArray:(NSArray *)theRegisterArray {
	if (theRegisterArray.count > 8) {
		// maximal number of 8 registers can be read with one request
		NSRange range = NSMakeRange(0, 7);
		theRegisterArray = [[theRegisterArray subarrayWithRange:range] mutableCopy];
		NSLog(@"prepareFrameWithRegisters: number of registers was > 8, last ones ommitted");
	}
	
	// start byte
	self.frame = [[NSMutableData alloc] initWithBytes:(unsigned char[]){0x80} length:1];
	
	// data
	NSMutableData *data = [[NSMutableData alloc] initWithBytes:(unsigned char[]){0x3f, 0x10} length:2];
	[data appendBytes:(unsigned char[]){theRegisterArray.count} length:1];  // number of registers
	
	unsigned char registerHigh;
	unsigned char registerLow;
	for (NSNumber *reg in theRegisterArray) {
		registerHigh = (unsigned char)(reg.intValue >> 8);
		registerLow = (unsigned char)(reg.intValue & 0xff);
		[data appendData:[NSData dataWithBytes:(unsigned char[]){registerHigh, registerLow} length:2]];
	}
	// append crc 16 to data
	[data appendData:[self crc16ForData:data]];
	
	// stuff data
	data = [[self kmpByteStuff:data] mutableCopy];
	
	// create frame
	[self.frame appendData:data];
	[self.frame appendData:[[NSMutableData alloc] initWithBytes:(unsigned char[]){0x0d} length:1]];
	NSLog(@"frame: %@", self.frame);

}



#pragma mark - KMP Decoder

-(void)decodeFrame:(NSData *)theFrame {
	self.frameReceived = NO;
	self.errorReceiving = NO;
	[self.frame appendData:theFrame];
	const unsigned char *bytes = theFrame.bytes;

	if (1 == theFrame.length) {
		// no data returned from Kamstrup meter
		if  (bytes[theFrame.length - 1] == 0x06) {
			self.frameReceived = YES;
			NSLog(@"hjfgj");
		}
		else {
			NSLog(@"Kamstrup: device said: no valid reply from kamstrup meter");
			self.errorReceiving = YES;
		}
		return;
	}

	if (bytes[theFrame.length - 1] == 0x0d) {
		// end of data - get params from frame
		bytes = self.frame.bytes;
		
		[self.responseData setObject:[NSData dataWithBytes:bytes length:1] forKey:@"starByte"];
		[self.responseData setObject:[NSData dataWithBytes:(bytes + self.frame.length - 1) length:1] forKey:@"stopByte"];

		// unstuff data
		NSRange range = NSMakeRange(1, self.frame.length - 2);
		NSData *unstuffedFrame = [self kmpByteUnstuff:[self.frame subdataWithRange:range]];
		bytes = unstuffedFrame.bytes;

		if (unstuffedFrame.length >= 4) {
			[self.responseData setObject:[NSData dataWithBytes:bytes length:1] forKey:@"dst"];
			[self.responseData setObject:[NSData dataWithBytes:(bytes + 1) length:1] forKey:@"cid"];
			range = NSMakeRange(unstuffedFrame.length - 2, 2);
			[self.responseData setObject:[unstuffedFrame subdataWithRange:range] forKey:@"crc"];
		}

		// calculate crc
		range = NSMakeRange(0, unstuffedFrame.length - 2);
		NSData *data = [unstuffedFrame subdataWithRange:range];

		if ([[self crc16ForData:data] isEqualToData:responseData[@"crc"]]) {
			NSLog(@"crc ok");
		}
		else {
			NSLog(@"crc error");
			self.errorReceiving = YES;
			return;
		}

		// decode application layer
		unsigned char *cid_ptr = (unsigned char *)[self.responseData[@"cid"] bytes];
		unsigned char cid = cid_ptr[0];
		if (cid == 0x01) {		 // GetType
			NSLog(@"GetType");
			
			range = NSMakeRange(2, 2);
			[self.responseData setObject:[data subdataWithRange:range] forKey:@"meterType"];
			
			range = NSMakeRange(4, 2);
			[self.responseData setObject:[data subdataWithRange:range] forKey:@"swRevision"];
		}
		else if (cid == 0x02) {
			NSLog(@"GetSerialNo"); // GetSerialNo
			range = NSMakeRange(2, data.length - 2);
			bytes = (unsigned char *)[[data subdataWithRange:range] bytes];
			unsigned int serialNo = (bytes[0] << 24) + (bytes[1] << 16) + (bytes[2] << 8) + bytes[3];
			[self.responseData setObject:[NSNumber numberWithUnsignedInt:serialNo] forKey:@"serialNo"] ;
			NSLog(@"%d", serialNo);
		}
		else if (cid == 0x10) {	// GetRegister
			NSLog(@"GetRegister");
			if (data.length > 2) {
				for (uint8_t i = 0; i < ((data.length - 2) / 9); i++) {	 // 9 bytes per register
					
					range = NSMakeRange(9 * i + 2, 2);
					bytes = (unsigned char *)[[data subdataWithRange:range] bytes];
					int16_t rid = (bytes[0] << 8) + bytes[1];
					//[self.responseData setObject:[NSNumber numberWithUnsignedInt:rid] forKey:@"rid"];

					[self.responseData setObject:[[NSMutableDictionary alloc] init] forKey:[NSNumber numberWithUnsignedInt:rid]];

					
					range = NSMakeRange(9 * i + 4, 1);
					bytes = (unsigned char *)[[data subdataWithRange:range] bytes];
					unsigned int unit = bytes[0];
					//[self.responseData setObject:[NSNumber numberWithUnsignedInt:unit] forKey:@"unit"];
					[self.responseData[([NSNumber numberWithUnsignedInt:rid])] setObject:[NSNumber numberWithUnsignedInt:unit] forKey:@"unit"];

					range = NSMakeRange(9 * i + 5, 1);
					bytes = (unsigned char *)[[data subdataWithRange:range] bytes];
					unsigned int length = bytes[0];
					//[self.responseData setObject:[NSNumber numberWithUnsignedInt:length] forKey:@"length"];
					[self.responseData[([NSNumber numberWithUnsignedInt:rid])] setObject:[NSNumber numberWithUnsignedInt:length] forKey:@"length"];
			
					range = NSMakeRange(9 * i + 6, 1);
					bytes = (unsigned char *)[[data subdataWithRange:range] bytes];
					unsigned int siEx = bytes[0];
					//[self.responseData setObject:[NSNumber numberWithUnsignedInt:siEx] forKey:@"siEx"];
					[self.responseData[([NSNumber numberWithUnsignedInt:rid])] setObject:[NSNumber numberWithUnsignedInt:siEx] forKey:@"siEx"];
			
					range = NSMakeRange(9 * i + 7, 4);
					bytes = (unsigned char *)[[data subdataWithRange:range] bytes];
					int32_t value = (bytes[0] << 24) + (bytes[1] << 16) + (bytes[2] << 8) + bytes[3];
					//[self.responseData setObject:[NSNumber numberWithInt:value] forKey:@"value"];
					[self.responseData[([NSNumber numberWithUnsignedInt:rid])] setObject:[NSNumber numberWithUnsignedInt:value] forKey:@"value"];
				}

			}
			else {
				NSLog(@"No registers in reply");
			}
		}
		else if (cid == 0x11) {	// PutRegister
			NSLog(@"PutRegister");
			range = NSMakeRange(2, data.length - 2);
			NSLog(@"%@", [data subdataWithRange:range]);
		}
		self.frameReceived = YES;
		//CFShow((__bridge CFTypeRef)(self.responseData));

	}
	else if (bytes[theFrame.length - 1] == 0x06) {
		NSLog(@"SetClock no CRC");	  // SetClock
		self.frameReceived = YES;
		//CFShow((__bridge CFTypeRef)(self.responseData));
	}
}


#pragma mark - Helper methods
*/
void kmp_crc16() {
	int counter;
	unsigned char *buf;
	
	buf = kmp_frame.data;
	
	kmp_frame.crc16 = 0;
	for (counter = 0; counter < kmp_frame.data_length; counter++) {
		kmp_frame.crc16 = (kmp_frame.crc16 << 8) ^ kmp_crc16_table[((kmp_frame.crc16 >> 8) ^ *(unsigned char *)buf++) & 0x00FF];
	}
}
/*
-(NSNumber *)numberForKmpNumber:(NSNumber *)theNumber andSiEx:(NSNumber *)theSiEx {
	int32_t number = theNumber.intValue;
	int8_t siEx = theSiEx.intValue & 0xff;
	int8_t signI = (siEx & 0x80) >> 7;
	int8_t signE = (siEx & 0x40) >> 6;
	int8_t exponent = (siEx & 0x3f);
	float res = powf(-1, (float)signI) * number * powf(10, (powf(-1, (float)signE) * exponent));
	if ((res - (int)res) == 0.0) {
		return [NSNumber numberWithInt:(int32_t)res];
	}
	else {
		return [NSNumber numberWithFloat:res];
	}
}

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
    unsigned char stuffed_data[KMP_DATA_L];
    unsigned int i;
    unsigned int j = 0;

    memset(stuffed_data, 0x00, KMP_DATA_L);

    for (i = 0; i < kmp_frame.data_length; i++) {
		if ((kmp_frame.data[i] == 0x80) || (kmp_frame.data[i] == 0x40) || (kmp_frame.data[i] == 0x0d) || (kmp_frame.data[i] == 0x06) || (kmp_frame.data[i] == 0x1b)) {
            stuffed_data[j++] = 0x1b;
            stuffed_data[j++] = kmp_frame.data[i] ^ 0xff;
		}
		else {
            stuffed_data[j++] = kmp_frame.data[i];
		}
	}
    memset(kmp_frame.data, 0x00, KMP_DATA_L);
    memcpy(kmp_frame.data, stuffed_data, j);
    kmp_frame.data_length = j;
}

void kmp_byte_unstuff() {
    unsigned char unstuffed_data[KMP_DATA_L];
    unsigned int i;
    unsigned int j = 0;
    
    for (i = 0; i < kmp_frame.data_length; i++) {
        if (kmp_frame.data[i] == 0x1b) {		  // byte unstuffing special char
            unstuffed_data[j++] = kmp_frame.data[i + 1] ^ 0xff;
            i++;
         }
        else {
            unstuffed_data[j++] = kmp_frame.data[i];
        }
    }
    memset(kmp_frame.data, 0x00, KMP_DATA_L);
    memcpy(kmp_frame.data, unstuffed_data, j);
    kmp_frame.data_length = j;
}

void kmp_get_serialized_frame(unsigned char *frame) {
    unsigned int i;

    frame[0] = kmp_frame.start_byte;
    frame[1] = kmp_frame.dst;
    frame[2] = kmp_frame.cid;
    for (i = 0; i < kmp_frame.data_length; i++) {
        frame[3 + i] = kmp_frame.data[i];
    }
    frame[3 + i] = (unsigned char)(kmp_frame.crc16 >> 8);
    frame[4 + i] = (unsigned char)(kmp_frame.crc16 & 0xff);
    frame[5 + i] = kmp_frame.stop_byte;
}


