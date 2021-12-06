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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

//
#include "hardware/regs/sysinfo.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include "pico/unique_id.h"

//
#include "pico-scoppy-core0-looper.h"
#include "pico-scoppy-ctx.h"
#include "pico-scoppy-pwm-sig-gen.h"
#include "pico-scoppy-samples.h"
#include "pico-scoppy-util.h"
#include "pico-scoppy.h"
#include "scoppy_usb.h"

static int debugf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    printf(fmt, args);
    va_end(args);
    return 0;
}

static int errorf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    printf(fmt, args);
    va_end(args);
    return 0;
}

static void ctx_sleep_ms(uint32_t msec) { sleep_ms(msec); }

static void ctx_tight_loop(void) { sleep_ms(1); }

static int ctx_read_serial(uint8_t *buf, int offset, int len) { return scoppy_usb_in_chars((char *)(buf + offset), len); }

void ctx_fatal_error_handler(int error) {
    printf("Fatal error: code=%d\n", error);

    // flash for 'error' number of times and then pause
    // repeat indefinitely
    while (true) {
        for (int i = 0; i < error; i++) {
            gpio_put(LED_PIN, true);
            sleep_ms(200);
            gpio_put(LED_PIN, false);
            sleep_ms(300);
        }

        sleep_ms(2000);
    }

    exit(1);
}

void ctx_set_status_led(bool status) { gpio_put(LED_PIN, status); }

static int ctx_write_serial(uint8_t *buf, int offset, int len) {
    if (!scoppy_usb_out_chars((char *)(buf + offset), len)) {
        // Failed due to rentrancy from the same core. wtf?
        assert(false);
        sleep_ms(2000);
    }

    // we're always assuming all bytes are written successfully. This is not the case if
    // usb is not connected.
    return len;
}

static void ctx_start_main_loop(struct scoppy_context *ctx) { pico_scoppy_start_core0_loop(ctx); }

static struct scoppy_context ctx;
struct scoppy_context *pico_scoppy_get_context() {
    ctx.read_serial = ctx_read_serial;
    ctx.write_serial = ctx_write_serial;
    ctx.tight_loop = ctx_tight_loop;
    ctx.sleep_ms = ctx_sleep_ms;
    ctx.debugf = debugf;
    ctx.errorf = errorf;
    ctx.start_main_loop = ctx_start_main_loop;
    ctx.fatal_error_handler = ctx_fatal_error_handler;
    ctx.set_status_led = ctx_set_status_led;
    ctx.sig_gen = pwm_sig_gen;

    pico_scoppy_seed_random();
    ctx.has_stdio = true;
    ctx.is_testing = false;

    pico_unique_board_id_t unique_id;
    pico_get_unique_board_id(&unique_id);
    memcpy(ctx.uniqueId, unique_id.id, sizeof(ctx.uniqueId));

    ctx.chipId = *((io_ro_32 *)(SYSINFO_BASE + SYSINFO_CHIP_ID_OFFSET));

#ifndef PICO_SCOPPY_BUILD_NUMBER
#error rats
#endif

#if PICO_SCOPPY_BUILD_NUMBER < 0
#error invalid build number
#endif

    ctx.build_number = PICO_SCOPPY_BUILD_NUMBER;

// Actually defined in CMakeLists.txt
#ifndef PICO_SCOPPY_VERSION
#error rats
#endif

    ctx.firmware_version = PICO_SCOPPY_VERSION;
    ctx.firmware_type = 2;

    return &ctx;
}

// from platform.c

//#define MANUFACTURER_RPI 0x927
//#define PART_RP2 0x2
