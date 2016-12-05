#include <esp8266.h>

#include "en61107.h"

char *en61107_frame;
unsigned int en61107_frame_length;

en61107_response_t *en61107_response;

#pragma mark - EN61107 Decoder

ICACHE_FLASH_ATTR
int en61107_decode_frame(char *frame, unsigned char frame_length, en61107_response_t *response) {
    return 0;
}
