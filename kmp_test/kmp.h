#include <stdint.h>
#include <stdbool.h>

#define KMP_FRAME_L         1024

void kmp_init(unsigned char *frame);
void kmp_crc16();

unsigned int kmp_get_type(unsigned char *frame);
unsigned int kmp_get_serial_no(unsigned char *frame);
unsigned int kmp_set_clock(unsigned char *frame, uint64_t unix_time);
unsigned int kmp_get_register(unsigned char *frame, uint16_t *register_list, uint16_t register_list_length);

void kmp_byte_stuff();
void kmp_byte_unstuff();
