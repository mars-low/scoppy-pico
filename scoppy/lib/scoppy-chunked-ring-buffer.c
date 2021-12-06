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

//
#include "scoppy-chunked-ring-buffer.h"

#define MIN(X, Y) ((X) < (Y) ? (X) : (Y))
#define MAX(X, Y) ((X) > (Y) ? (X) : (Y))

#ifdef NDEBUG
#define ASSERT(expr) ((void)(0))
#define CHECK(expr) ((void)(0))
#else

static void dump_struct(struct scoppy_uint8_chunked_ring_buffer *ring) {
    //printf("\n");
    printf("arr            : 0x%lX\n", (unsigned long)ring->arr);
    if (ring->start_addr <= ring->end_addr) {
        printf("start_addr     : 0x%lX\n", (unsigned long)ring->start_addr);
        printf("end_addr       : 0x%lX\n", (unsigned long)ring->end_addr);
    } else {
        printf("end_addr       : 0x%lX\n...\n", (unsigned long)ring->end_addr);
        printf("start_addr     : 0x%lX\n", (unsigned long)ring->start_addr);
    }
    printf("arr_end        : 0x%lX\n", (unsigned long)ring->arr_end);
    printf("arr_size       : 0x%lu\n", (unsigned long)ring->arr_size);
    printf("chunk_size     : 0x%lu\n", (unsigned long)ring->chunk_size);
    printf("data size      : 0x%lu\n", (unsigned long)ring->size(ring));
    printf("next_chunk_addr: 0x%lX\n", (unsigned long)ring->next_chunk_addr);
}

/* This prints an "Assertion failed" message and aborts.  */
static void my_assert_fail(const char *__assertion, const char *__file,
                           unsigned int __line) {
    printf("assertion failed: %s %s %d\n", __assertion, __file, __line);
    abort();
}

