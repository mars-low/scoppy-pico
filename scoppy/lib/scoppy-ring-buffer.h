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

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

// https://stackoverflow.com/questions/9718116/improving-c-circular-buffer-efficiency
// https://www.snellman.net/blog/archive/2016-12-13-ring-buffers/
// Array + two unmasked indices


struct scoppy_uint8_ring_buffer {
    uint32_t id;
    uint32_t capacity;
    uint32_t mask;
    uint32_t read_idx;
    uint32_t write_idx;

    // keep track of whether samples were discarded because the buffer was full
    bool discarded_samples;

    uint8_t *arr;

    uint32_t (*get_id)(struct scoppy_uint8_ring_buffer *ring);
    uint32_t (*size)(struct scoppy_uint8_ring_buffer *ring);
    bool (*is_full)(struct scoppy_uint8_ring_buffer *ring);
    bool (*is_empty)(struct scoppy_uint8_ring_buffer *ring);

    void (*put)(struct scoppy_uint8_ring_buffer *ring, uint8_t val);
    uint8_t (*get)(struct scoppy_uint8_ring_buffer *ring);
    uint32_t (*read_all)(struct scoppy_uint8_ring_buffer *ring, uint8_t *dest);
    bool (*has_discarded_samples)(struct scoppy_uint8_ring_buffer *ring);
    void (*clear_discarded_flag)(struct scoppy_uint8_ring_buffer *ring);
};

#ifndef NDEBUG
void scoppy_uint8_ring_buffer_set_idx_max(uint32_t new_max_capacity);
#endif

void scoppy_uint8_ring_buffer_init(struct scoppy_uint8_ring_buffer *ring, uint8_t *buf, uint32_t capacity);