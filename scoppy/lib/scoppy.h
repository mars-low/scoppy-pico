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

#ifndef __SCOPPY_H__
#define __SCOPPY_H__

#include <stdint.h>
#include <stdbool.h>

//
#include "scoppy-context.h"

#define SCOPPY_FATAL_ERROR_UNSUPPORTED_FIRMWARE_VERSION 2
#define SCOPPY_FATAL_ERROR_BAD_APP_PARAMS 3 // bad param sent to us from app
#define SCOPPY_FATAL_ERROR_INCOMING_ERROR 7 // some error in the incoming message. used for debug only.

#define RUN_MODE_RUN 0
#define RUN_MODE_STOP 1
#define RUN_MODE_SINGLE 2

#define TRIGGER_MODE_NONE 0
#define TRIGGER_MODE_AUTO 1
#define TRIGGER_MODE_NORMAL 2
#define TRIGGER_MODE_LAST 2

#define TRIGGER_TYPE_RISING_EDGE 0
#define TRIGGER_TYPE_FALLING_EDGE 1
#define TRIGGER_TYPE_LAST 1

extern const uint8_t scoppy_start_of_message_byte;
extern const uint8_t scoppy_end_of_message_byte;

struct scoppy_channel {
    bool enabled;
    uint8_t voltage_range;
};

// Stuff the app has sent
struct scoppy_app {
    bool is_logic_mode;

    uint8_t run_mode;

    // The timeperiod for the screen in picoseconds
    uint64_t timebasePs;

    // The selected sample rate in samples per second. 0 means auto.
    uint32_t selectedSampleRate;

    // The percentage of the sample record that should be pre-trigger samples
    uint8_t preTriggerSamples;

    uint8_t trigger_mode;

    // channel id in scope mode, mask of channels in logic mode
    uint8_t trigger_channel;

    uint8_t trigger_type;

    // in logic mode, each bit corresponds to a channel - high or low
    // bits for channels not set in trigger_channels should be ignored
    uint8_t trigger_level;

    // true if the app settings have changed.
    bool dirty;

    // true if a resync is required
    // usually only if the app mode has changed
    bool resync_required;
};

#define MAX_CHANNELS 8

// The current settings for the scope
struct scoppy {

    // Our preferred sample rate per channel
    //uint32_t preferredSampleRatePerChannelHz;

    struct scoppy_channel channels[MAX_CHANNELS];

    struct scoppy_app app;

    // true if the any of the channel settings have changed.
    bool channels_dirty;
};

extern struct scoppy scoppy;

void scoppy_main(struct scoppy_context *ctx);
int scoppy_get_num_enabled_channels();
void recalculate_sample_rate(struct scoppy_context *ctx);

#endif // __SCOPPY_H__
