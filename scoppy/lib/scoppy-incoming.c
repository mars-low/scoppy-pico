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

// my stuff
#include "scoppy-incoming.h"

//static struct scoppy_incoming data_instance;
static char *last_error = "???";

// struct scoppy_incoming *scoppy_new_incoming_message() {
//     scoppy_init_incoming(&data_instance);
//     return &data_instance;
// }

// struct scoppy_incoming *scoppy_get_incoming_instance() {
//     return &data_instance;
// }

#define SCOPPY_INCOMING_PRE 0x5555 // 0101
#define SCOPPY_INCOMING_POST 0xAAAA // 1010

void scoppy_init_incoming(struct scoppy_incoming *data) {
    data->pre = SCOPPY_INCOMING_PRE;
    data->pre_payload = SCOPPY_INCOMING_PRE;

    data->post = SCOPPY_INCOMING_POST;
    data->post_payload = SCOPPY_INCOMING_POST;
}

#ifndef NDEBUG
static void check_incoming(struct scoppy_incoming *data) {
    char *msg = NULL;
    if (data->pre != SCOPPY_INCOMING_PRE) {
        msg = "scoppy_incoming - pre clobbered";
    }
    else if (data->pre_payload != SCOPPY_INCOMING_PRE) {
        msg = "scoppy_incoming - pre payload clobbered";
    }
    else    if (data->post != SCOPPY_INCOMING_POST) {
        msg = "scoppy_incoming - post clobbered";
    }
    else if (data->post_payload != SCOPPY_INCOMING_POST) {
        msg = "scoppy_incoming - post payload clobbered";
    }

    if (msg != NULL) {
        printf("%s\n", msg);

        // give printf time to be sent (no access to sleep_ms here!)
        int j = 0;
        for (int i = 0; i < 100000000; i++) {
            j += i;
        }
        assert(j == 0);
    }
}
    #define CHECK_INCOMING(a) check_incoming(a)
#else
#define CHECK_INCOMING(a)
#endif

void scoppy_prepare_incoming(struct scoppy_incoming *data) {
    data->found_start_byte = false;
    data->found_end_byte = false;
    data->bytes_read = 0;
    data->bytes_skipped = 0;
    data->msg_size = 0;
    data->msg_type = 0;
    data->msg_type_plus_5 = 0;
    data->msg_version = 0;
    data->payload_len = 0;
    data->payload_ok = false;
}

