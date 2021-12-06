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

#include <assert.h>
#include <stdio.h>

// my stuff
#include "scoppy-outgoing.h"
#include "scoppy-util/number.h"
#include "scoppy.h"

static struct scoppy_outgoing msg_instance;
static char *last_error = "???";

#define SCOPPY_OUTGOING_PRE 0x5555 // 0101
#define SCOPPY_OUTGOING_POST 0xAAAA // 1010

void scoppy_init_outgoing() {
    msg_instance.pre = SCOPPY_OUTGOING_PRE;
    msg_instance.pre_data = SCOPPY_OUTGOING_PRE;

    msg_instance.post = SCOPPY_OUTGOING_POST;
    msg_instance.post_data = SCOPPY_OUTGOING_POST;
}

#ifndef NDEBUG
static void check_outgoing() {
    char *msg = NULL;
    if (msg_instance.pre != SCOPPY_OUTGOING_PRE) {
        msg = "scoppy_outgoing - pre clobbered";
    }
    else if (msg_instance.pre_data != SCOPPY_OUTGOING_PRE) {
        msg = "scoppy_outgoing - pre data clobbered";
    }
    else    if (msg_instance.post != SCOPPY_OUTGOING_POST) {
        msg = "scoppy_outgoing - post clobbered";
    }
    else if (msg_instance.post_data != SCOPPY_OUTGOING_POST) {
        msg = "scoppy_outgoing - post data clobbered";
    }

    if (msg != NULL) {
        printf("%s\n", msg);

        // give printf time to be sent (no access to sleep_ms here!)
        int j = 0;
        for (int i = 0; i < 100000000; i++) {
            j += i;
        }
        assert(j == 0);
    }
}
    #define CHECK_OUTGOING() check_outgoing()
#else
#define CHECK_OUTGOING()
#endif

struct scoppy_outgoing *scoppy_new_outgoing(uint8_t msg_type, uint8_t msg_version) {
    CHECK_OUTGOING();
    msg_instance.msg_type = msg_type;
    msg_instance.msg_version = msg_version;
    msg_instance.payload_len = 0;
    msg_instance.msg_size = -1;
    msg_instance.payload = &msg_instance.data[6];
    return &msg_instance;
}

void scoppy_prepare_outgoing(struct scoppy_outgoing *msg) {
    CHECK_OUTGOING();

    msg->msg_size = 1 +               // start byte
                    2 +               // message_size
                    1 +               // message type
                    1 +               // message type + 5
                    1 +               // message version
                    msg->payload_len; //
    //                1;                 // end of message byte

    msg->data[0] = scoppy_start_of_message_byte;

    scoppy_uint16_to_2_network_bytes(msg->data + 1, msg->msg_size);

    msg->data[3] = msg->msg_type;
    msg->data[4] = msg->msg_type + 5;
    msg->data[5] = msg->msg_version;

    CHECK_OUTGOING();
}

int scoppy_write_outgoing(int (*write_serial)(uint8_t *, int, int), struct scoppy_outgoing *msg) {
    CHECK_OUTGOING();
    scoppy_prepare_outgoing(msg);
    int ret = write_serial(msg->data, 0, msg->msg_size);
    CHECK_OUTGOING();
    return ret;
}

void scoppy_debug_outgoing(struct scoppy_outgoing *data) {
    //printf("\n");
}

char *scoppy_outgoing_error() {
    return last_error;
}
