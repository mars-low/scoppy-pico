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
#include <stdlib.h>
#include <string.h>

// For ADC input:
#include "hardware/adc.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "pico/stdlib.h"
#include "pico/time.h"

//
#include "scoppy-common.h"
#include "scoppy-message.h"
#include "scoppy-outgoing.h"
#include "scoppy-ring-buffer.h"
#include "scoppy.h"

//
#include "pico-scoppy-cont-sampling.h"
#include "pico-scoppy-samples.h"
#include "pico-scoppy-util.h"

static struct repeating_timer timer;
static struct repeating_timer *active_timer = NULL;

static uint8_t ring_buf1_arr[4098];
static struct scoppy_uint8_ring_buffer ring_buf1;
static uint8_t ring_buf2_arr[4098];
static struct scoppy_uint8_ring_buffer ring_buf2;

static struct scoppy_uint8_ring_buffer *active_buffer = &ring_buf1;
static struct scoppy_uint8_ring_buffer *dormant_buffer = &ring_buf2;

// Using a flag rather than a mutex/sem etc 'cos we're only running on the one
// core. This will probably have to change if we move to multicore
static volatile bool request_buffer_swap = false;

void pico_scoppy_continuous_sampling_init() {
    DEBUG_PRINT("  pico_scoppy_continuous_sampling_init()\n");

    // don't use the first and last bytes of the array. We can check these for
    // illegal writes outside of the buffer
    scoppy_uint8_ring_buffer_init(&ring_buf1, ring_buf1_arr + 1, sizeof(ring_buf1_arr) - 2);
    scoppy_uint8_ring_buffer_init(&ring_buf2, ring_buf2_arr + 1, sizeof(ring_buf2_arr) - 2);
}

void pico_scoppy_get_continuous_samples(struct scoppy_context *ctx) {
    //DEBUG_PRINT("get_continuous_samples()\n");

    // We need access to the buffer that is currently being used.
    // However the timer callback might be called anytime in this method so we
    // let the callback swap the buffers
    assert(active_timer != NULL);
    request_buffer_swap = true;

    //
    // Wait for the next timer callback to swap the buffers
    // Once swapped, we read from the dormant_buffer (the previously active buffer)
    //

    while (request_buffer_swap)
        ;

    if (request_buffer_swap) {
        assert(0);
        request_buffer_swap = false;
        return;
    }

    struct scoppy_uint8_ring_buffer *buf = dormant_buffer;
    //DEBUG_PRINT("  buffer(%lu): size=%lu\n", (unsigned long)buf->get_id(buf), (unsigned long)buf->size(buf));

    if (buf->size == 0) {
        return;
    }

    struct sampling_params *params = active_params;

    //DEBUG_PRINT("  seq=%lu, discarded=%d\n", (unsigned long)params->seq, (int)buf->has_discarded_samples(buf));
    bool is_new_wavepoint_record = params->seq++ == 0 || buf->discarded_samples;
    buf->clear_discarded_flag(buf);

    struct scoppy_outgoing *msg = scoppy_new_outgoing_samples_msg(
        params->realSampleRatePerChannel, active_params->channels, 
        is_new_wavepoint_record, false /* last message in frame */, true /* cont mode */, false /* single shot */,
        -1 /* trigger index - we didn't search for a trigger sample */, false /* is_logic_mode */);
    msg->payload_len += buf->read_all(buf, msg->payload + msg->payload_len);
    scoppy_write_outgoing(ctx->write_serial, msg);
}

void pico_scoppy_stop_continuous_sampling() {
    DEBUG_PRINT("  pico_scoppy_stop_continuous_sampling()\n");
    if (active_timer != NULL) {
        DEBUG_PRINT("    stopping adc read timer\n");
        cancel_repeating_timer(active_timer);
        active_timer = NULL;
    }
}

static bool adc_read_timer_callback(repeating_timer_t *rt) {

    if (active_params->enabled_channels & 0x00000001) {
        adc_select_input(0);
        active_buffer->put(active_buffer, (adc_read() >> 4) & 0x00FF);
    }

    if (active_params->enabled_channels & 0x00000002) {
        adc_select_input(1);
        active_buffer->put(active_buffer, (adc_read() >> 4) & 0x00FF);
    }

    if (request_buffer_swap) {
        // The get_samples() code wants to read the buffer
        struct scoppy_uint8_ring_buffer *tmp = dormant_buffer;
        dormant_buffer = active_buffer;
        active_buffer = tmp;

        // The buffer should have been emptied by the last get_samples()
        assert(active_buffer->is_empty(active_buffer));
        assert(active_buffer->has_discarded_samples(false));

        request_buffer_swap = false;
    }

    // keep repeating
    return true;
}

void pico_scoppy_start_continuous_sampling(struct scoppy_context *ctx) {
    DEBUG_PRINT("  pico_scoppy_start_continuous_sampling()\n");

    // Disable the adc fifo and interrupt. These aren't used during continuous mode.
    adc_fifo_setup(
        false, // Write each completed conversion to the sample FIFO
        false, // Enable DMA data request (DREQ)
        1,     // DREQ (and IRQ) asserted when at least 1 sample present
        false, // We won't see the ERR bit because of 8 bit reads; disable.
        true   // Shift each sample to 8 bits when pushing to FIFO
    );

    // negative timeout means exact delay (rather than delay between callbacks)
    int64_t delay_us = -1000000L / (signed long)active_params->realSampleRatePerChannel;

    assert(active_timer == NULL);
    active_timer = &timer;
    if (!add_repeating_timer_us(delay_us, adc_read_timer_callback, NULL, active_timer)) {
        ERROR_PRINT("Failed to add adc read timer\n");
        active_timer = NULL;

        // TODO: call fatal error handler?
        return;
    }

    DEBUG_PRINT("    added adc read timer with delay of %ldus\n", (long)delay_us);
}
