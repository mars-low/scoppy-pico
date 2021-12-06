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

#ifndef __SCOPPY_OUTGOING_H__
#define __SCOPPY_OUTGOING_H__

#include <stdbool.h>
#include <stdint.h>

#define SCOPPY_OUTGOING_ERROR 0
#define SCOPPY_OUTGOING_COMPLETE 1
#define SCOPPY_OUTGOING_INCOMPLETE 2


#define SCOPPY_OUTGOING_MAX_PAYLOAD_SIZE 4096

struct scoppy_outgoing {
    // for debugging
    uint32_t pre;

    uint8_t msg_type;

    uint8_t msg_version;

    // for debugging
    uint32_t pre_data;

    // The raw message data including the payload, the start and end bytes, message size and message type
    uint8_t data[SCOPPY_OUTGOING_MAX_PAYLOAD_SIZE + 5];

    // for debugging
    uint32_t post_data;

    // The address of the payload data
    uint8_t *payload;

    // The length of the payload data
    uint16_t payload_len;

    // The size of the complete message
    uint16_t msg_size;

    // for debugging
    uint32_t post;
};

void scoppy_init_outgoing();
struct scoppy_outgoing *scoppy_new_outgoing(uint8_t msg_type, uint8_t msg_version);

void scoppy_prepare_outgoing(struct scoppy_outgoing *msg);
int scoppy_write_outgoing(int (*write_serial)(uint8_t *, int, int), struct scoppy_outgoing *msg);
void scoppy_debug_outgoing(struct scoppy_outgoing *data);
char* scoppy_outgoing_error();

#endif // __SCOPPY_OUTGOING_H__