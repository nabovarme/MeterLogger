
#define KMP_FRAME_L         1024

typedef struct {
    int16_t rid;
    uint8_t unit;
    uint8_t length;
    uint8_t si_ex;
    int32_t value;
} kmp_response_register_t;

typedef kmp_response_register_t kmp_response_register_list_t[8];   // max 8 registers per request

typedef struct {
    unsigned int kmp_response_serial;
    unsigned int kmp_response_meter_type;
    unsigned int kmp_response_sw_revision;
    kmp_response_register_list_t kmp_response_register_list;
} kmp_response_t;

uint16_t kmp_crc16();
int kmp_pow(int a, int b);

unsigned int kmp_get_type(unsigned char *frame);
unsigned int kmp_get_serial(unsigned char *frame);
unsigned int kmp_set_clock(unsigned char *frame, uint64_t unix_time);
unsigned int kmp_get_register(unsigned char *frame, uint16_t *register_list, uint16_t register_list_length);

int kmp_decode_frame(unsigned char *frame, unsigned char frame_length, kmp_response_t *response);

double kmp_value_to_double(int32_t value, uint8_t si_ex);
void kmp_unit_to_string(uint8_t unit, char *unit_string);
void kmp_byte_stuff();
void kmp_byte_unstuff();
