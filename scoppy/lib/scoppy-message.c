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

#include "stdio.h"
#include "string.h"
#include <assert.h>
//
#include "scoppy-common.h"
#include "scoppy-message.h"
#include "scoppy-outgoing.h"
#include "scoppy-stdio.h"
#include "scoppy-util/number.h"
#include "scoppy.h"

struct scoppy_outgoing *scoppy_new_outgoing_sync_msg(struct scoppy_context *ctx) {
    struct scoppy_outgoing *msg = scoppy_new_outgoing(SCOPPY_OUTGOING_MSG_TYPE_SYNC, 1);

    scoppy_uint32_to_4_network_bytes(msg->payload + msg->payload_len, ctx->chipId);
    msg->payload_len += 4;

    memcpy(msg->payload + msg->payload_len, ctx->uniqueId, 8);
    msg->payload_len += 8;

    msg->payload[msg->payload_len++] = ctx->firmware_type;
    msg->payload[msg->payload_len++] = ctx->firmware_version;

    scoppy_int32_to_4_network_bytes(msg->payload + msg->payload_len, ctx->build_number);
    msg->payload_len += 4;

    return msg;
}

struct scoppy_outgoing *scoppy_new_outgoing_samples_msg(uint32_t realSampleRateHz, struct scoppy_channel *channels, bool new_wavepoint_record,
                                                        bool is_last_message_in_frame, bool is_continuous_mode, bool is_single_shot, int32_t trigger_idx,
                                                        bool is_logic_mode) {
    struct scoppy_outgoing *msg = scoppy_new_outgoing(SCOPPY_OUTGOING_MSG_TYPE_SAMPLES, 1);

    // This flag tells the app that this a new wavepoint record or not ie. the samples don't continue on from the previous message
    int flags = (new_wavepoint_record ? 1 : 0);
    if (is_last_message_in_frame) {
        flags |= 0x02;
    }

    if (is_continuous_mode) {
        flags |= 0x04;
    }

    if (is_single_shot) {
        flags |= 0x08;
    }

    if (is_logic_mode) {
        flags |= 0x10;
    }
    assert(flags >= 0 && flags <= 255);

    msg->payload[msg->payload_len++] = (uint8_t)flags;

    // number of channels with data to send (goes here but well fill that in later)
    // flags also go here
    int num_data_channels_offset = msg->payload_len++;

    // 6:7 reserved
    // 4:5 voltage range
    // 3:3 reserved
    // 0:2 channel id
    int num_data_channels = 0;
    if (is_logic_mode) {
        // The app expects a these values for logic mode
        num_data_channels = 1;
        msg->payload[msg->payload_len++] = 0;
    } else {
        for (int ch_id = 0; ch_id < MAX_CHANNELS; ch_id++) {
            struct scoppy_channel *ch = &channels[ch_id];
            if (ch->enabled) {
                msg->payload[msg->payload_len++] = ((uint8_t)ch_id) | (ch->voltage_range << 4);
                num_data_channels++;
            }
        }
    }

    msg->payload[num_data_channels_offset] = num_data_channels;

    // Sample rate in Hz (int32)
    scoppy_uint32_to_4_network_bytes(msg->payload + msg->payload_len, realSampleRateHz);
    msg->payload_len += 4;

    // Trigger position (within a given channel)
    scoppy_int32_to_4_network_bytes(msg->payload + msg->payload_len, trigger_idx);
    msg->payload_len += 4;

    return msg;
}

static void update_channel_from_config_byte(struct scoppy_context *ctx, int channel_id, uint8_t config_byte) {
    if (channel_id >= ARRAY_SIZE(scoppy.channels) || channel_id < 0) {
        CTX_DEBUG_PRINT(ctx, "  Invalid channel id: %d\n", channel_id);
        return;
    }

    bool enabled = (config_byte & 0x01) ? true : false;
    if (scoppy.channels[channel_id].enabled != enabled) {
        // CTX_DEBUG_PRINT(ctx, "  Changing channel 'enabled' state\n");
        scoppy.channels[channel_id].enabled = enabled;
        scoppy.channels_dirty = true;
    }

    CTX_LOG_PRINT(ctx, "    CHID %d -> %s\n", channel_id, (enabled ? "ON" : "OFF"));
}

