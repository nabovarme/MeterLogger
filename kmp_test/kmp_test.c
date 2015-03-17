#include <stdio.h>
#include "kmp_test.h"
#include "kmp.h"

int main() {
    unsigned char frame[KMP_FRAME_L];

    kmp_init();
	kmp_get_serial_no();
    kmp_get_serialized_frame(frame);
    printf("%s", frame);
}
