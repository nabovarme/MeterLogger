#ifndef EN61107_H
#define EN61107_H

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
	uint8_t a;
	uint8_t b;
	uint32_t ccc;
	uint16_t dd;
	uint8_t e;
	uint16_t ff;
	uint16_t gg;
} en61107_meter_program_t;

typedef struct {
	char customer_no[EN61107_CUSTOMER_NO_L];
	char meter_type[EN61107_METER_TYPE_L];
	en61107_meter_program_t meter_program;
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

typedef enum {
	UART_STATE_NONE,
	UART_STATE_STANDARD_DATA_2,
	UART_STATE_UNKNOWN_1,
	UART_STATE_UNKNOWN_2,
	UART_STATE_SERIAL_BYTE_1,
	UART_STATE_SERIAL_BYTE_2,
	UART_STATE_SERIAL_BYTE_3,
	UART_STATE_SERIAL_BYTE_4,
	UART_STATE_SERIAL_BYTE_5,
	UART_STATE_SERIAL_BYTE_6,
	UART_STATE_UNKNOWN_3,
	UART_STATE_UNKNOWN_4,
	UART_STATE_EN61107,
	UART_STATE_INST_VALUES
} en61107_uart_state_t;

ICACHE_FLASH_ATTR
bool parse_en61107_frame(en61107_response_t *response, char *frame, unsigned int frame_length);

ICACHE_FLASH_ATTR
bool parse_mc66cde_standard_data_2_frame(en61107_response_t *response, char *frame, unsigned int frame_length);

ICACHE_FLASH_ATTR
bool parse_mc66cde_inst_values_frame(en61107_response_t *response, char *frame, unsigned int frame_length);

ICACHE_FLASH_ATTR
void en61107_response_set_value(en61107_response_t *response, char *rid, char *value, unsigned int value_length);

ICACHE_FLASH_ATTR
void en61107_response_set_unit(en61107_response_t *response, char *rid, char *unit, unsigned int unit_length);

#endif