int scoppy_read_incoming(int (*read_serial)(uint8_t *, int, int), struct scoppy_incoming *data) {
    CHECK_INCOMING(data);
    uint8_t tmpBuf[1];
    if (!data->found_start_byte) {
        // keep reading a byte at a time until we find the 'start of message' byte
        int count;
        while ((count = read_serial(tmpBuf, 0, 1)) > 0) {
            if (tmpBuf[0] == scoppy_start_of_message_byte) {
                data->found_start_byte = true;
                data->bytes_read += count;
                break;
            } else {
                data->bytes_skipped += count;
            }
        }

        if (!data->found_start_byte) {
            CHECK_INCOMING(data);
            return SCOPPY_INCOMING_INCOMPLETE;
        }
    }

    if (data->msg_size == 0) {
        // https://hackaday.com/2020/08/04/dont-let-endianness-flip-you-around/
        // uint32_t be=(b[0]<<24)|(b[1]<<16)|(b[2]<<8)|b[3];

        assert(data->bytes_read == 1 || data->bytes_read == 2);
        if (data->bytes_read == 1) {
            // NB. We're just storing it temporarily in data->payload

            int count = read_serial(data->payload, 0, 1);
            assert(count <= 1);
            if (count < 1) {
                CHECK_INCOMING(data);
                return SCOPPY_INCOMING_INCOMPLETE;
            }
            data->bytes_read++;
        }

        assert(data->bytes_read == 2);

        // We already have one byte of the msg size...get the next
        int count = read_serial(data->payload, 1, 1);
        assert(count <= 1);
        if (count < 1) {
            CHECK_INCOMING(data);
            return SCOPPY_INCOMING_INCOMPLETE;
        }
        data->bytes_read++;

        // We have the start byte and the 2 message size bytes
        assert(data->bytes_read == 3);
        data->msg_size = (data->payload[0] << 8) | data->payload[1];

        if (data->msg_size < 5 || data->msg_size > SCOPPY_INCOMING_MAX_PAYLOAD_SIZE) {
            last_error = "Invalid message size";
            return SCOPPY_INCOMING_ERROR;
        }
    }

    if (data->msg_type == 0) {
        int count = read_serial(&data->msg_type, 0, 1);
        if (count < 1) {
            CHECK_INCOMING(data);
            return SCOPPY_INCOMING_INCOMPLETE;
        }
        data->bytes_read += count;

        if (data->msg_type == 0) {
            last_error = "Invalid message type";
            return SCOPPY_INCOMING_ERROR;
        }
    }

    if (data->msg_type_plus_5 == 0) {
        int count = read_serial(&data->msg_type_plus_5, 0, 1);
        if (count < 1) {
            CHECK_INCOMING(data);
            return SCOPPY_INCOMING_INCOMPLETE;
        }
        data->bytes_read += count;

        if (data->msg_type_plus_5 != data->msg_type + 5) {
            last_error = "Invalid message type checksum";
            scoppy_debug_incoming(data);
            return SCOPPY_INCOMING_ERROR;
        }
    }

    if (data->msg_version == 0) {
        int count = read_serial(&data->msg_version, 0, 1);
        if (count < 1) {
            CHECK_INCOMING(data);
            return SCOPPY_INCOMING_INCOMPLETE;
        }
        data->bytes_read += count;

        if (data->msg_version < 1) {
            last_error = "Invalid message version";
            scoppy_debug_incoming(data);
            return SCOPPY_INCOMING_ERROR;
        }
    }

    // Read enough chars to fill the buffer but no more
    int remaining = data->msg_size - data->bytes_read;
    if (remaining < 0 || remaining > SCOPPY_INCOMING_MAX_PAYLOAD_SIZE) {
        // msg size is wrong?
        last_error = "Wrong message size";
        return SCOPPY_INCOMING_ERROR;
    } else if (remaining > 0) {
        int count = read_serial(data->payload, data->payload_len, remaining);
        data->bytes_read += count;
        data->payload_len += count;
        if (data->bytes_read < data->msg_size) {
            CHECK_INCOMING(data);
            return SCOPPY_INCOMING_INCOMPLETE;
        }

        // for testing
        //return SCOPPY_INCOMING_INCOMPLETE;
    }

    assert(data->bytes_read == data->msg_size);

    if (!data->found_end_byte) {

        //
        // The buffer now has all the message data plus the 'end of message' byte
        //

        // There should be at least one byte ie. the 'end of message' byte
        if (data->payload_len < 1) {
            last_error = "payload_len too small";
            return SCOPPY_INCOMING_ERROR;
        }

        if (data->payload[data->payload_len - 1] != scoppy_end_of_message_byte) {
            //printf("last byte of message=%X\n", (unsigned int)data->payload[data->payload_len - 1]);
            last_error = "EOM byte not found.";
            return SCOPPY_INCOMING_ERROR;
        }

        // Don't include the 'end of message' byte in the payload length
        data->found_end_byte = 1;
        data->payload_len -= 1;
    }

    CHECK_INCOMING(data);
    return SCOPPY_INCOMING_COMPLETE;
}

void scoppy_debug_incoming(struct scoppy_incoming *data) {
    if (data->found_end_byte) {
        printf("Message Complete: payload_len=%d, ", data->payload_len);
        printf("msg_type=%d, msg_type2=%d, msg_size=%d, bytes_read=%d, found_end=%d, bytes_skipped=%d\n",
               data->msg_type, data->msg_type_plus_5, data->msg_size, data->bytes_read, data->found_end_byte, data->bytes_skipped);
    } else if (data->msg_type > 0) {
        printf("Message. msg_type=%d, msg_type2=%d, msg_size=%d, bytes_read=%d, found_end=%d, bytes_skipped=%d\n",
               data->msg_type, data->msg_type_plus_5, data->msg_size, data->bytes_read, data->found_end_byte, data->bytes_skipped);
        printf("payload: len=%d, data=", data->payload_len);
        for (int i = 0; i < 5; i++)
            printf("%d ", data->payload[i]);
        printf("\n");
    } else if (data->msg_size > 0) {
        printf("Message. msg_size=%d, bytes_read=%d, found_end=%d, bytes_skipped=%d\n",
               data->msg_size, data->bytes_read, data->found_end_byte, data->bytes_skipped);
    } else if (data->found_start_byte) {
        printf("Message. Found start byte but not message size. Num read=%d\n", data->bytes_read);
    } else {
        printf("Message. Start byte not found, skipped=%d\n", data->bytes_skipped);
    }

    printf("\n");
}

char *scoppy_incoming_error() {
    return last_error;
}