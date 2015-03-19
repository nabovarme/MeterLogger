#include <stdint.h>
#include <stdbool.h>

#define KMP_FRAME_L         1024

typedef struct {
    int16_t rid;
    unsigned int unit;
    unsigned int length;
    unsigned int siEx;
    int32_t value;
} kmd_response_register_t;

typedef kmd_response_register_t kmd_response_register_list_t[8];   // max 8 registers per request

typedef struct {
    unsigned int kmp_response_serial;
    unsigned int kmp_response_meter_type;
    unsigned int kmp_response_sw_revision;
    kmd_response_register_list_t kmd_response_register_list;
} kmp_response_t;

void kmp_init(unsigned char *frame);
uint16_t kmp_crc16();

unsigned int kmp_get_type(unsigned char *frame);
unsigned int kmp_get_serial(unsigned char *frame);
unsigned int kmp_set_clock(unsigned char *frame, uint64_t unix_time);
unsigned int kmp_get_register(unsigned char *frame, uint16_t *register_list, uint16_t register_list_length);

bool kmp_decode_frame(unsigned char *frame, unsigned char frame_length, kmp_response_t *response);

void kmp_byte_stuff();
void kmp_byte_unstuff();
