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

#pragma once

#include <stdbool.h>
#include <stdint.h>

//
#include "scoppy-incoming.h"

struct scoppy_context {

    // JEDEC JEP-106 compliant chip identifier.
    uint32_t chipId;
    uint8_t uniqueId[8];
    uint8_t firmware_type;
    uint8_t firmware_version;
    int32_t build_number;
    bool has_stdio;
    bool is_testing;
    struct scoppy_incoming *incoming;

    int (*read_serial)(uint8_t *, int, int);
    int (*write_serial)(uint8_t *, int, int);
    void (*tight_loop)(void);
    void (*sleep_ms)(uint32_t);
    int (*debugf)( const char *format, ... );
    int (*errorf)( const char *format, ... );
    void (*start_main_loop)(struct scoppy_context *ctx);
    //void (*on_sampling_params_changed)(void);
    void (*fatal_error_handler)(int);
    void (*set_status_led)(bool);
    void (*sig_gen)(uint8_t function, unsigned gpio, uint32_t freq, uint16_t duty);
};