#define ASSERT(expr) \
    ((expr)          \
         ? (void)(0) \
         : (dump_struct(ring), my_assert_fail(#expr, __FILE__, __LINE__)))

static void scoppy_uint8_chunked_ring_buffer_check(struct scoppy_uint8_chunked_ring_buffer *ring) {

    // next chunk address is always at the start of a chunk
    ASSERT(((ring->next_chunk_addr - ring->arr) % ring->chunk_size) == 0);

    if (ring->start_addr != NULL) {
        ASSERT(ring->end_addr != NULL);

        ASSERT(ring->start_addr <= ring->arr_end);

        // start address is always at the start of a chunk
        ASSERT(((ring->start_addr - ring->arr) % ring->chunk_size) == 0);
    } else {
        ASSERT(ring->end_addr == NULL);
    }

    if (ring->end_addr != NULL) {
        ASSERT(ring->start_addr != NULL);

        ASSERT(ring->end_addr <= ring->arr_end);

        // end address is always the last byte of a chunk
        ASSERT(((ring->end_addr + 1 - ring->arr) % ring->chunk_size) == 0);
    } else {
        ASSERT(ring->start_addr == NULL);
    }

    // There must be room to allocate the next chunk without it going past the end of the array
    ASSERT((ring->next_chunk_addr + ring->chunk_size) <= (ring->arr_end + 1));
}

#define CHECK(x) scoppy_uint8_chunked_ring_buffer_check(x)
#endif

static uint32_t scoppy_uint8_chunked_ring_buffer_size(struct scoppy_uint8_chunked_ring_buffer *ring) {
    if (ring->end_addr == NULL) {
        return 0;
    } else if (ring->end_addr >= ring->start_addr) {
        // Not wrapped
        return (ring->end_addr - ring->start_addr) + 1;
    } else {
        // wrapped
        uint32_t sz = (ring->arr_end - ring->start_addr) + 1;
        sz += (ring->end_addr - ring->arr) + 1;
        return sz;
    }
}

// distance from the start of valid data eg. start_addr
// returns -1 if not within valid data
static int32_t scoppy_uint8_chunked_ring_buffer_index(struct scoppy_uint8_chunked_ring_buffer *ring, uint8_t *addr) {
    // deliberately allowing NULL to be supplied as the addr
    if (addr == NULL) {
        return -1;
    }

    if (addr < ring->arr || addr > ring->arr_end) {
        //printf("scoppy_uint8_chunked_ring_buffer_index: addr=0x%lX\n", (unsigned long)addr);
        ASSERT(!"out of bounds");
        return -1;
    }

    if (ring->end_addr == NULL) {
        return -1;
    } else if (ring->end_addr >= ring->start_addr) {
        // Not wrapped
        if (addr < ring->start_addr || addr > ring->end_addr) {
            return -1;
        } else {
            return (addr - ring->start_addr);
        }
    } else {
        // wrapped
        if (addr >= ring->start_addr) {
            return addr - ring->start_addr;
        } else if (addr > ring->end_addr) {
            // we are between end_addr and start_addr
            return -1;
        } else {
            // we are between arr and end_addr
            return (ring->arr_end - ring->start_addr) + (addr - ring->arr) + 1;
        }
    }
}

void scoppy_uint8_chunked_ring_buffer_clear(struct scoppy_uint8_chunked_ring_buffer *ring) {
    ring->start_addr = NULL; // empty buffer
    ring->end_addr = NULL;   // empty buffer
    ring->next_chunk_addr = ring->arr;
    CHECK(ring);
}

// is it empty?
bool scoppy_uint8_chunked_ring_buffer_is_empty(struct scoppy_uint8_chunked_ring_buffer *ring) {
    if (ring->end_addr == NULL) {
        ASSERT(ring->start_addr == NULL);
        ASSERT(ring->size(ring) == 0);
        return true;
    } else {
        ASSERT(ring->start_addr != NULL);
        ASSERT(ring->size(ring) > 0);
        return false;
    }
}

static uint8_t *scoppy_uint8_chunked_ring_buffer_reserve_chunk(struct scoppy_uint8_chunked_ring_buffer *ring) {

    //
    // Note. We don't check that the caller is reserving more chunks than are available. The caller will just have to make sure they don't do it!.
    //

    uint8_t *this_chunk = ring->next_chunk_addr;

    ring->next_chunk_addr = this_chunk + ring->chunk_size;
    if (ring->next_chunk_addr > ring->arr_end) {
        ring->next_chunk_addr = ring->arr;
    }

    if (this_chunk == ring->start_addr) {
        ASSERT(ring->end_addr != NULL);

        // start_addr is now pointing to a (previously written to) chunk that will be overwritten by the caller so
        // we need to bump start address.
        // start_addr will be wrapped if required

        // First check if the end address is within the invalidated chunk. If so then the buffer will become empty.
        if (ring->end_addr > ring->start_addr && ring->end_addr < (ring->start_addr + ring->chunk_size)) {
            // This is probably not the way the app would want to empty the buffer. Probably indicates a bug.
            ring->start_addr = NULL;
            ring->end_addr = NULL;
        } else {
            ring->start_addr = ring->next_chunk_addr;
        }
    }

    CHECK(ring);
    return this_chunk;
}

static void scoppy_uint8_chunked_ring_buffer_unreserve_chunk(struct scoppy_uint8_chunked_ring_buffer *ring, uint8_t *chunk_addr) {

    if (ring->end_addr == NULL) {
        ASSERT(ring->start_addr == NULL);

        // If the buffer was empty then the start_addr has no real meaning so we need to set it here
        ring->start_addr = chunk_addr;
    }

    // The end address will now be the last byte of the chunk that has just been unreserved (and presumably written to!)
    ring->end_addr = (chunk_addr + ring->chunk_size) - 1;

    CHECK(ring);
}

// Read all data from a particular address in the buffer. The address maybe modified by an offset
uint32_t scoppy_uint8_chunked_ring_buffer_read_from(struct scoppy_uint8_chunked_ring_buffer *ring, uint8_t *src_addr, int32_t src_offset, uint8_t *dest, uint32_t max_bytes_to_copy) {
    ASSERT(max_bytes_to_copy >= 0);

    if (ring->end_addr == NULL) {
        // buffer is empty
        //ASSERT(ring->start_addr == NULL);
        return 0;
    }

    // Notes:
    // src_addr has to be in valid data ie. between start and end (and withing the buffer of course)
    // Once the src_addr has be adjusted for the offset it must also be in valid data

    if (src_addr < ring->arr || src_addr > ring->arr_end) {
        ASSERT(!"invalid src_addr param: outside buffer");
        return 0;
    }

    if (ring->end_addr >= ring->start_addr) {
        //
        // Not wrapped
        //

        // arr <= start_addr <= end_addr <= arr_end
        // valid data is from start_addr to end_addr

        if (src_addr < ring->start_addr || src_addr > ring->end_addr) {
            ASSERT(!"invalid src_addr param: outside non-wrapped data");
            return 0;
        }

        // Ajusting for the offset is easy here because we don't need to worry about wrapping
        src_addr += src_offset;

        if (src_addr < ring->start_addr || src_addr > ring->end_addr) {
            ASSERT(!"invalid src_offset param: outside non-wrapped data");
            return 0;
        }

        int data_size = (ring->end_addr - src_addr) + 1;
        int num_to_copy = MIN(data_size, max_bytes_to_copy);

        //printf("copying from: 0x%lX\n", (unsigned long)src_addr);
        memcpy(dest, src_addr, num_to_copy);
        return num_to_copy;
    } else {
        //
        // Wrapped
        //

        // arr <= end_addr < start_addr <= arr_end
        // valid data is from start_addr to arr_end and then arr to end_addr

        if (src_addr < ring->start_addr && src_addr > ring->end_addr) {
            ASSERT(!"invalid src_addr param: outside wrapped data");
            return 0;
        }

        // Apply the offset
        if (src_offset > 0) {
            if (src_addr <= ring->end_addr) {
                ASSERT(src_addr >= ring->arr && src_addr <= ring->end_addr);

                // easy. src_addr won't wrap after applying the (+ve) offset - but we need to check it isn't past
                // the end of valid data
                src_addr += src_offset;
                if (src_addr > ring->end_addr) {
                    ASSERT(!"invalid src_offset param: past wrapped data (1)");
                    return 0;
                }
            } else {
                ASSERT(src_addr >= ring->start_addr && src_addr <= ring->arr_end);

                int bytes_to_end_of_buffer = ring->arr_end - src_addr;
                if (src_offset <= bytes_to_end_of_buffer) {
                    src_addr += src_offset;
                    ASSERT(src_addr <= ring->arr_end);
                } else {
                    src_offset -= bytes_to_end_of_buffer;
                    ASSERT(src_offset > 0);

                    // if src_offset is now 1, src_addr should equal ring->addr (this why the -1 is in the next statement)
                    src_addr = ring->arr + src_offset - 1;
                    if (src_addr > ring->end_addr) {
                        ASSERT(!"invalid src_offset param: past wrapped data (2)");
                        return 0;
                    }
                }
            }
        } else if (src_offset < 0) {
            if (src_addr >= ring->start_addr) {
                ASSERT(src_addr >= ring->start_addr && src_addr <= ring->arr_end);

                // easy. src_addr won't wrap after applying the (-ve) offset
                src_addr += src_offset;
                if (src_addr < ring->start_addr) {
                    ASSERT(!"invalid src_offset param: before wrapped data (1)");
                    return 0;
                }
            } else {
                ASSERT(src_addr >= ring->arr && src_addr <= ring->end_addr);
                int bytes_to_start_of_buffer = ring->arr - src_addr; // NB. this will be -ve

                if (src_offset >= bytes_to_start_of_buffer) {
                    // eg. src_offset == -1 and bytes_to_start_of_buffer = -2
                    // src_addr won't wrap after applying the offset
                    src_addr += src_offset;
                    ASSERT(src_addr >= ring->arr);
                } else {
                    // eg -10 = -10 - -2 = -8
                    src_offset -= bytes_to_start_of_buffer;
                    ASSERT(src_offset < 0);

                    // if src_offset is now -1, src_addr should equal ring->arr_end - 0
                    // if src_offset is now -2, src_addr should equal ring->arr_end - 1
                    // if src_offset is now -3, src_addr should equal ring->arr_end - 2
                    // etc
                    src_addr = ring->arr_end + src_offset + 1;
                    if (src_addr < ring->start_addr) {
                        ASSERT(!"invalid src_offset param: before wrapped data (2)");
                        return 0;
                    }
                }
            }
        } else {
            // src_offset is 0
        }

        // same test as further above. If this fails then its a programming error in this function
        if (src_addr < ring->start_addr && src_addr > ring->end_addr) {
            ASSERT(!"invalid src_addr param: outside wrapped data (2)");
            return 0;
        }

        if (src_addr >= ring->arr && src_addr <= ring->end_addr) {
            int data_size = (ring->end_addr - src_addr) + 1;
            int num_to_copy = MIN(data_size, max_bytes_to_copy);
            memcpy(dest, src_addr, num_to_copy);
            return num_to_copy;
        } else {
            ASSERT(src_addr >= ring->start_addr && src_addr <= ring->arr_end);

            int data_size = (ring->arr_end - src_addr) + 1;
            int first_copy_size = MIN(data_size, max_bytes_to_copy);
            memcpy(dest, src_addr, first_copy_size);

            data_size = (ring->end_addr - ring->arr) + 1;
            int second_copy_size = MIN(data_size, max_bytes_to_copy - first_copy_size);
            if (second_copy_size > 0) {
                memcpy(dest + first_copy_size, ring->arr, second_copy_size);
            } else {
                second_copy_size = 0;
            }

            return first_copy_size + second_copy_size;
        }
    }

    ASSERT("WTF?");
    return 0;
}

int16_t scoppy_uint8_chunked_ring_buffer_read_byte(struct scoppy_uint8_chunked_ring_buffer *ring, uint8_t *src_addr, int32_t src_offset) {
    uint8_t byte;
    uint32_t count = scoppy_uint8_chunked_ring_buffer_read_from(ring, ring->start_addr, 0, &byte, 1);
    if (count == 0) {
        return -1;
    }

    return byte;
}

static uint32_t scoppy_uint8_chunked_ring_buffer_read_all(struct scoppy_uint8_chunked_ring_buffer *ring, uint8_t *dest, uint32_t max_bytes_to_copy) {
    return scoppy_uint8_chunked_ring_buffer_read_from(ring, ring->start_addr, 0, dest, max_bytes_to_copy);
}

// static uint32_t scoppy_uint8_chunked_ring_buffer_read_allx(struct scoppy_uint8_chunked_ring_buffer *ring, uint8_t *dest, uint32_t dest_size) {
//     if (ring->end_addr == NULL) {
//         ASSERT(ring->start_addr == NULL);
//         return 0;
//     }

//     if (ring->end_addr >= ring->start_addr) {
//         int data_size = (ring->end_addr - ring->start_addr) + 1;
//         int num_to_copy = MIN(data_size, dest_size);
//         memcpy(dest, ring->start_addr, num_to_copy);
//         return num_to_copy;
//     } else {
//         int data_size = (ring->arr_end - ring->start_addr) + 1;
//         int first_copy_size = MIN(data_size, dest_size);
//         memcpy(dest, ring->start_addr, first_copy_size);

//         data_size = (ring->end_addr - ring->arr) + 1;
//         int second_copy_size = MIN(data_size, dest_size - first_copy_size);
//         if (second_copy_size > 0) {
//             memcpy(dest + first_copy_size, ring->arr, second_copy_size);
//         } else {
//             second_copy_size = 0;
//         }

//         return first_copy_size + second_copy_size;
//     }

//     return 0;
// }

uint32_t scoppy_uint8_chunked_ring_buffer_id(struct scoppy_uint8_chunked_ring_buffer *ring) {
    return ring->id;
}

void scoppy_uint8_chunked_ring_buffer_copy(struct scoppy_uint8_chunked_ring_buffer *ring, struct scoppy_uint8_chunked_ring_buffer *to) {
    // An id we can use when debugging so we know which buffer we are dealing with
    to->id = ring->id;
    to->capacity = ring->capacity;

    to->num_chunks = ring->num_chunks;
    to->chunk_size = ring->chunk_size;

    // The start of the underlying array
    to->arr = ring->arr;
    // The size of the underlying array
    to->arr_size = ring->arr_size;
    // The last address of the array that we can write to (inclusive)
    to->arr_end = ring->arr_end;

    // The address of the next chunk that can be reserved
    to->next_chunk_addr = ring->next_chunk_addr;

    // Start of data that can be read
    to->start_addr = ring->start_addr;

    // End of data that can be read (inclusive) - NULL means the buffer is empty
    to->end_addr = ring->end_addr;

    to->dump = ring->dump;

    to->get_id = ring->get_id;

    // The number of bytes available in the buffer
    to->size = ring->size;

    // distance of the given address from start of valid data
    to->index = ring->index;

    // remove all data from the buffer
    to->clear = ring->clear;

    // is it empty?
    to->is_empty = ring->is_empty;

    to->copy = ring->copy;

    // Reserve a chunk in the buffer that can be safely written to
    // If the chunk has previously been written to then the existing data is discarded
    // (ie the read_addr will be moved to the end of the buffer)
    // It is assumed that once a chunk is reserved it can be written to at any time
    to->reserve_chunk = ring->reserve_chunk;

    // Writing to the chunk is complete
    to->unreserve_chunk = ring->unreserve_chunk;

    // Read all the data from the buffer
    to->read_all = ring->read_all;

    // Read all data from a particular address in the buffer. The address maybe modified by an offset
    to->read_from = ring->read_from;

    // Read all data from a particular address in the buffer. The address maybe modified by an offset
    to->read_byte = ring->read_byte;
}

static uint32_t next_id = 0;
void scoppy_uint8_chunked_ring_buffer_init(struct scoppy_uint8_chunked_ring_buffer *ring, uint8_t *arr, uint32_t arr_size, uint32_t chunk_size) {

    ring->id = next_id++;
    ring->chunk_size = chunk_size;

    ring->arr = arr;
    ring->arr_size = arr_size;

    // Determine the last address we can write to (inclusive). Takes into account the fact that the array might not be able
    // to hold an integer number of chunks
    ring->num_chunks = arr_size / chunk_size;                  // drop the fractional part
    ring->arr_end = arr + (ring->num_chunks * chunk_size) - 1; // inclusive

    ring->start_addr = NULL; // empty buffer
    ring->end_addr = NULL;   // empty buffer
    ring->next_chunk_addr = arr;

#ifndef NDEBUG
    ring->dump = dump_struct;
#endif
    ring->get_id = scoppy_uint8_chunked_ring_buffer_id;
    ring->size = scoppy_uint8_chunked_ring_buffer_size;
    ring->index = scoppy_uint8_chunked_ring_buffer_index;
    ring->clear = scoppy_uint8_chunked_ring_buffer_clear;
    ring->is_empty = scoppy_uint8_chunked_ring_buffer_is_empty;
    ring->copy = scoppy_uint8_chunked_ring_buffer_copy;

    ring->reserve_chunk = scoppy_uint8_chunked_ring_buffer_reserve_chunk;
    ring->unreserve_chunk = scoppy_uint8_chunked_ring_buffer_unreserve_chunk;

    // ring->put = scoppy_uint8_chunked_ring_buffer_put;
    // ring->get = scoppy_uint8_chunked_ring_buffer_get;
    ring->read_from = scoppy_uint8_chunked_ring_buffer_read_from;
    ring->read_all = scoppy_uint8_chunked_ring_buffer_read_all;
    ring->read_byte = scoppy_uint8_chunked_ring_buffer_read_byte;
    // ring->has_discarded_samples = scoppy_uint8_chunked_ring_buffer_has_discarded_samples;
    // ring->clear_discarded_flag = scoppy_uint8_chunked_ring_buffer_clear_discarded_flag;

    CHECK(ring);
}
