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

#ifndef __SCOPPY_INCOMING_H__
#define __SCOPPY_INCOMING_H__

#include <stdbool.h>
#include <stdint.h>

#define SCOPPY_INCOMING_ERROR 0
#define SCOPPY_INCOMING_COMPLETE 1
#define SCOPPY_INCOMING_INCOMPLETE 2

// I can't image a message from the app to the frontend will ever be this big
#define SCOPPY_INCOMING_MAX_PAYLOAD_SIZE 512

extern const uint8_t scoppy_start_of_message_byte;
extern const uint8_t scoppy_end_of_message_byte;

struct scoppy_incoming {
    uint32_t pre;

    bool found_start_byte;
    bool found_end_byte;

    // skipped before we found the start byte
    // just keeping this value for debugging
    int16_t bytes_skipped; 
    
    // number of bytes read from start (including start byte)
    // once we've read the end byte this should be the same as msg_size
    int16_t bytes_read;

    // The size read from the message (the 2nd and 3rd bytes)
    int16_t msg_size;

    uint8_t msg_type;
    uint8_t msg_type_plus_5; // always has a value of msg_type + 5

    uint8_t msg_version;

    uint32_t pre_payload;

    // The raw message payload not including the start and end bytes, message size and message type
    uint8_t payload[SCOPPY_INCOMING_MAX_PAYLOAD_SIZE];

    uint32_t post_payload;

    // The length of the raw message data
    int16_t payload_len;

    // Flag to indicate if the message payload was parsed successfully
    bool payload_ok;

    uint32_t post;
};

void scoppy_init_incoming(struct scoppy_incoming *data);
void scoppy_prepare_incoming(struct scoppy_incoming *);

int scoppy_read_incoming(int (*read_serial)(uint8_t *, int, int), struct scoppy_incoming *data);
void scoppy_debug_incoming(struct scoppy_incoming *data);
char* scoppy_incoming_error();

#endif // __SCOPPY_INCOMING_H__