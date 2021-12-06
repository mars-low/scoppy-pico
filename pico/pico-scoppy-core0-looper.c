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
#include "pico-scoppy-core0-looper.h"
#include "pico-scoppy-non-cont-sampling.h"
#include "pico-scoppy-samples.h"
#include "pico-scoppy-util.h"
#include "pico-scoppy.h"

static void calculate_clkdiv_and_real_sample_rate_for_adc(struct sampling_params *params) {
    DEBUG_PRINT("  preferred SR: %lu\n", (unsigned long)params->preferredSampleRatePerChannelHz);
    // sleep_ms(50);

    // From the docs:
    // Divisor of 0 -> full speed. Free-running capture with the divider is
    // equivalent to pressing the ADC_CS_START_ONCE button once per `div + 1`
    // cycles (div not necessarily an integer). Each conversion takes 96
    // cycles, so in general you want a divider of 0 (hold down the button
    // continuously) or > 95 (take samples less frequently than 96 cycle
    // intervals). This is all timed by the 48 MHz ADC clock.
    //
    // My notes:
    // div values under 95 are clamped to 95
    // div values greater than 2^16 are invalid
    //
    // sample rate = (48000000)/(div+1) = 500000/(div+1)
    // (div + 1) = 500000/SR
    // div = (48000000/SR) - 1
    //
    // div = 4999 = 100 S/s

    // Todo: take into account the number of channels
    uint8_t num_channels = params->num_enabled_channels;

    // Deliberately using an int here to ensure that DIV.FRAC is zero. I think a non-zero frac would cause
    // the period between samples to not be exactly the same
    params->clkdivint = (48000000 / (params->preferredSampleRatePerChannelHz * num_channels)) - 1;

    // Div.INT is only 2 bytes
    if (params->clkdivint > 63999) {
        params->clkdivint = 63999; // 750S/s
    } else if (params->clkdivint <= 95) {
        // Strange. A clkdivint of 95 gives a measured samplerate of 250kS/s (expected 500kS/s)
        // A clkdivint of 0 gives a measured samplerate of 500kS/s.
        // Further testing showed that values between 1 and 94 give unexpected results.
        // All tested values above 96 give expected results.
        params->clkdivint = 0;
    }

    if (params->clkdivint == 0) {
        params->realSampleRatePerChannel = 500000 / num_channels;
    } else {
        params->realSampleRatePerChannel = (48000000 / (params->clkdivint + 1)) / num_channels;
    }

    DEBUG_PRINT("  real SR: %lu\n", (unsigned long)params->realSampleRatePerChannel);
    // sleep_ms(50);
}

static void calculate_clkdiv_and_real_sample_rate_for_pio(struct sampling_params *params) {
    DEBUG_PRINT("calculate_clkdiv_and_real_sample_rate_for_pio()\n");
    uint32_t pio_cycles_per_sample = 1lu;
    uint32_t sys_clk_freq = clock_get_hz(clk_sys);
    // DEBUG_PRINT("  sys_clk_freq: %luHz\n", sys_clk_freq); // 125000000
    DEBUG_PRINT("  params->preferredSampleRatePerChannelHz: %lu\n", (unsigned long)params->preferredSampleRatePerChannelHz);
    params->clkdivint = sys_clk_freq / (params->preferredSampleRatePerChannelHz * pio_cycles_per_sample);
    // DEBUG_PRINT("  params->clkdivint: %lu\n", (unsigned long)params->clkdivint);

    // div = clk / (sr * cylclespersample)
    // div * sr * cps = clk
    // sr = clk / (div * cps)

    // Div.INT is only 2 bytes
    if (params->clkdivint > 63999u) {
        params->clkdivint = 63999u; // 750S/s
    } else {
        // Set a limit of 25MSPS (clkdiv=5) (may have to change the way we calculate this if we're overclocking)
        // 40MSPS (clkdiv=3) did not work
        // uint32_t min_clkdiv_int = sys_clk_freq / (22000000 * pio_cycles_per_sample);
        // increasing the chunk size might help
        uint32_t min_clkdiv_int = 5;
        if (params->clkdivint < min_clkdiv_int) {
            params->clkdivint = min_clkdiv_int;
        }

        if (params->clkdivint <= 1u) {
            params->clkdivint = 1u;
        }
    }

    params->realSampleRatePerChannel = sys_clk_freq / (params->clkdivint * pio_cycles_per_sample);

    DEBUG_PRINT("  real SR: %lu, pio clkdiv=%lu\n", params->realSampleRatePerChannel, params->clkdivint);
}

