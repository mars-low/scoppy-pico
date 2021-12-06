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

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// my stuff

#include "scoppy-common.h"
#include "scoppy-incoming.h"
#include "scoppy-message.h"
#include "scoppy-outgoing.h"
#include "scoppy-stdio.h"
#include "scoppy.h"

// These must match the values used in the Scoppy app!
const uint8_t scoppy_start_of_message_byte = 255;
const uint8_t scoppy_end_of_message_byte = 86;

struct scoppy scoppy;

enum scoppy_state { STATE_UNSYNCED,
                    STATE_SYNCED };

static enum scoppy_state unsynced_state_handler(struct scoppy_context *ctx) {

    int send_delay = 0;
    for (;;) {
        ctx->set_status_led(true);

        // always create a 'new' message in case the buffer of the old was reused (there is only one instance of the msg object)
        CTX_DEBUG_PRINT(ctx, "Sending sync message\n");
        struct scoppy_outgoing *outgoing = scoppy_new_outgoing_sync_msg(ctx);
        scoppy_write_outgoing(ctx->write_serial, outgoing);
        // todo check for errors

        // Give the host time to get back to us and then try multiple times to get the response
        ctx->sleep_ms(200);

        CTX_DEBUG_PRINT(ctx, "Reading sync response\n");
        int ret = scoppy_read_and_process_incoming_message(ctx, 20 /* tries */, 50 /* pause between tries */);
        if (ret == SCOPPY_INCOMING_COMPLETE) {
            uint8_t msg_type = ctx->incoming->msg_type;
            bool payload_ok = ctx->incoming->payload_ok;

            // prepare for new message (this will clear the msg_type in incoming)
            scoppy_prepare_incoming(ctx->incoming);

            if (msg_type == SCOPPY_INCOMING_MSG_TYPE_SYNC_RESPONSE && payload_ok) {
                return STATE_SYNCED;
            }
        }

        ctx->set_status_led(false);

        // NB. We've already used up a second trying to read the response
        ctx->sleep_ms(200 * send_delay);
        if (send_delay > 10) {
            send_delay = 0;
        } else {
            send_delay++;
        }
    }

    // we should never get here!
    assert(false);
    return STATE_UNSYNCED;
}

static enum scoppy_state synced_state_handler(struct scoppy_context *ctx) {
    ctx->start_main_loop(ctx);
    return STATE_UNSYNCED;
}


void debug_print_state(struct scoppy_context *ctx, enum scoppy_state state) {
    if (state == STATE_UNSYNCED) {
        CTX_DEBUG_PRINT(ctx, "STATE=UNSYNCED\n");
    } else if (state == STATE_SYNCED) {
        CTX_DEBUG_PRINT(ctx, "STATE=SYNCED\n");
    } else {
        assert(false);
        CTX_DEBUG_PRINT(ctx, "STATE=???\n");
    }
}

static void init_scoppy() {
    int num_channels = ARRAY_SIZE(scoppy.channels);
    for (int i = 0; i < num_channels; i++) {
        scoppy.channels[i].enabled = false;
        scoppy.channels[i].voltage_range = 0;
    }

    scoppy.app.timebasePs = 1000000000; // 100 ms
    scoppy.app.preTriggerSamples = 50; // ie. 50%
    scoppy.app.is_logic_mode = false;
    scoppy.app.resync_required = false;
}

int scoppy_get_num_enabled_channels() {
    int num_enabled = 0;
    int num_channels = ARRAY_SIZE(scoppy.channels);
    for (int i = 0; i < num_channels; i++) {
        if (scoppy.channels[i].enabled) {
            num_enabled++;
        }
    }

    return num_enabled;
}

static enum scoppy_state state = STATE_UNSYNCED;
void scoppy_main(struct scoppy_context *ctx) {

    init_scoppy();

    struct scoppy_incoming incoming;
    scoppy_init_incoming(&incoming); // once off initialisation
    scoppy_prepare_incoming(&incoming);
    ctx->incoming = &incoming;

    scoppy_init_outgoing();

    for (;;) {
        switch (state) {
        case STATE_UNSYNCED:
            state = (unsynced_state_handler(ctx));
            debug_print_state(ctx, state);
            break;
        case STATE_SYNCED:
            state = (synced_state_handler(ctx));
            debug_print_state(ctx, state);
            break;
        default:
            assert(false);
            state = STATE_UNSYNCED;
            CTX_ERROR_PRINT(ctx, "wtf? s[115]");
            debug_print_state(ctx, state);
            break;
        }
    }
}