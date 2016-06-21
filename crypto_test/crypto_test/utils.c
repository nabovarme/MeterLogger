//
//  utils.c
//  crypto_test
//
//  Created by stoffer on 21/06/2016.
//  Copyright © 2016 stoffer. All rights reserved.
//

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include "utils.h"

void os_get_random(char *s, size_t length) {
    uint i;
    srand(time(NULL));
    for (i = 0; i < length; i++) {
        s[i] = rand()%256;
    }
}

