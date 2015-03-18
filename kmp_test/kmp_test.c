#include <stdio.h>
#include "kmp_test.h"
#include "kmp.h"

int main() {
    unsigned char frame[KMP_FRAME_L];
    unsigned int frame_length;
    uint16_t register_list[1];
    
    unsigned int i;
    
    kmp_init(frame);
    
	frame_length = kmp_get_serial_no(frame);
    for (i = 0; i < frame_length; i++) {
        printf("0x%.2X ", frame[i]);
    }
    printf("\n");
    
    register_list[0] = 0x98;
    frame_length = kmp_get_register(frame, register_list, 1);
    for (i = 0; i < frame_length; i++) {
        printf("0x%.2X ", frame[i]);
    }
    printf("\n");

}
