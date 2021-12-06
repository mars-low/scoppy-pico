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

#include <string.h>

//
#include "scoppy-ring-buffer.h"

// Must be a power of 2
// eg. 2^28 = 268435456
#ifdef NDEBUG
static const uint32_t IDX_MAX = 268435456UL;
#else
static uint32_t IDX_MAX = 268435456UL;
#endif

#ifndef NDEBUG
void scoppy_uint8_ring_buffer_set_idx_max(uint32_t new_max_capacity) {
    IDX_MAX = new_max_capacity;
}
#endif

// static  void scoppy_uint8_ring_buffer_clear(struct scoppy_uint8_ring_buffer *ring) {
//     ring->write_idx = ring->read_idx = 0;
// }

static bool scoppy_uint8_ring_buffer_is_empty(struct scoppy_uint8_ring_buffer *ring) {
    return ring->read_idx == ring->write_idx;
}

static uint32_t scoppy_uint8_ring_buffer_size(struct scoppy_uint8_ring_buffer *ring) {
    return ring->write_idx - ring->read_idx;
}

static bool scoppy_uint8_ring_buffer_is_full(struct scoppy_uint8_ring_buffer *ring) {
    return scoppy_uint8_ring_buffer_size(ring) >= ring->capacity;
}

static void scoppy_uint8_ring_buffer_trim_indexes(struct scoppy_uint8_ring_buffer *ring) {
    if (ring->read_idx > IDX_MAX && ring->write_idx > IDX_MAX) {
        // Manually wrap the indexes so we dont get to the situation where write_idx is 0 and
        // read_idx is MAX_UINT. In that case methods like scoppy_uint8_ring_buffer_size would fail miserably

#ifndef NDEBUG
        uint32_t read_buf_idx = ring->read_idx & ring->mask;
        uint32_t write_buf_idx = ring->write_idx & ring->mask;
#endif

        ring->read_idx -= IDX_MAX;
        ring->write_idx -= IDX_MAX;
        assert(ring->read_idx <= ring->write_idx);

#ifndef NDEBUG
        assert(read_buf_idx == (ring->read_idx & ring->mask));
        assert(write_buf_idx == (ring->write_idx & ring->mask));
#endif
    }
}

static uint8_t scoppy_uint8_ring_buffer_get(struct scoppy_uint8_ring_buffer *ring) {
    if (ring->read_idx == ring->write_idx) {
        assert(0);
        return 0;
    }

    uint32_t buf_idx = ring->read_idx & ring->mask;
    ring->read_idx++;
    assert(ring->read_idx <= ring->write_idx);

    scoppy_uint8_ring_buffer_trim_indexes(ring);

    return ring->arr[buf_idx];
}

static void scoppy_uint8_ring_buffer_put(struct scoppy_uint8_ring_buffer *ring, uint8_t val) {
    if (scoppy_uint8_ring_buffer_is_full(ring)) {
        // discard an entry
        scoppy_uint8_ring_buffer_get(ring);
        ring->discarded_samples = true;

        assert(!scoppy_uint8_ring_buffer_is_full(ring));
    }

    uint32_t buf_idx = ring->write_idx & ring->mask;
    ring->write_idx++;
    ring->arr[buf_idx] = val;
}

// The size of dest must be at least the current size of this buffer
static uint32_t scoppy_uint8_ring_buffer_read_all(struct scoppy_uint8_ring_buffer *ring, uint8_t *dest) {
    if (ring->read_idx == ring->write_idx) {
        // ring buffer is empty
        return 0;
    }

#ifndef NDEBUG
    uint32_t saved_size = scoppy_uint8_ring_buffer_size(ring);
#endif

    // Indexes into the actually array containing the data. If these values are equal it means
    // the buffer is full. These values are also equal if the buffer is empty but we have eliminated
    // that possibility by checking emptiness at the start of this function.
    uint32_t read_buf_idx = ring->read_idx & ring->mask;
    uint32_t write_buf_idx = ring->write_idx & ring->mask;

    uint32_t count;
    if (write_buf_idx > read_buf_idx) {
        count = write_buf_idx - read_buf_idx;
        memcpy(dest, ring->arr + read_buf_idx, count);
    } else {
        // Copy from the read index to the end of the array (up to capacity)
        int count1 = ring->capacity - read_buf_idx;
        memcpy(dest, ring->arr + read_buf_idx, count1);

        // Copy from the start of the array up until the write idx (exclusive)
        int count2 = write_buf_idx;
        memcpy(dest + count1, ring->arr, count2);

        count = count1 + count2;
    }

    assert(count == saved_size);

    // ring buffer is now empty
    ring->write_idx = ring->read_idx = 0;

    return count;
}

bool scoppy_uint8_ring_buffer_has_discarded_samples(struct scoppy_uint8_ring_buffer *ring) {
    return ring->discarded_samples;
}

void scoppy_uint8_ring_buffer_clear_discarded_flag(struct scoppy_uint8_ring_buffer *ring) {
    ring->discarded_samples = false;
}

uint32_t scoppy_uint8_ring_buffer_id(struct scoppy_uint8_ring_buffer *ring) {
    return ring->id;
}

static uint32_t next_id = 0;
void scoppy_uint8_ring_buffer_init(struct scoppy_uint8_ring_buffer *ring, uint8_t *buf, uint32_t capacity) {
    ring->id = next_id++;
    ring->read_idx = ring->write_idx = 0;
    ring->capacity = capacity;
    ring->mask = capacity - 1;
    ring->arr = buf;
    ring->discarded_samples = false;

    // The capacity must always be a power of two for this method to work
    // Hmmm. This doesn't actually check that it's a power of 2!
    assert(capacity % 2 == 0);
    assert(IDX_MAX % 2 == 0);

    // The maximum capacity can only be half the range of the index data types. (So 2^31-1 when using 32 bit unsigned integers)
    // But I'm using less to make it easier to safely wrap read_idx and write_idx
    assert(capacity <= IDX_MAX);

    ring->get_id = scoppy_uint8_ring_buffer_id;
    ring->size = scoppy_uint8_ring_buffer_size;
    ring->is_full = scoppy_uint8_ring_buffer_is_full;
    ring->is_empty = scoppy_uint8_ring_buffer_is_empty;
    ring->put = scoppy_uint8_ring_buffer_put;
    ring->get = scoppy_uint8_ring_buffer_get;
    ring->read_all = scoppy_uint8_ring_buffer_read_all;
    ring->has_discarded_samples = scoppy_uint8_ring_buffer_has_discarded_samples;
    ring->clear_discarded_flag = scoppy_uint8_ring_buffer_clear_discarded_flag;
}
