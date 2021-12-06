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

// my stuff
#include "scoppy-outgoing.h"
#include "scoppy.h"
#include "assert.h"
#include "fake-serial.h"
#include "scoppy-outgoing-test.h"

void read_message() {

    // struct scoppy_incoming *msg = scoppy_new_incoming_message();

    // int ret;
    // while ((ret = scoppy_read_incoming(fake_serial_read, msg)) == SCOPPY_INCOMING_INCOMPLETE) {
    //     //scoppy_debug_incoming(msg);
    // }

    // if (ret == SCOPPY_INCOMING_ERROR) {
    //     printf("Error: %s\n", scoppy_incoming_error());
    // }
    // assert(ret == SCOPPY_INCOMING_COMPLETE);
}

void run_scoppy_outgoing_test() {
    printf("scoppy-outgoing-test: ");

    uint8_t msg_type = 33;
    struct scoppy_outgoing *msg = scoppy_new_outgoing(msg_type, 1);
    assert(msg->msg_type == msg_type);

    // A message without a payload
    msg->payload_len = 0;

    scoppy_prepare_outgoing(msg);
    assert(msg->payload_len == 0);
    assert(msg->msg_size == 5);
    assert(msg->data[0] == scoppy_start_of_message_byte);
    assert(msg->data[1] == 0);  // size
    assert(msg->data[2] == 5);  // size
    assert(msg->data[3] == 33); // type
    assert(msg->data[4] == 38); // type
    //assert(msg->data[4] == scoppy_start_of_message_byte);

    // A payload of one byte
    *(msg->payload) = 44;
    msg->payload_len = 1;

    scoppy_prepare_outgoing(msg);
    assert(msg->msg_size == 6);
    assert(msg->data[0] == scoppy_start_of_message_byte);
    assert(msg->data[1] == 0);  // size
    assert(msg->data[2] == 6);  // size
    assert(msg->data[3] == 33); // type
    assert(msg->data[4] == 38); // type
    assert(msg->data[5] == 44); // payload
    //assert(msg->data[5] == scoppy_start_of_message_byte);

    // max payload size
    memset(msg->payload, 55, SCOPPY_OUTGOING_MAX_PAYLOAD_SIZE);
    msg->payload_len = SCOPPY_OUTGOING_MAX_PAYLOAD_SIZE;

    scoppy_prepare_outgoing(msg);
    assert(msg->msg_size == SCOPPY_OUTGOING_MAX_PAYLOAD_SIZE + 5); // 4101 = 1005
    assert(msg->data[0] == scoppy_start_of_message_byte);
    assert(msg->data[1] == 16);  // size
    assert(msg->data[2] == 5);  // size
    assert(msg->data[3] == 33); // type
    assert(msg->data[4] == 38); // type
    assert(msg->data[5] == 55); // start payload
    assert(msg->data[SCOPPY_OUTGOING_MAX_PAYLOAD_SIZE + 3] == 55); // end of payload
    //assert(msg->data[SCOPPY_OUTGOING_MAX_PAYLOAD_SIZE + 4] == scoppy_start_of_message_byte);

    printf("OK\n");
}
