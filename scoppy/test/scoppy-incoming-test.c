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

// my stuff
#include "scoppy-incoming.h"
#include "assert.h"
#include "fake-serial.h"
#include "scoppy-incoming-test.h"
#include "scoppy-test.h"

static int read_message(struct scoppy_incoming *incoming) {

    int ret;
    while ((ret = scoppy_read_incoming(fake_serial_read, incoming)) == SCOPPY_INCOMING_INCOMPLETE) {
        //scoppy_debug_incoming(msg);
    }

    //if (ret == SCOPPY_INCOMING_ERROR) {
    //    printf("Error: %s\n", scoppy_incoming_error());
    //}

    return ret;
}

void run_scoppy_incoming_test() {
    TPRINTF("scoppy_incoming_test...");

    //
    // NB. This doesn't test the payload itself.  Just message size etc.
    // Payload tests should go in scoppy-message-test.c
    //

    struct scoppy_incoming incoming;
    scoppy_init_incoming(&incoming);

    // som - 255
    // msg size - 2 bytes
    // msg type
    // msg type + 5
    // msg version
    // payload
    // eom - 86

    TPRINTF(" 1 ");
    uint8_t serial_data[] = {255, 0, 9, 10, 15, 1, 99, 99, 86, 77,77,77,77};
    fake_serial_set_data(serial_data, sizeof(serial_data));
    int ret = read_message(&incoming);
    if(ret != SCOPPY_INCOMING_COMPLETE) {
        printf("%s\n", scoppy_incoming_error());
    }
    assert(ret == SCOPPY_INCOMING_COMPLETE);
    scoppy_init_incoming(&incoming);

    TPRINTF(" 2 ");
    fake_serial_set_data(serial_data, sizeof(serial_data));
    fake_serial_set_max_read_count(1);
    ret = read_message(&incoming);
    assert(ret == SCOPPY_INCOMING_COMPLETE);
    scoppy_init_incoming(&incoming);

    TPRINTF(" 3 ");
    uint8_t serial_data2[] = {66, 66, 66, 255, 0, 7, 10, 99, 99, 86};
    fake_serial_set_data(serial_data2, sizeof(serial_data2));
    fake_serial_set_max_read_count(1);
    ret = read_message(&incoming);
    assert(ret == SCOPPY_INCOMING_COMPLETE);
    scoppy_init_incoming(&incoming);

    // Now try an invalid message (0 message type) followed by valid message
    TPRINTF(" 3 ");
    uint8_t serial_data3[] = {255, 0, 7, 0, 99, 99, 86, 255, 0, 7, 10, 99, 99, 86};
    fake_serial_set_data(serial_data3, sizeof(serial_data3));
    // ... the first try will fail
    ret = read_message(&incoming);
    assert(ret == SCOPPY_INCOMING_ERROR);
    // ... the second should succeed
    ret = read_message(&incoming);
    assert(ret == SCOPPY_INCOMING_COMPLETE);
    scoppy_init_incoming(&incoming);

    printf(" OK\n");
}
