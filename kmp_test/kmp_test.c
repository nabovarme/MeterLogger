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

int main(int argc, char *argv[]) {
    unsigned int i;
    unsigned char c;
    ssize_t c_length;
    
    unsigned char serial_port_dev[1024];
    
    if (argc > 1) {
        strcpy(serial_port_dev, argv[1]);
    }
    else {
        strcpy(serial_port_dev, "/dev/tty.usbserial-A6YNEE07");
    }
    
    // allocate frame to send
    unsigned char frame[KMP_FRAME_L];
    unsigned int frame_length;
    
    // allocate struct for response
    kmp_response_t response;
    
    // open serial port
#ifdef __APPLE__
    int fd = open (serial_port_dev, O_RDWR | O_NOCTTY | O_NONBLOCK);   // mac os x
#else
    int fd = open (serial_port_dev, O_RDWR | O_NOCTTY | O_SYNC);
#endif
    if (fd < 0) {
        printf("error %d opening %s: %s", errno, serial_port_dev, strerror(errno));
        return 0;
    }
    set_interface_attribs (fd, B1200, 0);  // set speed to 1200 bps, 8n2 (no parity)
    set_blocking (fd, 0);                // set no blocking
    

    // initialize kmp
    kmp_init(frame);

    // get serial
    // prepare frame
    frame_length = kmp_get_serial(frame);
    
    // print frame
    for (i = 0; i < frame_length; i++) {
        printf("0x%.2X ", frame[i]);
    }
    printf("\n");
    
    // send frame
    write(fd, frame, frame_length);     // send kmp request
    
    usleep (1000000);             // sleep 1 seconds

    // read data
    memset(frame, 0x00, sizeof(frame));    // clear frame
    frame_length = 0;
    c_length = 0;
    
    do {
        c_length = read(fd, &c, 1);
        if (c_length) {
            frame[frame_length++] = c;
        }
    } while (c_length);
    
    // print received frame
    for (i = 0; i < frame_length; i++) {
        printf("0x%.2X ", frame[i]);
    }
    printf("\n");

    // decode frame
    kmp_decode_frame(frame, frame_length, &response);
    
    printf("serial number is: %u\n", response.kmp_response_serial);
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

