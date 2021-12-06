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
#include <stdint.h>

// my stuff
#include "fake-serial.h"

static int max_read_count = 9999;
static int serial_data_idx = 0;
static int serial_data_len = 0;
static uint8_t *serial_data = NULL;
int fake_serial_read(uint8_t *buf, int offset, int count) {
    int remaining = serial_data_len - serial_data_idx;
    if (remaining <= 0) {
        return 0;
    }

    if (remaining < count) {
        count = remaining;
    }

    // to test one byte at a time
    if (count > 20) {
        count = 1;
    }

    memcpy((void *)(buf + offset), (void *)(serial_data + serial_data_idx), count);
    serial_data_idx += count;

    return count;
}

void fake_serial_set_max_read_count(int count) {
    max_read_count = count;
}

void fake_serial_set_data(uint8_t *data, int len) {
    serial_data = data;
    serial_data_idx = 0;
    serial_data_len = len;
}

int fake_serial_write(uint8_t *buf, int offset, int count) {
    return 0;
}