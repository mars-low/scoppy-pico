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

#include "scoppy.h"

// This must be an even number
// Also using powers of 2 because some FFT algorithms require this
// 2^11 = 2k
//#define BYTES_TO_SEND_PER_CHANNEL 2048
#define BYTES_TO_SEND_PER_CHANNEL 2000
// 2^17 = 128k
//#define SINGLE_SHOT_TOTAL_BYTES_TO_SEND 131072 
#define SINGLE_SHOT_TOTAL_BYTES_TO_SEND 100000 

#define SAMPLING_PARAMS_PRE 0xCAFE
#define SAMPLING_PARAMS_POST 0xD9AB

struct sampling_params {
    // for debugging
    uint32_t pre;

    uint32_t preferredSampleRatePerChannelHz;
    uint32_t realSampleRatePerChannel;
    uint32_t clkdivint;

    // The total number of bytes for all channels (not bytes per channel!)
    int num_bytes_to_send;
    int min_num_pre_trigger_bytes;
    int min_num_post_trigger_bytes; // The trigger sample itself is included in this

    // sequence number. 0 for the first set of samples, 1 for the next etc
    // only used in continuous mode
    uint32_t seq;

    // same format as channel mask passed to adc_set_round_robin()
    uint8_t enabled_channels;
    uint8_t num_enabled_channels;
    struct scoppy_channel channels[MAX_CHANNELS];

    // trigger stuff
    uint8_t trigger_mode; // off/normal/single
    uint8_t trigger_channel; // channel id in scope mode, a mask of channels in logic mode
    uint8_t trigger_type; // eg. rising edge, falling edge

    // run mode - we need to know what run mode was used to get the last
    // samples eg. single shot mode can last a while so the scoppy.app.run_mode might have changed in the meantime
    uint8_t run_mode;

    // logic or scope
    bool is_logic_mode;

    void (*get_samples)(struct scoppy_context *ctx);

    // for debugging
    uint32_t post;
};

extern struct sampling_params *active_params;
extern struct sampling_params *dormant_params;

void pico_scoppy_init_samplers();
void pico_scoppy_sampling_loop();
bool pico_scoppy_is_sampler_restart_required();
void pico_scoppy_get_null_samples(struct scoppy_context *ctx);

inline void pico_scoppy_check_params(const char *label, struct sampling_params *params) {
    if (params->get_samples == 0) {
        printf("%s - sampling_params get_samples is null\n", label);
        sleep_ms(2000);
        assert(false);
    }

    if (params->pre != SAMPLING_PARAMS_PRE) {
        printf("%s - sampling_params buffer underrun\n", label);
        sleep_ms(2000);
        assert(false);
    }

    if(params->post != SAMPLING_PARAMS_POST) {
        printf("%s - sampling_params buffer overrun\n", label);
        sleep_ms(2000);
        assert(false);
    }
}

#ifndef NDEBUG
    #define CHECK_SAMPLING_PARAMS(a, b) pico_scoppy_check_params(a, b)
#else
#define CHECK_SAMPLING_PARAMS(a, b)
#endif