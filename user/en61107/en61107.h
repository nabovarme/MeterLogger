#define EN61107_FRAME_L 256
#define EN61107_REGISTER_L 32
#define EN61107_RID_L 6
#define EN61107_UNIT_L 5
#define EN61107_VALUE_L 10
#define EN61107_SERIAL_L 13
#define EN61107_METER_TYPE_L 8

typedef struct {
    char rid[EN61107_RID_L];
    char unit[EN61107_UNIT_L];
    char value[EN61107_VALUE_L];
} en61107_response_register_t;

typedef en61107_response_register_t en61107_response_register_list_t[8];   // max 8 registers per request

typedef struct {
    char en61107_response_serial[EN61107_SERIAL_L];
    char en61107_response_meter_type[EN61107_METER_TYPE_L];
    en61107_response_register_list_t en61107_response_register_list;
} en61107_response_t;

ICACHE_FLASH_ATTR
int parse_en61107_frame(en61107_response_t *en61107_response, char *en61107_frame, unsigned int en61107_frame_length);

ICACHE_FLASH_ATTR
int parse_mc66cde_frame(int32_t *mc66cde_response, char *mc66cde_frame);

