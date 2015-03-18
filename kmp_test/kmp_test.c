#include <stdio.h>
#include "kmp_test.h"
#include "kmp.h"

int main() {
    unsigned char frame[KMP_FRAME_L];
    unsigned int frame_length;
    uint16_t register_list[1];
    
    unsigned char received_frame[32];
    
    kmd_response_register_list_t response;
    

    unsigned int i;
    
    kmp_init(frame);
    
    frame_length = kmp_get_type(frame);
    for (i = 0; i < frame_length; i++) {
        printf("0x%.2X ", frame[i]);
    }
    printf("\n");
    

    frame_length = kmp_get_serial(frame);
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
    
    received_frame[0] = 0x40;
    received_frame[1] = 0x3F;
    received_frame[2] = 0x01;
    received_frame[3] = 0x00;
    received_frame[4] = 0x01;
    received_frame[5] = 0x07;
    received_frame[6] = 0x01;
    received_frame[7] = 0xFE;
    received_frame[8] = 0x58;
    received_frame[9] = 0x0D;
    kmp_decode_frame(received_frame, 10, response);
   
    received_frame[0] = 0x40;
    received_frame[1] = 0x3F;
    received_frame[2] = 0x02;
    received_frame[3] = 0x00;
    received_frame[4] = 0x69;
    received_frame[5] = 0x2D;
    received_frame[6] = 0x32;
    received_frame[7] = 0xCD;
    received_frame[8] = 0x5D;
    received_frame[9] = 0x0D;
    kmp_decode_frame(received_frame, 10, response);

    received_frame[0] = 0x40;
    received_frame[1] = 0x3F;
    received_frame[2] = 0x10;
    received_frame[3] = 0x00;
    received_frame[4] = 0x98;
    received_frame[5] = 0x33;
    received_frame[6] = 0x04;
    received_frame[7] = 0x00;
    received_frame[8] = 0x02;
    received_frame[9] = 0xA5;
    received_frame[10] = 0xBD;
    received_frame[11] = 0xA0;
    received_frame[12] = 0xDF;
    received_frame[13] = 0x1C;
    received_frame[14] = 0x0D;
    kmp_decode_frame(received_frame, 15, response);

}
