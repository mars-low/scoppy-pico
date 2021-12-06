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

#include <stdint.h>

//
#include "number.h"

// https://stackoverflow.com/questions/3991478/building-a-32-bit-float-out-of-its-4-composite-bytes

// unpack method for retrieving data in network byte,
//   big endian, order (MSB first)
// increments index i by the number of bytes unpacked
// usage:
//   int i = 0;
//   float x = unpackFloat(&buffer[i], &i);
//   float y = unpackFloat(&buffer[i], &i);
//   float z = unpackFloat(&buffer[i], &i);
/*
float scoppy_float_from_4_network_bytes(const void *buf) {
    const uint8_t *b = (const uint8_t *)buf;
    uint32_t temp = 0;
    temp = ((b[0] << 24) |
            (b[1] << 16) |
            (b[2] << 8) |
            b[3]);
    return *((float *)&temp);
}
*/

uint64_t scoppy_uint64_from_8_network_bytes(const void *buf) {

    // https://stackoverflow.com/questions/12240299/convert-bytes-to-int-uint-in-c

    const uint8_t *b = (const uint8_t *)buf;
    return ((uint64_t)b[0] << 56) | (uint64_t)b[1] << 48 | (uint64_t)b[2] << 40 | (uint64_t)b[3] << 32 | (uint64_t)b[4] << 24 | (uint64_t)b[5] << 16 |
           (uint64_t)b[6] << 8 | (uint64_t)b[7];
}

int32_t scoppy_int32_from_4_network_bytes(const void *buf) {

    // https://stackoverflow.com/questions/12240299/convert-bytes-to-int-uint-in-c

    const uint8_t *b = (const uint8_t *)buf;
    return ((int32_t)b[0] << 24) | (int32_t)b[1] << 16 | (int32_t)b[2] << 8 | (int32_t)b[3];
}

uint32_t scoppy_uint32_from_4_network_bytes(const void *buf) {

    // https://stackoverflow.com/questions/12240299/convert-bytes-to-int-uint-in-c

    const uint8_t *b = (const uint8_t *)buf;
    return ((uint32_t)b[0] << 24) | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | (uint32_t)b[3];
}

int16_t scoppy_int16_from_2_network_bytes(const void *buf) {

    // https://stackoverflow.com/questions/12240299/convert-bytes-to-int-uint-in-c

    const uint8_t *b = (const uint8_t *)buf;
    return ((int16_t)b[0] << 8) | (int16_t)b[1];
}

uint16_t scoppy_uint16_from_2_network_bytes(const void *buf) {
    const uint8_t *b = (const uint8_t *)buf;
    return ((uint16_t)b[0] << 8 | (uint16_t)b[1]);
}

uint8_t scoppy_uint8_from_1_network_byte(const void *buf) {
    const uint8_t *b = (const uint8_t *)buf;
    return ((uint16_t)b[0]);
}

void scoppy_uint32_to_4_network_bytes(void *buf, uint32_t value) {
    uint8_t *b = (uint8_t *)buf;
    b[0] = (value >> 24) & 0xFF;
    b[1] = (value >> 16) & 0xFF;
    b[2] = (value >> 8) & 0xFF;
    b[3] = (value >> 0) & 0xFF;
}

void scoppy_int32_to_4_network_bytes(void *buf, int32_t value) {
    uint8_t *b = (uint8_t *)buf;
    b[0] = (value >> 24) & 0xFF;
    b[1] = (value >> 16) & 0xFF;
    b[2] = (value >> 8) & 0xFF;
    b[3] = (value >> 0) & 0xFF;
}

void scoppy_uint16_to_2_network_bytes(void *buf, uint16_t value) {
    uint8_t *b = (uint8_t *)buf;
    b[0] = (value >> 8) & 0xFF;
    b[1] = (value >> 0) & 0xFF;
}

// pack method for storing data in network,
//   big endian, byte order (MSB first)
// returns number of bytes packed
// usage:
//   float x, y, z;
//   int i = 0;
//   i += packFloat(&buffer[i], x);
//   i += packFloat(&buffer[i], y);
//   i += packFloat(&buffer[i], z);
/*
int packFloat(void *buf, float x) {
    unsigned char *b = (unsigned char *)buf;
    unsigned char *p = (unsigned char *) &x;
#if defined (_M_IX86) || (defined (CPU_FAMILY) && (CPU_FAMILY == I80X86))
    b[0] = p[3];
    b[1] = p[2];
    b[2] = p[1];
    b[3] = p[0];
#else
    b[0] = p[0];
    b[1] = p[1];
    b[2] = p[2];
    b[3] = p[3];
#endif
    return 4;
}
*/
