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

#include <stdint.h>

//
#include "scoppy.h"
#include "scoppy-context.h"
#include "scoppy-incoming.h"
#include "scoppy-outgoing.h"

#define SCOPPY_OUTGOING_MSG_TYPE_SYNC 60
#define SCOPPY_OUTGOING_MSG_TYPE_SAMPLES 61

#define SCOPPY_OUTGOING_MAX_SAMPLE_BYTES (SCOPPY_OUTGOING_MAX_PAYLOAD_SIZE - 50)

#define SCOPPY_INCOMING_MSG_TYPE_SYNC_RESPONSE 80
#define SCOPPY_INCOMING_MSG_TYPE_HORZ_SCALE_CHANGED 81
#define SCOPPY_INCOMING_MSG_TYPE_CHANNELS_CHANGED 82
#define SCOPPY_INCOMING_MSG_TYPE_TRIGGER_CHANGED 83
#define SCOPPY_INCOMING_MSG_TYPE_SIG_GEN 84
#define SCOPPY_INCOMING_MSG_TYPE_SELECTED_SAMPLE_RATE 85
// 86 is the end of message byte
#define SCOPPY_INCOMING_MSG_TYPE_PRE_TRIGGER_SAMPLES 87

struct scoppy_outgoing *scoppy_new_outgoing_sync_msg(struct scoppy_context *ctx);
struct scoppy_outgoing *scoppy_new_outgoing_samples_msg(uint32_t realSampleRateHz, struct scoppy_channel *channels, bool new_wavepoint_record, bool is_last_message_in_frame, bool is_continuous_mode, bool is_single_shot, int32_t trigger_idx, bool is_logic_mode);

int scoppy_read_and_process_incoming_message(struct scoppy_context *ctx, int num_tries, int32_t sleep_between_tries_ms);