static void calculate_clkdiv_and_real_sample_rate(struct sampling_params *params) {
    if (scoppy.app.is_logic_mode) {
        calculate_clkdiv_and_real_sample_rate_for_pio(params);
    } else {
        calculate_clkdiv_and_real_sample_rate_for_adc(params);
    }
}

static bool update_sample_rate_params(struct sampling_params *params) {
    DEBUG_PRINT("update_sample_rate_params()\n");

    bool is_logic_mode = params->is_logic_mode;
    DEBUG_PRINT("    is_logic_mode=%d\n", is_logic_mode);
    uint8_t total_bytes_per_sample = is_logic_mode ? 1 : params->num_enabled_channels;

    // The number of samples that we can transfer per channel
    // For now assume 1 byte per sample
    int num_bytes = is_logic_mode ? (BYTES_TO_SEND_PER_CHANNEL * 2) : BYTES_TO_SEND_PER_CHANNEL;

    // Calculate the sample rate that will span twice the timebase.
    // N.B. The trace flickers a lot if the span is too close to the timebase
    uint64_t sr_per_channel = num_bytes * 1000000000000L / scoppy.app.timebasePs / (is_logic_mode ? 3 : 2);
    uint64_t total_sr = sr_per_channel * total_bytes_per_sample;

    bool cont_mode;

    if (scoppy.app.selectedSampleRate != 0) {
        // The user has selected a sample rate
        if (scoppy.app.run_mode == RUN_MODE_SINGLE) {
            num_bytes = SINGLE_SHOT_TOTAL_BYTES_TO_SEND / total_bytes_per_sample;
        }

        total_sr = scoppy.app.selectedSampleRate * total_bytes_per_sample;

        if (is_logic_mode) {
            cont_mode = false;
        } else {
            cont_mode = scoppy.app.selectedSampleRate < 2000;
        }
    } else if (scoppy.app.run_mode == RUN_MODE_SINGLE) {
        // the sample rate that would result in 5 times screen coverage
        num_bytes = SINGLE_SHOT_TOTAL_BYTES_TO_SEND / total_bytes_per_sample;
        sr_per_channel = num_bytes * 1000000000000L / scoppy.app.timebasePs / 5;
        total_sr = sr_per_channel * total_bytes_per_sample;

        // if it will take longer than 10 seconds then calulcate the sample rate
        // that will span 10 seconds (and thus it will take approx 10 seconds to aquire the samples)
        if (num_bytes / sr_per_channel > 10) {
            sr_per_channel = num_bytes / 10;
            total_sr = sr_per_channel * total_bytes_per_sample;
        }

        cont_mode = false;
    } else if (scoppy.app.timebasePs >= 1000000000000L) {
        if (is_logic_mode) {
            cont_mode = false;
        } else {
            // Sample rate is slow enough that we can run in continous mode
            if (sr_per_channel > 2500) {
                total_sr = 5000 * total_bytes_per_sample;
            } else if (sr_per_channel > 1000) {
                total_sr = 2500 * total_bytes_per_sample;
            } else if (sr_per_channel > 500) {
                total_sr = 1000 * total_bytes_per_sample;
            } else if (sr_per_channel > 200) {
                total_sr = 400 * total_bytes_per_sample;
            } else if (sr_per_channel > 100) {
                total_sr = 200 * total_bytes_per_sample;
            } else if (sr_per_channel > 50) {
                total_sr = 100 * total_bytes_per_sample;
            } else if (sr_per_channel > 20) {
                total_sr = 40 * total_bytes_per_sample;
            } else if (sr_per_channel > 10) {
                total_sr = 20 * total_bytes_per_sample;
            } else if (sr_per_channel > 5) {
                total_sr = 10 * total_bytes_per_sample;
            } else {
                total_sr = 5 * total_bytes_per_sample;
            }
            cont_mode = true;
        }
    } else {
        if (is_logic_mode) {
            // Use a smarter way to calculate the ranges
        } else {
            if (total_sr > 400000) {
                total_sr = 500000;
            } else if (total_sr > 300000) {
                total_sr = 400000;
            } else if (total_sr > 250000) {
                total_sr = 300000;
            } else if (total_sr > 200000) {
                total_sr = 250000;
            } else if (total_sr > 150000) {
                total_sr = 200000;
            } else if (total_sr > 125000) {
                total_sr = 150000;
            } else if (total_sr > 100000) {
                total_sr = 125000;
            } else if (total_sr > 75000) {
                total_sr = 100000;
            }
        }

        cont_mode = false;
    }

    if (total_sr > 500000 && !is_logic_mode) {
        // adc limit
        total_sr = 500000;
    } else if (total_sr < total_bytes_per_sample) {
        total_sr = total_bytes_per_sample;
    }

    params->num_bytes_to_send = num_bytes * total_bytes_per_sample;
    params->min_num_pre_trigger_bytes = params->num_bytes_to_send * scoppy.app.preTriggerSamples / 100; // preTriggerSamples is a percentage eg. 50
    params->min_num_post_trigger_bytes = params->num_bytes_to_send - params->min_num_pre_trigger_bytes; // The trigger sample itself is included in this

    params->preferredSampleRatePerChannelHz = total_sr / total_bytes_per_sample;

    DEBUG_PRINT("  total_sr=%lu, num_bytes_to_send=%d\n", (unsigned long)total_sr, (int)params->num_bytes_to_send);

    return cont_mode;
}

