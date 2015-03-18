#include <errno.h>
#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <string.h>
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
    
    /*
    frame_length = kmp_get_type(frame);
    for (i = 0; i < frame_length; i++) {
        printf("0x%.2X ", frame[i]);
    }
    printf("\n");
    
    */
    frame_length = kmp_get_serial(frame);
    for (i = 0; i < frame_length; i++) {
        printf("0x%.2X ", frame[i]);
    }
    printf("\n");
    /*
    
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
    */
    
    char *portname = "/dev/tty.usbserial-A6YNEE07";
//  int fd = open (portname, O_RDWR | O_NOCTTY | O_SYNC);
    int fd = open (portname, O_RDWR | O_NOCTTY | O_NONBLOCK);   // mac os x
    if (fd < 0) {
        printf("error %d opening %s: %s", errno, portname, strerror(errno));
        return 0;
    }
    
    set_interface_attribs (fd, B1200, 0);  // set speed to 1200 bps, 8n2 (no parity)
    set_blocking (fd, 0);                // set no blocking
    
    write (fd, frame, frame_length);           // send 7 character greeting
    
    //usleep ((7 + 25) * 100);             // sleep enough to transmit the 7 plus
    // receive 25:  approx 100 uS per char transmit
    char buf [100];
    while (1) {
        int n = read(fd, buf, sizeof buf);  // read up to 100 characters if ready to read
        if (n) {
            printf("%x", n);
        }
    }
}

int set_interface_attribs (int fd, int speed, int parity) {
    struct termios tty;
    memset (&tty, 0, sizeof tty);
    if (tcgetattr (fd, &tty) != 0) {
        printf("error %d from tcgetattr", errno);
        return -1;
    }
    
    cfsetospeed(&tty, speed);
    cfsetispeed(&tty, speed);
    
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;     // 8-bit chars
    // disable IGNBRK for mismatched speed tests; otherwise receive break
    // as \000 chars
    tty.c_iflag &= ~IGNBRK;         // disable break processing
    tty.c_lflag = 0;                // no signaling chars, no echo,
    // no canonical processing
    tty.c_oflag = 0;                // no remapping, no delays
    tty.c_cc[VMIN]  = 0;            // read doesn't block
    tty.c_cc[VTIME] = 5;            // 0.5 seconds read timeout
    
    tty.c_iflag &= ~(IXON | IXOFF | IXANY); // shut off xon/xoff ctrl
    
    tty.c_cflag |= (CLOCAL | CREAD);// ignore modem controls,
    // enable reading
    tty.c_cflag &= ~(PARENB | PARODD);      // shut off parity
    tty.c_cflag |= parity;
    //tty.c_cflag &= ~CSTOPB;
    tty.c_cflag |= CSTOPB;      // two stop bits
    tty.c_cflag &= ~CRTSCTS;
    
    if (tcsetattr (fd, TCSANOW, &tty) != 0) {
        printf("error %d from tcsetattr", errno);
        return -1;
    }
    return 0;
}

void set_blocking (int fd, int should_block) {
    struct termios tty;
    memset (&tty, 0, sizeof tty);
    if (tcgetattr (fd, &tty) != 0)
    {
        printf("error %d from tggetattr", errno);
        return;
    }
    
    tty.c_cc[VMIN]  = should_block ? 1 : 0;
    tty.c_cc[VTIME] = 5;            // 0.5 seconds read timeout
    
    if (tcsetattr (fd, TCSANOW, &tty) != 0) {
        printf("error %d setting term attributes", errno);
    }
}

