#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "ets_sys.h"
#include "osapi.h"
#include "user_interface.h"

#include "en61107.h"

unsigned char *en61107_frame;
unsigned int en61107_frame_length;

en61107_response_t *en61107_response;

ICACHE_FLASH_ATTR
unsigned int en61107_get_type(unsigned char *frame) {
	return 0;
}

ICACHE_FLASH_ATTR
unsigned int en61107_get_data(unsigned char *frame) {
    // clear frame
    en61107_frame = frame;
    memset(en61107_frame, 0x00, EN61107_FRAME_L);
	
	strncpy(en61107_frame, "/?!\r\n", EN61107_FRAME_L);
	
	return strlen(en61107_frame);
}

ICACHE_FLASH_ATTR
unsigned int en61107_set_clock(unsigned char *frame, uint64_t unix_time) {
    // DEBUG: not implemented
    return 0;
}

ICACHE_FLASH_ATTR
unsigned int en61107_get_register(unsigned char *frame, uint16_t *register_list, uint16_t register_list_length) {
	return 0;
}

#pragma mark - EN61107 Decoder

ICACHE_FLASH_ATTR
int en61107_decode_frame(unsigned char *frame, unsigned char frame_length, en61107_response_t *response) {
    return 0;
}