static int process_trigger_params(struct scoppy_context *ctx, int i) {
    struct scoppy_incoming *incoming = ctx->incoming;
    scoppy.app.trigger_mode = scoppy_uint8_from_1_network_byte(incoming->payload + i) & 0x00FF;
    if (scoppy.app.trigger_mode > TRIGGER_MODE_LAST) {
        CTX_ERROR_PRINT(ctx, "  invalid trigger mode: %d\n", (int)scoppy.app.trigger_mode);
        ctx->fatal_error_handler(SCOPPY_FATAL_ERROR_BAD_APP_PARAMS);
    }
    i += 1;

    scoppy.app.trigger_channel = scoppy_uint8_from_1_network_byte(incoming->payload + i) & 0x00FF;
    if (!scoppy.app.is_logic_mode) {
        if (scoppy.app.trigger_channel >= ARRAY_SIZE(scoppy.channels)) {
            CTX_ERROR_PRINT(ctx, "  invalid trigger_channel: %d\n", (int)scoppy.app.trigger_channel);
            ctx->fatal_error_handler(SCOPPY_FATAL_ERROR_BAD_APP_PARAMS);
        }
    }
    i += 1;

    scoppy.app.trigger_type = scoppy_uint8_from_1_network_byte(incoming->payload + i) & 0x00FF;
    if (scoppy.app.trigger_type > TRIGGER_TYPE_LAST) {
        CTX_ERROR_PRINT(ctx, "  invalid trigger type: %d\n", (int)scoppy.app.trigger_type);
        // ctx->fatal_error_handler(SCOPPY_FATAL_ERROR_BAD_APP_PARAMS);
        scoppy.app.trigger_type = TRIGGER_TYPE_RISING_EDGE;
    }
    i += 1;

    // The app sends an int16 but we only use uint8
    int16_t trigger_level = scoppy_int16_from_2_network_bytes(incoming->payload + i);
    if (trigger_level < 0) {
        CTX_ERROR_PRINT(ctx, "  invalid trigger level: %d\n", (int)trigger_level);
        trigger_level = 0;
    }

    if (trigger_level > 255) {
        CTX_ERROR_PRINT(ctx, "  invalid trigger level: %d\n", (int)trigger_level);
        trigger_level = 255;
        // The trigger can end up at the wrong level if the voltage level changes so don't make this a
        // fatal error (anymore)
        // ctx->fatal_error_handler(SCOPPY_FATAL_ERROR_BAD_APP_PARAMS);
    }
    scoppy.app.trigger_level = trigger_level;
    i += 2;

    CTX_LOG_PRINT(ctx, "  Trigger. mode=%u, ch=%u, type=%u, level=%u\n", (unsigned)scoppy.app.trigger_mode, (unsigned)scoppy.app.trigger_channel,
                  (unsigned)scoppy.app.trigger_type, (unsigned)scoppy.app.trigger_level);

    return i;
}

static void process_sync_response_message(struct scoppy_context *ctx) {
    CTX_DEBUG_PRINT(ctx, "Processing sync response message\n");
    struct scoppy_incoming *incoming = ctx->incoming;

    if (incoming->payload_len < 6) {
        CTX_DEBUG_PRINT(ctx, "  Payload too small: ignore this message\n");
        return;
    }

    int i = 0;
    uint8_t flags = incoming->payload[i++];
    CTX_DEBUG_PRINT(ctx, "  flags=%u\n", (unsigned)flags);

    // Lower 2 bits is the run mode
    scoppy.app.run_mode = flags & 0x3;
    if (scoppy.app.run_mode > 3) {
        CTX_ERROR_PRINT(ctx, "  unsupported run mode\n");
        ctx->fatal_error_handler(SCOPPY_FATAL_ERROR_BAD_APP_PARAMS);
        return;
    }
    CTX_DEBUG_PRINT(ctx, "  run_mode=%u\n", (unsigned)scoppy.app.run_mode);

    // Bits 3 and 4 are the app mode
    uint8_t app_mode = (flags >> 2) & 0x3;
    if (app_mode > 2) {
        CTX_ERROR_PRINT(ctx, "  unsupported app mode\n");
        ctx->fatal_error_handler(SCOPPY_FATAL_ERROR_BAD_APP_PARAMS);
        return;
    }
    CTX_DEBUG_PRINT(ctx, "  app_mode=%u\n", (unsigned)app_mode);

    bool is_logic_mode = app_mode > 0 ? true : false;
    if (is_logic_mode != scoppy.app.is_logic_mode) {
        scoppy.app.is_logic_mode = is_logic_mode;

        // NB. this is going to cause an extra sync if the app starts off in logic mode
        scoppy.app.resync_required = true;
    }

    // Next 4 bytes ununsed
    i += 4;

    uint8_t num_channels = incoming->payload[i++];
    if (num_channels == 0 || num_channels > 8) {
        CTX_ERROR_PRINT(ctx, "  invalid num channels: %d\n", (int)num_channels);
        ctx->fatal_error_handler(SCOPPY_FATAL_ERROR_BAD_APP_PARAMS);
        return;
    }

    for (int i_ch = 0; i_ch < num_channels; i_ch++) {
        update_channel_from_config_byte(ctx, i_ch, incoming->payload[i++]);
    }

    // Adjustments for voltage range expected at the ADC
    // Not used for now. See MessageFactory.java for more details

    // Lower voltage range offset
    i++;

    // Upper voltage range offset
    i++;

    // timebase is sent over the network as 100ths of a microsecond
    uint32_t timebase_centi_us = scoppy_uint32_from_4_network_bytes(incoming->payload + i);
    scoppy.app.timebasePs = ((uint64_t)timebase_centi_us) * 10000;
    i += 4;
    CTX_LOG_PRINT(ctx, "  timebase_centi_us=%lx\n", (unsigned long)timebase_centi_us);

#ifndef NDEBUG
    float timebase_ms = ((uint64_t)timebase_centi_us) / 100000;
    CTX_DEBUG_PRINT(ctx, "  Timebase=%lups %fms\n", (unsigned long)scoppy.app.timebasePs, (double)timebase_ms);
#endif

    i += process_trigger_params(ctx, i);

    incoming->payload_ok = true;

    scoppy.app.dirty = true;
    scoppy.channels_dirty = true;
}

