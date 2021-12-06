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


//
// A ring buffer where the client reserves a chunk to write to. Once the write is complete the client
// informs the buffer. Reserves and write completions must be in the same order. More than one chunk
// can be reserved at any time.
//
// This module is specifically designed for (possibly chained) DMA transfers.
//
// To make this more robust we could store a status byte with each chunk and with that we could
// check more thoroughly that the caller is doing the right thing (eg only unreserving reserved chunks)
//

struct scoppy_uint8_chunked_ring_buffer {

    // An id we can use when debugging so we know which buffer we are dealing with
    uint32_t id;
    uint32_t capacity;
    
    uint32_t num_chunks;
    uint32_t chunk_size;

    // The start of the underlying array
    uint8_t *arr;
    // The size of the underlying array
    uint32_t arr_size;
    // The last address of the array that we can write to (inclusive)
    uint8_t *arr_end;

    // The address of the next chunk that can be reserved
    uint8_t *next_chunk_addr;

    // Start of data that can be read
    uint8_t *start_addr;

    // End of data that can be read (inclusive) - NULL means the buffer is empty
    uint8_t *end_addr;

    void (*dump)(struct scoppy_uint8_chunked_ring_buffer *ring);

    uint32_t (*get_id)(struct scoppy_uint8_chunked_ring_buffer *ring);

    // The number of bytes available in the buffer
    uint32_t (*size)(struct scoppy_uint8_chunked_ring_buffer *ring);

    // distance of the given address from start of valid data
    int32_t (*index)(struct scoppy_uint8_chunked_ring_buffer *ring, uint8_t *addr);

    // Returns true if writing to the buffer will cause it it wrap
    //bool (*is_full)(struct scoppy_uint8_chunked_ring_buffer *ring);
    
    // remove all data from the buffer
    void (*clear)(struct scoppy_uint8_chunked_ring_buffer *ring);

    // is it empty?
    bool (*is_empty)(struct scoppy_uint8_chunked_ring_buffer *ring);

    void (*copy)(struct scoppy_uint8_chunked_ring_buffer *ring, struct scoppy_uint8_chunked_ring_buffer *to);

    // Reserve a chunk in the buffer that can be safely written to
    // If the chunk has previously been written to then the existing data is discarded
    // (ie the read_addr will be moved to the end of the buffer)
    // It is assumed that once a chunk is reserved it can be written to at any time
    uint8_t *(*reserve_chunk)(struct scoppy_uint8_chunked_ring_buffer *ring);

    // Writing to the chunk is complete
    void (*unreserve_chunk)(struct scoppy_uint8_chunked_ring_buffer *ring, uint8_t *chunk_addr);

    // Read all the data from the buffer
    uint32_t (*read_all)(struct scoppy_uint8_chunked_ring_buffer *ring, uint8_t *dest, uint32_t dest_size);

    // Read all data from a particular address in the buffer. The address maybe modified by an offset
    uint32_t (*read_from)(struct scoppy_uint8_chunked_ring_buffer *ring, uint8_t *src_addr, int32_t src_offset, uint8_t *dest, uint32_t max_bytes_to_copy);
    
    // Read all data from a particular address in the buffer. The address maybe modified by an offset
    int16_t (*read_byte)(struct scoppy_uint8_chunked_ring_buffer *ring, uint8_t *src_addr, int32_t src_offset);

    //uint32_t (*read_all)(struct scoppy_uint8_ring_buffer *ring, uint8_t *dest);
    //bool (*has_discarded_samples)(struct scoppy_uint8_chunked_ring_buffer *ring);
    //void (*clear_discarded_flag)(struct scoppy_uint8_chunked_ring_buffer *ring);
};

//#ifndef NDEBUG
//void scoppy_uint8_chunked_ring_buffer_set_idx_max(uint32_t new_max_capacity);
//#endif

void scoppy_uint8_chunked_ring_buffer_init(struct scoppy_uint8_chunked_ring_buffer *ring, uint8_t *arr, uint32_t arr_size, uint32_t chunk_size);