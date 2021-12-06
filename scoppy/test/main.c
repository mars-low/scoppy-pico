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

#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// for sleep
#include <unistd.h>

// my stuff
#include "simul.h"
#include "scoppy-util/md5.h"
#include "scoppy.h"
#include "scoppy-incoming-test.h"
#include "scoppy-message-test.h"
#include "scoppy-outgoing-test.h"
#include "scoppy-chunked-ring-buffer-test.h"
#include "scoppy-ring-buffer-test.h"

int main() {
    run_scoppy_incoming_test();
    run_scoppy_outgoing_test();
    run_scoppy_message_test();
    run_scoppy_ring_buffer_tests();
    run_scoppy_chunked_ring_buffer_tests();

    //run_scoppy_simulation();

    //md5_test("xreddy12344");

    return 0;
}