static void process_horz_scale_changed_message(struct scoppy_context *ctx) {
    CTX_DEBUG_PRINT(ctx, "Processing horz scale changed message\n");
    struct scoppy_incoming *incoming = ctx->incoming;

    // timebase is sent over the network as 100ths of a microsecond
    int i = 0;
    uint32_t timebase_centi_us = scoppy_uint32_from_4_network_bytes(incoming->payload + i);
    scoppy.app.timebasePs = ((uint64_t)timebase_centi_us) * 10000;
    i += 4;
    // CTX_DEBUG_PRINT(ctx, "  Timebase=%lups (raw=%x)\n", scoppy.app.timebasePs, timebase_deci_us);
    CTX_LOG_PRINT(ctx, "  timebase_centi_us=%lx\n", (unsigned long)timebase_centi_us);

    scoppy.app.dirty = true;

    incoming->payload_ok = true;
}

static void process_channels_changed_message(struct scoppy_context *ctx) {
    CTX_DEBUG_PRINT(ctx, "Processing channel changed message\n");
    struct scoppy_incoming *incoming = ctx->incoming;

    int i = 0;
    uint8_t num_channels = incoming->payload[i++];
    if (num_channels == 0 || num_channels > 8) {
        CTX_DEBUG_PRINT(ctx, "  invalid num channels: %d\n", (int)num_channels);
        return;
    }

    for (int i_ch = 0; i_ch < num_channels; i_ch++) {
        update_channel_from_config_byte(ctx, i_ch, incoming->payload[i++]);
    }

    scoppy.app.dirty = true;

    incoming->payload_ok = true;
}

static void process_trigger_changed_message(struct scoppy_context *ctx) {
    CTX_DEBUG_PRINT(ctx, "Processing channel changed message\n");
    struct scoppy_incoming *incoming = ctx->incoming;

    process_trigger_params(ctx, 0);
    scoppy.app.dirty = true;
    incoming->payload_ok = true;
}

static void process_selected_sample_rate_message(struct scoppy_context *ctx) {
    CTX_DEBUG_PRINT(ctx, "Processing selected sample rate message\n");
    struct scoppy_incoming *incoming = ctx->incoming;

    int i = 0;
    scoppy.app.selectedSampleRate = scoppy_uint32_from_4_network_bytes(incoming->payload + i);
    i += 4;

    CTX_LOG_PRINT(ctx, "  selected sample rate=%lx\n", (unsigned long)scoppy.app.selectedSampleRate);

    scoppy.app.dirty = true;

    incoming->payload_ok = true;
}

