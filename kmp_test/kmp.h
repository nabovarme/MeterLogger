#include <stdint.h>
#include <stdbool.h>

#define KMP_DATA_L			1024
#define KMP_CRC16_TABLE_L	256
	
typedef struct {
	uint8_t start_byte;
	uint8_t dst;
	uint8_t cid;
	unsigned char data[KMP_DATA_L];
	uint16_t data_length;
	uint16_t crc16;
	uint8_t stop_byte;
} kmp_frame_t;

kmp_frame_t *kmp_frame;


void init();
void kmp_crc16();
void kmp_get_serial_no();
