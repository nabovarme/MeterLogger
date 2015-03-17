#include <stdint.h>
#include <stdbool.h>

#define KMP_DATA_L			1024
#define KMP_FRAME_L         (KMP_DATA_L + 6)

void kmp_init();
void kmp_crc16();

void kmp_get_type();
void kmp_get_serial_no();

void kmp_byte_stuff();
void kmp_byte_unstuff();
void kmp_get_serialized_frame(unsigned char *frame);
