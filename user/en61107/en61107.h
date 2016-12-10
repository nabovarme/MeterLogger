#define EN61107_FRAME_L 1024
#define EN61107_REGISTER_L 32
#define EN61107_RID_L 6
#define EN61107_UNIT_L 5
#define EN61107_VALUE_L 10
#define EN61107_CUSTOMER_NO_L 13
#define EN61107_METER_TYPE_L 8

typedef struct {
	char value[EN61107_VALUE_L];
	char unit[EN61107_UNIT_L];
} en61107_response_register_t;

typedef struct {
	char customer_no[EN61107_CUSTOMER_NO_L];
	char meter_type[EN61107_METER_TYPE_L];
	en61107_response_register_t t1;
	en61107_response_register_t t2;
	en61107_response_register_t t3;
	en61107_response_register_t tdif;
	en61107_response_register_t flow1;
	en61107_response_register_t effect1;
	en61107_response_register_t hr;
	en61107_response_register_t v1;
	en61107_response_register_t e1;
} en61107_response_t;

ICACHE_FLASH_ATTR
bool parse_en61107_frame(en61107_response_t *response, char *frame, unsigned int frame_length);

ICACHE_FLASH_ATTR
bool parse_mc66cde_inst_values_frame(en61107_response_t *response, char *frame);

ICACHE_FLASH_ATTR
void en61107_response_set_value(en61107_response_t *response, char *rid, char *value, unsigned int value_length);

ICACHE_FLASH_ATTR
void en61107_response_set_unit(en61107_response_t *response, char *rid, char *unit, unsigned int unit_length);