static void process_pre_trigger_samples_message(struct scoppy_context *ctx) {
    CTX_DEBUG_PRINT(ctx, "Processing pre_trigger_samples_message\n");
    struct scoppy_incoming *incoming = ctx->incoming;

    int i = 0;
    scoppy.app.preTriggerSamples = scoppy_uint8_from_1_network_byte(incoming->payload + i);
    if (scoppy.app.preTriggerSamples > 100) {
        CTX_ERROR_PRINT(ctx, "  incorrect value for pre-trigger samples\n");
        ctx->fatal_error_handler(SCOPPY_FATAL_ERROR_BAD_APP_PARAMS);
        scoppy.app.preTriggerSamples = 100;
    }
    i++;

    // CTX_DEBUG_PRINT(ctx, "  Timebase=%lups (raw=%x)\n", scoppy.app.timebasePs, timebase_deci_us);
    CTX_LOG_PRINT(ctx, "  pre-trigger samples=%lx\n", (unsigned long)scoppy.app.preTriggerSamples);

    scoppy.app.dirty = true;

    incoming->payload_ok = true;
}

static void process_sig_gen_message(struct scoppy_context *ctx) {
    CTX_DEBUG_PRINT(ctx, "Processing sig. gen. message\n");
    struct scoppy_incoming *incoming = ctx->incoming;

    int i = 0;

    // function
    uint8_t func = scoppy_uint8_from_1_network_byte(incoming->payload + i);
    i += 1;

    // gpio
    uint8_t gpio = scoppy_uint8_from_1_network_byte(incoming->payload + i);
    i += 1;

    // freq
    uint32_t freq = scoppy_uint32_from_4_network_bytes(incoming->payload + i);
    i += 4;

    // duty
    // For now just use the lower byte
    uint16_t duty = scoppy_uint16_from_2_network_bytes(incoming->payload + i) & 0x00FF;
    i += 2;

    incoming->payload_ok = true;

    ctx->sig_gen(func, gpio, freq, duty);
}

static void process_complete_incoming_message(struct scoppy_context *ctx) {
    // CTX_DEBUG_PRINT(ctx, "process_complete_incoming_message\n");
    struct scoppy_incoming *incoming = ctx->incoming;
    if (incoming->msg_type == SCOPPY_INCOMING_MSG_TYPE_SYNC_RESPONSE) {
        process_sync_response_message(ctx);
    } else if (incoming->msg_type == SCOPPY_INCOMING_MSG_TYPE_HORZ_SCALE_CHANGED) {
        process_horz_scale_changed_message(ctx);
    } else if (incoming->msg_type == SCOPPY_INCOMING_MSG_TYPE_CHANNELS_CHANGED) {
        process_channels_changed_message(ctx);
    } else if (incoming->msg_type == SCOPPY_INCOMING_MSG_TYPE_TRIGGER_CHANGED) {
        process_trigger_changed_message(ctx);
    } else if (incoming->msg_type == SCOPPY_INCOMING_MSG_TYPE_SELECTED_SAMPLE_RATE) {
        process_selected_sample_rate_message(ctx);
    } else if (incoming->msg_type == SCOPPY_INCOMING_MSG_TYPE_PRE_TRIGGER_SAMPLES) {
        process_pre_trigger_samples_message(ctx);
    } else if (incoming->msg_type == SCOPPY_INCOMING_MSG_TYPE_SIG_GEN) {
        process_sig_gen_message(ctx);
    } else {
        CTX_LOG_PRINT(ctx, "Unknown message type %d - ignore\n", incoming->msg_type);
    }
}

int scoppy_read_and_process_incoming_message(struct scoppy_context *ctx, int num_tries, int32_t sleep_between_tries_ms) {
    int read_tries = 1;
    int ret;
    while ((ret = scoppy_read_incoming(ctx->read_serial, ctx->incoming)) == SCOPPY_INCOMING_INCOMPLETE && read_tries++ < num_tries) {
        // scoppy_debug_incoming(msg);
        ctx->sleep_ms(50);
    }

    if (ret == SCOPPY_INCOMING_COMPLETE) {
        CTX_DEBUG_PRINT(ctx, "Got incoming message\n");
        process_complete_incoming_message(ctx);
    } else if (ret == SCOPPY_INCOMING_ERROR) {
        CTX_ERROR_PRINT(ctx, "%s\n", scoppy_incoming_error());
#ifndef NDEBUG
        ctx->fatal_error_handler(SCOPPY_FATAL_ERROR_INCOMING_ERROR);
#endif
        scoppy_prepare_incoming(ctx->incoming);
    } else {
        // No incoming data or incomplete data.
    }

    return ret;
}
