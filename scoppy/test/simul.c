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

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

//
#include "simul.h"
#include "scoppy-incoming.h"
#include "scoppy-message.h"
#include "scoppy.h"
#include "fake-serial.h"

int ctx_debugf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    return 0;
}

int ctx_errorf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    va_end(args);
    return 0;
}

void ctx_sleep_ms(uint32_t msec) {
    // https://stackoverflow.com/questions/1157209/is-there-an-alternative-sleep-function-in-c-to-milliseconds
    struct timespec ts;
    int res;

    if (msec < 0) {
        errno = EINVAL;
        return;
    }

    ts.tv_sec = msec / 1000;
    ts.tv_nsec = (msec % 1000) * 1000000;

    do {
        res = nanosleep(&ts, &ts);
    } while (res && errno == EINTR);
}

void ctx_tight_loop(void) {
    ctx_sleep_ms(1);
}

void ctx_fatal_error_handler(int error) {
    printf("Fatal error: code=%d\n", error);
    exit(1);
}

void ctx_set_status_led(bool status){};

struct scoppy_outgoing *ctx_get_samples() {
    // uint8_t samples[] = {1, 2, 3, 4, 5};
    // memcpy(buffer, samples, sizeof(samples));
    // return sizeof(samples);
    return NULL;
}

void ctx_on_sampling_params_changed(void) {}

void run_scoppy_simulation() {
    struct scoppy_context ctx;
    ctx.read_serial = fake_serial_read;
    ctx.write_serial = fake_serial_write;
    ctx.tight_loop = ctx_tight_loop;
    ctx.sleep_ms = ctx_sleep_ms;
    ctx.debugf = ctx_debugf;
    ctx.errorf = ctx_errorf;
    //ctx.get_samples = ctx_get_samples;
    //ctx.on_sampling_params_changed = ctx_on_sampling_params_changed;
    ctx.fatal_error_handler = ctx_fatal_error_handler;
    ctx.set_status_led = ctx_set_status_led;

    ctx.build_number = 0x71234589;
    ctx.has_stdio = true;
    ctx.is_testing = true;

    uint8_t incoming_data[] = {

        // An invalid message (bad message type)
        scoppy_start_of_message_byte,
        0, 5,
        0,
        scoppy_end_of_message_byte,

        // Response with invalid message type - ignored
        scoppy_start_of_message_byte,
        0, 5,
        //SCOPPY_INCOMING_MSG_TYPE_SYNC_RESPONSE,
        99,
        scoppy_end_of_message_byte,

        // A valid sync response message
        scoppy_start_of_message_byte,
        0, 23,
        SCOPPY_INCOMING_MSG_TYPE_SYNC_RESPONSE, SCOPPY_INCOMING_MSG_TYPE_SYNC_RESPONSE + 5,
        0x03,                   // flags
        0xE1, 0xA9, 0xF2, 0x2A, // build number
        0x02, 0x02, 0x01,       // 2 channels. Turn on CH2 only (id=1)
        0x00, 0x00,             // Input voltage range offsets

        // 0xFE, 0xDB, 0x09, 0xE2, // Timebase: 42757677780000ps
        // 0x1A, 0x67,             // Max transfer bytes: 6759
        // 0xF5,                   // Max transfer per second: 245

        0x00, 0x01, 0x86, 0xA0, // Timebase: 1 ms (in deci microseconds 10^8)
        0x03, 0xE8,             // Max transfer bytes: 1000
        0x05,                   // Max transfer per second: 5

        scoppy_end_of_message_byte};

    fake_serial_set_max_read_count(1);
    fake_serial_set_data(incoming_data, sizeof(incoming_data));
    scoppy_main(&ctx);
}
