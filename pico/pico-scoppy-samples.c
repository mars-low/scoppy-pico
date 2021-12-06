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

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// For ADC input:
#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pico/time.h"

//
#include "scoppy-chunked-ring-buffer.h"
#include "scoppy-common.h"
#include "scoppy-message.h"
#include "scoppy-outgoing.h"
#include "scoppy-ring-buffer.h"
#include "scoppy.h"

#include "pico-scoppy-cont-sampling.h"
#include "pico-scoppy-non-cont-sampling.h"
#include "pico-scoppy-samples.h"
#include "pico-scoppy-util.h"
#include "pico-scoppy.h"

// Channel 0 is GPIO26

// Using a flag rather than a mutex/sem etc 'cos we're only running on the one
// core. This will probably have to change if we move to multicore
static volatile bool request_buffer_swap = false;

static struct sampling_params params1, params2;
struct sampling_params *active_params = &params1;
struct sampling_params *dormant_params = &params2;

void pico_scoppy_init_samplers() {

    // Init GPIO for analogue use: hi-Z, no pulls, disable digital input buffer.
    adc_gpio_init(26);
    adc_gpio_init(27);

    adc_init();

    pico_scoppy_continuous_sampling_init();
    pico_scoppy_non_continuous_sampling_init();
}

static void stop_sampling() {
    DEBUG_PRINT("stop_sampling()\n");
    pico_scoppy_stop_continuous_sampling();
    pico_scoppy_stop_non_continuous_sampling();

    adc_run(false);
    adc_fifo_drain();

    // We need to do this or else we get weird behaviour when changing the rrobin mask. If we don't then the following scenario happens:
    // both channels on: first byte is channel 0, second byte is channel 1 => OK
    // switch off ch 0: first byte is channel 1 => OK
    // switch on ch 0: first byte is channel 1, second byte is channel 0 => BAD!!! app shows channels swapped
    //
    // Have since found out that we can simply write a 0 to AINSEL
    // See: https://forums.raspberrypi.com/viewtopic.php?t=324786

    adc_init();
}

static void restart_sampling() {
    DEBUG_PRINT("restart_sampling()\n");

    // When we restart sampling it will be a new wavepoint record
    dormant_params->seq = 0;

    stop_sampling();

    // Activate the new sampling params
    struct sampling_params *tmp = active_params;
    active_params = dormant_params;
    dormant_params = tmp;

    // Make the values in the now dormant params the same as active params so that we can check if anything has changed
    *dormant_params = *active_params;

    if (active_params->get_samples == pico_scoppy_get_non_continuous_samples) {
        pico_scoppy_start_non_continuous_sampling();
    } else if (active_params->get_samples == pico_scoppy_get_continuous_samples) {
        pico_scoppy_start_continuous_sampling();
    } else {
        // sampling has stopped eg. no channels are configured
        DEBUG_PRINT("  not restarting sampling\n");
    }
}

static uint8_t get_voltage_range_id(int channel_id) {
    if (channel_id == 0) {
        return (gpio_get(VOLTAGE_RANGE_PIN_CH_0_BIT_1) ? 1u : 0u) << 1 | (gpio_get(VOLTAGE_RANGE_PIN_CH_0_BIT_0) ? 1u : 0u);
    } else if (channel_id == 1) {
        return (gpio_get(VOLTAGE_RANGE_PIN_CH_1_BIT_1) ? 1u : 0u) << 1 | (gpio_get(VOLTAGE_RANGE_PIN_CH_1_BIT_0) ? 1u : 0u);
    } else {
        return 0;
    }
}

static uint32_t last_multicore_msg = MULTICORE_MSG_NONE;

bool pico_scoppy_is_sampler_restart_required() {
    if (last_multicore_msg == MULTICORE_MSG_RESTART_REQUIRED) {
        return true;
    }

    if (multicore_fifo_rvalid()) {
        last_multicore_msg = multicore_fifo_pop_blocking();
        return (last_multicore_msg == MULTICORE_MSG_RESTART_REQUIRED);
    } else {
        return false;
    }
}

void pico_scoppy_get_null_samples(struct scoppy_context *ctx) { return; }

void pico_scoppy_sampling_loop() {
    DEBUG_PRINT("Entered sampling_loop() - core1\n");

    // core0 will provide the context pointer
    struct scoppy_context *ctx = (struct scoppy_context *)multicore_fifo_pop_blocking();

    // Determines the maximum number of frames per second
    //(or in continuous mode the frequency at which we send more samples to the app)
    static const int min_delay_time_us = 100 * 1000;
    absolute_time_t last_get_samples_time = get_absolute_time();

    // Wait for core0 to tell us we can start sampling
    DEBUG_PRINT(" core1: waiting for core0\n");
    last_multicore_msg = multicore_fifo_pop_blocking();
    DEBUG_PRINT(" core1: got start msg from core0\n");

    for (;;) {
        if (multicore_fifo_rvalid()) {
            // We shouldn't get a new message until we've dealt with the last one
            // NB. last_multicore_msg can be set in the get_samples code while waiting for a trigger

            assert(last_multicore_msg == MULTICORE_MSG_NONE);
            last_multicore_msg = multicore_fifo_pop_blocking();
        }

        if (last_multicore_msg == MULTICORE_MSG_RESTART_REQUIRED) {
            // core0 is waiting for us to stop sampling. We have, so tell it.
            if (!multicore_fifo_wready()) {
                assert(false);
                multicore_fifo_drain();
            }

            // DEBUG_PRINT(" core1: sending MULTICORE_MSG_SAMPLING_STOPPED\n");
            multicore_fifo_push_blocking(MULTICORE_MSG_SAMPLING_STOPPED);

            // Wait for core0 to update the active_params
            // DEBUG_PRINT(" core1: waiting for MULTICORE_MSG_RESTART_SAMPLING from core0\n");
            last_multicore_msg = multicore_fifo_pop_blocking();
            assert(last_multicore_msg == MULTICORE_MSG_RESTART_SAMPLING);
            CHECK_SAMPLING_PARAMS("core1-a-1", active_params);
            restart_sampling();
            last_multicore_msg = MULTICORE_MSG_NONE;
        }

        // Don't get samples too often. We don't want to overload the app.
        bool delay = true;
        while (delay) {
            absolute_time_t now = get_absolute_time();
            if (absolute_time_diff_us(last_get_samples_time, now) < min_delay_time_us) {
                // Delay some more
                sleep_us(1000);
            } else {
                // Time's up! Lets get some samples.
                delay = false;
            }
        }

        // update the currently selected voltage range
        for (int i = 0; i < MAX_CHANNELS; i++) {
            struct scoppy_channel *ch = &active_params->channels[i];
            if (ch->enabled) {
                ch->voltage_range = get_voltage_range_id(i);
            }
        }

        last_get_samples_time = get_absolute_time();
        CHECK_SAMPLING_PARAMS("core1-a-2", active_params);
        active_params->get_samples(ctx);
        CHECK_SAMPLING_PARAMS("core1-a-3", active_params);

        if (active_params->run_mode == RUN_MODE_SINGLE) {
            // HACK. Both cores might be readin/writing to this at the same time
            scoppy.app.run_mode = RUN_MODE_STOP;
            scoppy.app.dirty = true;
            active_params->get_samples = pico_scoppy_get_null_samples;
        }

        // if (ctx->is_testing) {
        //    DEBUG_PRINT("Done for now\n");
        //    exit(0);
        //} else {
        // for (;;) {
        //    ctx->sleep_ms(1000);
        //}
        //}
    }
}
