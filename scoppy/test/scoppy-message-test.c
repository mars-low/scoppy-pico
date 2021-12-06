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
#include <stdint.h>

//
#include "scoppy-message.h"
#include "scoppy-message-test.h"
#include "scoppy-test.h"

void run_scoppy_message_test() {
    TPRINTF("scoppy_message_test...");

    struct scoppy_context ctx;
    ctx.chipId = 0x12345678;
    ctx.uniqueId[0] = 0xAA;
    ctx.uniqueId[7] = 0xEE;
    ctx.firmware_type = 1;
    ctx.firmware_version = 6;

    ctx.build_number = 0x71234589;
    struct scoppy_outgoing *msg = scoppy_new_outgoing_sync_msg(&ctx);

    TPRINTF(" 1 ");

    assert(msg->payload[0] == 0x12);
    assert(msg->payload[4] == 0xAA);
    assert(msg->payload[11] == 0xEE);
    assert(msg->payload[12] == 0x01);
    assert(msg->payload[13] == 0x06);

    // printf("\n payload[13]=%x\n", (unsigned int)msg->payload[13]);
    // printf(" payload[14]=%x\n", (unsigned int)msg->payload[14]);
    // printf(" payload[15]=%x\n", (unsigned int)msg->payload[15]);
    // printf(" payload[16]=%x\n", (unsigned int)msg->payload[16]);
    // printf(" payload[17]=%x\n", (unsigned int)msg->payload[17]);

    assert((msg->payload[14] & 0xFF) == 0x71); // msb of build number
    assert((msg->payload[15] & 0xFF) == 0x23); // 
    assert((msg->payload[16] & 0xFF) == 0x45); // 
    assert((msg->payload[17] & 0xFF) == 0x89); // lsb of build number

    printf(" OK\n");
}