static void consume_all_incoming_messages(struct scoppy_context *ctx) {
    int loopy = 0;
    while (loopy++ < 1000) {
        int ret = scoppy_read_and_process_incoming_message(ctx, 1, 0);
        if (ret == SCOPPY_INCOMING_COMPLETE) {
            // Prepare for next message
            scoppy_prepare_incoming(ctx->incoming);

            // Don't break out of loop. Keep consuming messages.
        } else {
            break;
        }
    }

    if (loopy >= 999) {
        assert(false);
    }
}

static bool restart_sampling_required = false;

// Returns true if the configuration has changed since we last restarted sampling
// This method is idempotent.
static bool aquisition_configuration_changed(struct scoppy_context *ctx) {
    // DEBUG_PRINT("aquisition_configuration_changed()\n");

    // Check for messages from the app. This might update the 'scoppy' structure. This must be called
    // frequently to consume messages from the app
    int ret = scoppy_read_and_process_incoming_message(ctx, 1, 0);
    if (ret == SCOPPY_INCOMING_COMPLETE) {
        // Prepare for next message
        scoppy_prepare_incoming(ctx->incoming);
    }

    if (!scoppy.channels_dirty && !scoppy.app.dirty) {
        // DEBUG_PRINT("  nothing changed\n");

        // config hasn't changed since we last checked
        return restart_sampling_required;
    }

    //
    // NB. The scoppy data structure can only be changed by the current core/thread
    // so we don't need to worry about it changing underneath us.
    // The active params might be read/written at any time by interrupt handlers.
    //

    //
    // We populate the dormant_params here. Once it's updated we change the value of a single
    // pointer to make it the active params. That way interrupt handlers or other threads won't
    // see the sampling_params data structure in a an inconsistent state.
    //

    if (scoppy.channels_dirty) {
        DEBUG_PRINT("  channel config changed\n");
        dormant_params->enabled_channels = 0;
        dormant_params->num_enabled_channels = 0;
        for (int i = 0; i < ARRAY_SIZE(scoppy.channels); i++) {
            dormant_params->channels[i] = scoppy.channels[i];
            if (dormant_params->channels[i].enabled) {
                dormant_params->enabled_channels |= (1U << i);
                dormant_params->num_enabled_channels++;
            }
        }

        DEBUG_PRINT("    num enabled=%u\n", (unsigned)dormant_params->num_enabled_channels);
    }
    scoppy.channels_dirty = false;

    if (scoppy.app.dirty) {
        DEBUG_PRINT("  app config changed\n");
        dormant_params->trigger_mode = scoppy.app.trigger_mode;
        dormant_params->trigger_channel = scoppy.app.trigger_channel;
        dormant_params->trigger_type = scoppy.app.trigger_type;
        dormant_params->run_mode = scoppy.app.run_mode;
        dormant_params->is_logic_mode = scoppy.app.is_logic_mode;

        if (dormant_params->run_mode == RUN_MODE_STOP) {
            DEBUG_PRINT("    run_mode==STOP\n");
            dormant_params->get_samples = pico_scoppy_get_null_samples;
        } else if (dormant_params->num_enabled_channels < 1) {
            dormant_params->get_samples = pico_scoppy_get_null_samples;
            DEBUG_PRINT("    zero enabled channels\n");
        } else {
            bool cont_mode = update_sample_rate_params(dormant_params);
            if (cont_mode) {
                dormant_params->get_samples = pico_scoppy_get_continuous_samples;
                dormant_params->realSampleRatePerChannel = dormant_params->preferredSampleRatePerChannelHz;
            } else {
                dormant_params->get_samples = pico_scoppy_get_non_continuous_samples;
                calculate_clkdiv_and_real_sample_rate(dormant_params);
            }
        }
    }
    scoppy.app.dirty = false;

    // Dormant params are the ones not currently in use by the aquisition 'engine' but are updated when there is a change in the app. eg timebase
    if (dormant_params->get_samples != active_params->get_samples) {
        DEBUG_PRINT("    sampling mode changed\n");
        restart_sampling_required = true;
        return true;
    }

    // this should really be checking the total sample rate
    if (active_params->run_mode != RUN_MODE_SINGLE) {
        if (dormant_params->realSampleRatePerChannel != active_params->realSampleRatePerChannel) {
            DEBUG_PRINT("    real sample rate changed\n");
            restart_sampling_required = true;
            return true;
        }
    }

    if (dormant_params->enabled_channels != active_params->enabled_channels) {
        DEBUG_PRINT("    enabled channels changed\n");
        restart_sampling_required = true;
        return true;
    }

    // In continuous mode triggering is done in the app so don't restart sampling if the trigger mode changes
    if (active_params->get_samples != pico_scoppy_get_continuous_samples) {
        if (dormant_params->trigger_mode != active_params->trigger_mode) {
            DEBUG_PRINT("    trigger mode changed\n");
            restart_sampling_required = true;
            return true;
        }
    }

    if (dormant_params->run_mode != active_params->run_mode) {
        DEBUG_PRINT("    run mode changed\n");
        restart_sampling_required = true;
        return true;
    }

    if (dormant_params->is_logic_mode != active_params->is_logic_mode) {
        DEBUG_PRINT("    is_logic_mode changed\n");
        restart_sampling_required = true;
        return true;
    }

    if (dormant_params->is_logic_mode) {
        if (dormant_params->trigger_channel != active_params->trigger_channel) {
            DEBUG_PRINT("    trigger channel changed (LA)\n");
            restart_sampling_required = true;
            return true;
        }

        if (dormant_params->trigger_type != active_params->trigger_type) {
            // In oscilloscope mode we don't need to restart sampling and so the trigger type needs to be read from
            // scoppy.app.trigger_type

            // We DO need to restart sampling in LA mode 'cos we need to use a different pio program
            DEBUG_PRINT("    trigger type changed (LA)\n");
            restart_sampling_required = true;
            return true;
        }
    }

    if (dormant_params->min_num_pre_trigger_bytes != active_params->min_num_pre_trigger_bytes) {
        DEBUG_PRINT("    min_num_pre_trigger_bytes changed\n");
        restart_sampling_required = true;
        return true;
    }

    if (restart_sampling_required) {
        DEBUG_PRINT(" aquisition params have not just changed but changed previously\n");
    } else {
        DEBUG_PRINT(" aquisition params have not changed - restart not required\n");
    }
    return restart_sampling_required;
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

void pico_scoppy_start_core0_loop(struct scoppy_context *ctx) {

    active_params->get_samples = pico_scoppy_get_null_samples;
    active_params->pre = SAMPLING_PARAMS_PRE;
    active_params->post = SAMPLING_PARAMS_POST;

    dormant_params->get_samples = pico_scoppy_get_null_samples;
    dormant_params->pre = SAMPLING_PARAMS_PRE;
    dormant_params->post = SAMPLING_PARAMS_POST;

    for (;;) {
        if (scoppy.app.resync_required) {
            scoppy.app.resync_required = false;

            // This is a signal to the caller that resyncing is required.
            return;
        }

        // Multiple messages might have come in quick succession eg. change to horz timebase
        // We only really want the last so read all pending messages from the app
        consume_all_incoming_messages(ctx);
        if (aquisition_configuration_changed(ctx)) {
            restart_sampling_required = false;

            // There should be plenty of room in the outgoing fifo
            if (!multicore_fifo_wready()) {
                assert(false);
                multicore_fifo_drain();
            }

            // There shouldn't be any messages waiting for us
            assert(!multicore_fifo_rvalid());

            // tell the sampler that a restart is required
            multicore_fifo_push_blocking(MULTICORE_MSG_RESTART_REQUIRED);

            // wait for sampling to stop
            uint32_t msg = multicore_fifo_pop_blocking();
            assert(msg == MULTICORE_MSG_SAMPLING_STOPPED);

            // Only now can we update active_params - knowing that the sampler is not accessing it
            struct sampling_params *tmp = active_params;
            active_params = dormant_params;
            dormant_params = tmp;
            // Make the values in the now dormant params the same as active params so that we can check if anything has changed
            *dormant_params = *active_params;

            CHECK_SAMPLING_PARAMS("core0-a", active_params);
            CHECK_SAMPLING_PARAMS("core0-d", dormant_params);

            // The sampler can start again
            multicore_fifo_push_blocking(MULTICORE_MSG_RESTART_SAMPLING);

            // There shouldn't be any messages waiting for us
            assert(!multicore_fifo_rvalid());
            // Outgoing fifo should have be empty or have one message in it at the most
            assert(multicore_fifo_wready());
        }
    }
}
