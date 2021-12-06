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

#include "scoppy-ring-buffer.h"
#include "scoppy-ring-buffer-test.h"
#include "scoppy-test.h"

static void ring_buffer_basic_test() {
    TPRINTF("ring_buffer_basic_test...");

    TPRINTF(" 1 ");
    uint8_t outer_arr[6];
    outer_arr[0] = 101;
    outer_arr[5] = 102;
    uint8_t *arr = outer_arr + 1;

    struct scoppy_uint8_ring_buffer ring;
    scoppy_uint8_ring_buffer_init(&ring, arr, 4);
    assert(ring.size(&ring) == 0);

    ring.put(&ring, 33);
    assert(ring.size(&ring) == 1);
    assert(arr[0] == 33);
    assert(ring.get(&ring) == 33);
    assert(ring.size(&ring) == 0);

    ring.put(&ring, 44);
    ring.put(&ring, 55);
    ring.put(&ring, 66);
    assert(arr[3] == 66);
    assert(ring.size(&ring) == 3);

    // wrap
    ring.put(&ring, 77);
    assert(ring.has_discarded_samples(&ring) == false);
    assert(ring.size(&ring) == 4);
    assert(arr[0] == 77);

    // buffer is full so write will discard data (ie the 44)
    assert(ring.is_full(&ring) == true);
    ring.put(&ring, 88);
    assert(ring.has_discarded_samples(&ring) == true);
    assert(ring.size(&ring) == 4);
    assert(ring.get(&ring) == 55);

    // Check for writes outside of the buffer
    assert(outer_arr[0] == 101);
    assert(outer_arr[5] == 102);

    TPRINTF(" OK\n");
}

static void ring_buffer_max_capacity_test() {
    // Test that the internal indexes in the ring buffer can rollover correctly
    TPRINTF("ring_buffer_max_capacity_test...");

    TPRINTF(" 1 ");
    scoppy_uint8_ring_buffer_set_idx_max(8);

    uint8_t outer_arr[6];
    outer_arr[0] = 101;
    outer_arr[5] = 102;
    uint8_t *arr = outer_arr + 1;
    struct scoppy_uint8_ring_buffer ring;
    scoppy_uint8_ring_buffer_init(&ring, arr, 4);
    assert(ring.size(&ring) == 0);

    int i;
    for (i = 0; i <= 12; i++) {
        ring.put(&ring, i);
    }

    // OK. There should now be 4 entries in the buffer, the first with a value of 9. The last with a value of 12
    // The internal indexes should have wrapped.
    assert(ring.read_idx <= 8);
    assert(ring.write_idx <= 8);

    assert(ring.is_full(&ring));
    assert(ring.get(&ring) == 9);
    assert(ring.get(&ring) == 10);
    assert(ring.get(&ring) == 11);
    assert(ring.get(&ring) == 12);
    assert(ring.is_empty(&ring));

    // Check for writes outside of the buffer
    assert(outer_arr[0] == 101);
    assert(outer_arr[5] == 102);

    TPRINTF(" OK\n");
}

static void ring_buffer_readall_test() {
    // Test that the internal indexes in the ring buffer can rollover correctly
    TPRINTF("ring_buffer_readall_test...");

    TPRINTF(" 1 ");
    scoppy_uint8_ring_buffer_set_idx_max(8);

    uint8_t outer_buf_arr[6];
    outer_buf_arr[0] = 101;
    outer_buf_arr[5] = 102;
    uint8_t *arr = outer_buf_arr + 1;
    struct scoppy_uint8_ring_buffer ring;
    scoppy_uint8_ring_buffer_init(&ring, arr, 4);
    assert(ring.size(&ring) == 0);

    // Destination array to read into
    uint8_t outer_dest_arr[6];
    outer_dest_arr[0] = 103;
    outer_dest_arr[5] = 104;
    uint8_t *dest_arr = outer_dest_arr + 1;

    ring.put(&ring, 54);
    assert(ring.read_all(&ring, dest_arr) == 1);
    assert(dest_arr[0] == 54);

    // fill array without wrapping
    ring.put(&ring, 55);
    ring.put(&ring, 56);
    ring.put(&ring, 57);
    ring.put(&ring, 58);
    assert(ring.size(&ring) == 4);
    assert(ring.is_full(&ring));
    assert(ring.read_all(&ring, dest_arr) == 4);
    assert(dest_arr[0] == 55);
    assert(dest_arr[3] == 58);

    // fill array and wrap 1 item
    ring.put(&ring, 59);
    ring.put(&ring, 60);
    ring.put(&ring, 61);
    ring.put(&ring, 62);
    ring.put(&ring, 63);
    assert(ring.size(&ring) == 4);
    assert(ring.is_full(&ring));
    assert(ring.read_all(&ring, dest_arr) == 4);
    assert(dest_arr[0] == 60);
    assert(dest_arr[3] == 63);

    // fill array and wrap 3 items
    ring.put(&ring, 64);
    ring.put(&ring, 65);
    ring.put(&ring, 66);
    ring.put(&ring, 67);
    ring.put(&ring, 68);
    ring.put(&ring, 69);
    ring.put(&ring, 70);
    assert(ring.size(&ring) == 4);
    assert(ring.is_full(&ring));
    assert(ring.read_all(&ring, dest_arr) == 4);
    assert(dest_arr[0] == 67);
    assert(dest_arr[3] == 70);

    // Large number of puts followed by readall
    assert(ring.is_empty(&ring));
    for (int i = 0; i <= 250; i++) {
        ring.put(&ring, i);
    }
    assert(ring.read_all(&ring, dest_arr) == 4);
    assert(dest_arr[0] == 247);
    assert(dest_arr[3] == 250);

    // Check for writes outside of the buffer array
    assert(outer_buf_arr[0] == 101);
    assert(outer_buf_arr[5] == 102);

    // Check for writes outside of the dest array
    assert(outer_dest_arr[0] == 103);
    assert(outer_dest_arr[5] == 104);

    TPRINTF(" OK\n");
}

void run_scoppy_ring_buffer_tests() {
    TPRINTF("run_scoppy_ring_buffer_tests...\n");
    ring_buffer_basic_test();
    ring_buffer_max_capacity_test();
    ring_buffer_readall_test();
}
