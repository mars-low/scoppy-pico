/*
 * This file is part of the scoppy-pico project.
 *
 * Copyright (C) 2021 FHDM Apps <scoppy@fhdm.xyz>
 * https://github.com/fhdm-dev
 *
 * scoppy-pico is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * scoppy-pico is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with scoppy-pico.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <string.h>

//
#include "md5.h"

int md5_test(char *stringToHash) {
    struct MD5Context context;
    unsigned char checksum[16];
    int i;

    printf("MD5 (\"%s\") = ", stringToHash);
    MD5Init(&context);
    MD5Update(&context, (unsigned char const *)stringToHash, strlen(stringToHash));
    MD5Final(checksum, &context);
    for (i = 0; i < 16; i++) {
        printf("%02x", (unsigned int)checksum[i]);
    }

    unsigned int intChecksum = (checksum[0] << 16) | (checksum[1] << 8) | (checksum[2]);
    printf("\n%x", intChecksum);

    printf("\n");
    return 0;
}
