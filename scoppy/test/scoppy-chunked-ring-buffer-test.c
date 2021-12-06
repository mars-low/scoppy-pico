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
#include <string.h>

//
#include "scoppy-chunked-ring-buffer.h"
#include "scoppy-chunked-ring-buffer-test.h"
#include "scoppy-test.h"

static void chunked_ring_buffer_basic_test() {
    TPRINTF("chunked_ring_buffer_basic_test...");

    TPRINTF(" 1 ");
    uint8_t outer_arr[8];
    outer_arr[0] = 101;
    outer_arr[7] = 102;

    uint8_t *arr = outer_arr + 1;
    uint32_t arr_size = sizeof(outer_arr) - 2;

    //
    // This buffer has room for 3 chunks of 2
    //

    struct scoppy_uint8_chunked_ring_buffer ring;
    scoppy_uint8_chunked_ring_buffer_init(&ring, arr, arr_size, 2);
    //assert(ring.get_id(&ring) == 0);
    assert(ring.size(&ring) == 0);
    uint8_t *reserved1 = ring.reserve_chunk(&ring);
    assert(ring.size(&ring) == 0);
    uint8_t *reserved2 = ring.reserve_chunk(&ring);
    assert(ring.size(&ring) == 0);
    uint8_t *reserved3 = ring.reserve_chunk(&ring);
    assert(ring.size(&ring) == 0);

    //
    // This buffer has room for 3 chunks of 2
    //

    scoppy_uint8_chunked_ring_buffer_init(&ring, arr, arr_size, 2);
    //assert(ring.get_id(&ring) == 1);
    assert(ring.size(&ring) == 0);
    reserved1 = ring.reserve_chunk(&ring);
    ring.unreserve_chunk(&ring, reserved1);
    assert(ring.size(&ring) == 2);

    reserved2 = ring.reserve_chunk(&ring);
    ring.unreserve_chunk(&ring, reserved2);
    assert(ring.size(&ring) == 4);

    reserved3 = ring.reserve_chunk(&ring);
    ring.unreserve_chunk(&ring, reserved3);
    assert(ring.size(&ring) == 6);

    // data has now been 'written' to bytes 0-7
    // The next chunk to be reserved will be byte 0. This will invalidate
    // bytes 0-1 and so reduce the size
    assert(ring.start_addr == ring.arr);
    assert(ring.next_chunk_addr == ring.arr);
    assert(ring.end_addr == ring.end_addr);

    uint8_t *reserved_wrapped_1 = ring.reserve_chunk(&ring);
    assert(ring.size(&ring) == 4);
    assert(ring.start_addr > ring.arr); // start_addr has been bumped

    ring.reserve_chunk(&ring);
    assert(ring.size(&ring) == 2);

    ring.reserve_chunk(&ring);
    assert(ring.size(&ring) == 0);

    // check for buffer over/underrun
    assert(outer_arr[0] == 101);
    assert(outer_arr[7] == 102);

    TPRINTF(" OK\n");
}

static void chunked_ring_buffer_data_test() {
    TPRINTF("chunked_ring_buffer_data_test...");

    TPRINTF(" 1 ");
    uint8_t outer_arr[15];
    outer_arr[0] = 101;
    outer_arr[14] = 102;

    uint8_t *arr = outer_arr + 1;
    uint32_t arr_size = sizeof(outer_arr) - 2;
    uint32_t chunk_size = 3;

    //
    // This buffer has room for 4 chunks of 3 bytes
    // The array is not an integer multiple of chunk size
    //

    struct scoppy_uint8_chunked_ring_buffer ring;
    scoppy_uint8_chunked_ring_buffer_init(&ring, arr, arr_size, chunk_size);
    //assert(ring.get_id(&ring) == 2);
    assert(ring.size(&ring) == 0);
    assert(ring.num_chunks == 4);

    //
    // One chunk at a time
    //
    uint8_t dest[128];
    memset(dest, 99, sizeof(dest));

    //printf("\n");
    for (uint8_t i = 0; i < 100; i++) {
        //printf("i=%u\n", (unsigned)i);

        uint8_t *reserved1 = ring.reserve_chunk(&ring);
        memset(reserved1, i, chunk_size);
        ring.unreserve_chunk(&ring, reserved1);
        if (i < 3) {
            assert(ring.size(&ring) == chunk_size * (i + 1));
        } else {
            assert(ring.size(&ring) == ring.num_chunks * chunk_size);
        }

        int chunks_filled = ring.num_chunks; // eg 4
        if (i < ring.num_chunks) {
            chunks_filled = i + 1;
        }

        uint32_t num_copied = ring.read_all(&ring, dest, sizeof(dest));
        assert(num_copied == chunks_filled * ring.chunk_size);

        // check the data that has been written from the 4 chunks into dest
        for (int j = 0; j < chunks_filled; j++) {
            //printf("  j=%d\n", j);

            // the most recent 'chunk_size' bytes should have a value of i
            // the previous should have a value of i-1
            // etc etc

            // position in dest
            int dest_idx = j * chunk_size;

            int expected_value = i - ((chunks_filled - 1) - j);

            if (expected_value >= 0) {
                assert(dest_idx >= 0);

                for (int k = 0; k < chunk_size; k++) {
                    //printf("    k=%d\n", k);
                    uint8_t got_value = dest[dest_idx + k];

                    //printf("      dest_idx+k=%d, got_value=%u, expected=%d\n", dest_idx + k, (unsigned)got_value, expected_value);

                    if (got_value != expected_value) {
                        printf("unexpected value in dest. got %u, expected %d\n", (unsigned)dest[dest_idx + k], expected_value);
                        printf("  i=%u, j=%d, k=%d\n", (unsigned)i, j, k);
                        assert(false);
                    }
                }
            } else {
                assert(i < 3);
            }
        }
    }

    // reserving a chunk should reduce the size and thus the number of bytes copied
    ring.reserve_chunk(&ring);
    uint32_t num_copied = ring.read_all(&ring, dest, sizeof(dest));
    assert(num_copied == (ring.num_chunks - 1) * ring.chunk_size);

    assert(dest[12] == 99);

    // check for buffer over/underrun
    assert(outer_arr[0] == 101);
    assert(outer_arr[14] == 102);

    TPRINTF(" OK\n");
}

static void chunked_ring_buffer_dma_test() {
    // Test the ring buffer as it would be used by the chained dma channels
    TPRINTF("chunked_ring_buffer_dma_test...");

    TPRINTF(" 1 ");
    uint8_t outer_arr[6];
    outer_arr[0] = 101;
    outer_arr[5] = 102;

    uint8_t *arr = outer_arr + 1;
    uint32_t arr_size = sizeof(outer_arr) - 2;
    uint32_t chunk_size = 1;

    //
    // This buffer has room for 4 chunks of 1 byte
    // The array is not an integer multiple of chunk size
    //

    struct scoppy_uint8_chunked_ring_buffer ring;
    scoppy_uint8_chunked_ring_buffer_init(&ring, arr, arr_size, chunk_size);
    //assert(ring.get_id(&ring) == 2);
    assert(ring.size(&ring) == 0);
    assert(ring.num_chunks == 4);

    uint8_t dest[128];
    memset(dest, 99, sizeof(dest));

    // reserve space for each dma channel
    uint8_t *reserved1 = ring.reserve_chunk(&ring);
    uint8_t *reserved2 = ring.reserve_chunk(&ring);
    assert(ring.size(&ring) == 0);

    // Now alternate between each channel
    *reserved1 = 0;
    ring.unreserve_chunk(&ring, reserved1);
    assert(ring.size(&ring) == 1);
    assert(ring.read_all(&ring, dest, sizeof(dest)) == 1);
    reserved1 = ring.reserve_chunk(&ring);
    assert(ring.size(&ring) == 1);
    assert(ring.read_all(&ring, dest, sizeof(dest)) == 1);
    assert(dest[0] == 0);

    *reserved2 = 1;
    ring.unreserve_chunk(&ring, reserved2);
    assert(ring.size(&ring) == 2);
    reserved2 = ring.reserve_chunk(&ring);

    *reserved1 = 2;
    ring.unreserve_chunk(&ring, reserved1);
    assert(ring.size(&ring) == 3);
    reserved1 = ring.reserve_chunk(&ring);
    assert(ring.size(&ring) == 2); // 2 chunks of the 4 are reserved, leaving only 2 chunks of valid data
    assert(ring.read_all(&ring, dest, sizeof(dest)) == 2);
    assert(dest[0] == 1 && dest[1] == 2);

    *reserved2 = 3;
    ring.unreserve_chunk(&ring, reserved2);
    assert(ring.size(&ring) == 3);
    reserved2 = ring.reserve_chunk(&ring);
    assert(ring.size(&ring) == 2);
    assert(ring.read_all(&ring, dest, sizeof(dest)) == 2);
    assert(dest[0] == 2 && dest[1] == 3);

    *reserved1 = 4;
    ring.unreserve_chunk(&ring, reserved1);
    assert(ring.size(&ring) == 3);
    assert(ring.size(&ring) == 3);
    assert(ring.read_all(&ring, dest, sizeof(dest)) == 3);
    assert(dest[0] == 2 && dest[1] == 3 && dest[2] == 4);
    reserved1 = ring.reserve_chunk(&ring);
    assert(ring.size(&ring) == 2); // 2 chunks of the 4 are reserved, leaving only 2 chunks of valid data
    assert(ring.read_all(&ring, dest, sizeof(dest)) == 2);
    assert(dest[0] == 3 && dest[1] == 4);

    *reserved2 = 5;
    ring.unreserve_chunk(&ring, reserved2);
    assert(ring.size(&ring) == 3);
    assert(ring.read_all(&ring, dest, sizeof(dest)) == 3);
    assert(dest[0] == 3 && dest[1] == 4 && dest[2] == 5);
    reserved2 = ring.reserve_chunk(&ring);
    assert(ring.size(&ring) == 2);
    assert(ring.read_all(&ring, dest, sizeof(dest)) == 2);
    assert(dest[0] == 4 && dest[1] == 5);

    *reserved1 = 6;
    ring.unreserve_chunk(&ring, reserved1);
    reserved1 = ring.reserve_chunk(&ring);

    *reserved2 = 7;
    ring.unreserve_chunk(&ring, reserved2);
    assert(ring.size(&ring) == 3);
    assert(ring.read_all(&ring, dest, sizeof(dest)) == 3);
    assert(dest[0] == 5 && dest[1] == 6 && dest[2] == 7);
    reserved2 = ring.reserve_chunk(&ring);
    assert(ring.size(&ring) == 2); // 2 chunks of the 4 are reserved, leaving only 2 chunks of valid data
    assert(ring.read_all(&ring, dest, sizeof(dest)) == 2);
    assert(dest[0] == 6 && dest[1] == 7);

    *reserved1 = 8;
    ring.unreserve_chunk(&ring, reserved1);
    assert(ring.size(&ring) == 3);
    assert(ring.read_all(&ring, dest, sizeof(dest)) == 3);
    assert(dest[0] == 6 && dest[1] == 7 && dest[2] == 8);
    reserved1 = ring.reserve_chunk(&ring);
    assert(ring.size(&ring) == 2); // 2 chunks of the 4 are reserved, leaving only 2 chunks of valid data
    assert(ring.read_all(&ring, dest, sizeof(dest)) == 2);
    assert(dest[0] == 7 && dest[1] == 8);

    *reserved2 = 9;
    ring.unreserve_chunk(&ring, reserved2);
    assert(ring.size(&ring) == 3);

    *reserved1 = 10;
    ring.unreserve_chunk(&ring, reserved1);
    assert(ring.size(&ring) == 4);

    // zero reserved chunks
    assert(ring.read_all(&ring, dest, sizeof(dest)) == 4);
    assert(dest[0] == 7 && dest[1] == 8 && dest[2] == 9 && dest[3] == 10);

    // check for buffer over/underrun
    assert(dest[4] == 99);
    assert(outer_arr[0] == 101);
    assert(outer_arr[5] == 102);

    TPRINTF(" OK\n");
}

static void chunked_ring_buffer_read_from_non_wrapped_test() {
    TPRINTF("chunked_ring_buffer_read_from_non_wrapped_test...");

    TPRINTF(" 1 ");
    uint8_t outer_arr[14];
    outer_arr[0] = 101;
    outer_arr[13] = 102;

    uint8_t *arr = outer_arr + 1;
    uint32_t arr_size = sizeof(outer_arr) - 2;
    uint32_t chunk_size = 3;

    uint8_t dest[128];
    memset(dest, 99, sizeof(dest));

    //
    // This buffer has room for 4 chunks of 3 bytes
    //

    struct scoppy_uint8_chunked_ring_buffer ring;
    scoppy_uint8_chunked_ring_buffer_init(&ring, arr, arr_size, chunk_size);
    //assert(ring.get_id(&ring) == 2);
    assert(ring.num_chunks == 4);
    assert(outer_arr[13] == 102);

    // Arrange buffer so that first and last chunks don't contain valid data

    // This chunk will become invalid later
    uint8_t *reserved0 = ring.reserve_chunk(&ring);
    ring.unreserve_chunk(&ring, reserved0);

    // 1st chunk of valid data
    uint8_t *reserved1 = ring.reserve_chunk(&ring);
    reserved1[0] = 1;
    reserved1[1] = 2;
    reserved1[2] = 3;
    ring.unreserve_chunk(&ring, reserved1);

    // 2nd chunk of valid data
    uint8_t *reserved2 = ring.reserve_chunk(&ring);
    reserved2[0] = 4;
    reserved2[1] = 5;
    reserved2[2] = 6;
    ring.unreserve_chunk(&ring, reserved2);

    // reserve chunk after end_addr
    uint8_t *reserved3 = ring.reserve_chunk(&ring);
    // reserve another...this will be located at the start of the buffer
    uint8_t *reserved4 = ring.reserve_chunk(&ring);
    assert(reserved4 == ring.arr);
    assert(ring.end_addr == ring.arr + 8);
    assert(ring.start_addr > ring.arr);
    assert(ring.end_addr > ring.start_addr);
    assert(ring.size(&ring) == 6);

    assert(ring.read_from(&ring, reserved1, 0, dest, 1) == 1);
    assert(dest[0] == 1);

    assert(ring.read_from(&ring, reserved1, 1, dest, 1) == 1);
    //printf("%u\n", (unsigned)dest[0]);
    assert(dest[0] == 2);

    assert(ring.read_from(&ring, reserved1 + 1, -1, dest, 3) == 3);
    assert(dest[0] == 1);
    assert(dest[1] == 2);
    assert(dest[2] == 3);

    assert(ring.read_from(&ring, reserved2, -3, dest, 10) == 6); // offset takes us to the start of reserved1
    assert(dest[0] == 1);
    assert(dest[3] == 4);
    assert(dest[5] == 6);

    // test 'index'
    assert(ring.index(&ring, reserved0) == -1);     // invalid data chunk
    assert(ring.index(&ring, reserved0 + 2) == -1); // invalid data chunk
    assert(ring.index(&ring, reserved1) == 0);      // 1st valid data chunk
    assert(ring.index(&ring, reserved1 + 2) == 2);  // 1st valid data chunk
    assert(ring.index(&ring, reserved2) == 3);      // 2nd valid data chunk
    assert(ring.index(&ring, reserved2 + 2) == 5);  // 2nd valid data chunk
    assert(ring.index(&ring, reserved3) == -1);     // invalid data chunk
    assert(ring.index(&ring, reserved3 + 2) == -1); // invalid data chunk

    // check for buffer over/underrun
    assert(dest[10] == 99);
    assert(outer_arr[0] == 101);
    assert(outer_arr[13] == 102);

    TPRINTF(" OK\n");
}

static void chunked_ring_buffer_read_from_wrapped_test() {
    TPRINTF("chunked_ring_buffer_read_from_wrapped_test...");

    TPRINTF(" 1 ");
    uint8_t outer_arr[14];
    outer_arr[0] = 101;
    outer_arr[13] = 102;

    uint8_t *arr = outer_arr + 1;
    uint32_t arr_size = sizeof(outer_arr) - 2;
    uint32_t chunk_size = 3;

    uint8_t dest[128];
    memset(dest, 99, sizeof(dest));

    //
    // This buffer has room for 4 chunks of 3 bytes
    //

    struct scoppy_uint8_chunked_ring_buffer ring;
    scoppy_uint8_chunked_ring_buffer_init(&ring, arr, arr_size, chunk_size);
    //assert(ring.get_id(&ring) == 2);
    assert(ring.num_chunks == 4);
    assert(outer_arr[13] == 102);

    // Arrange buffer so that first and last chunks only contain valid data

    // First allocate all 4 chunks - start is at ring.arr, end is at ring.arr_end

    // These 3 will become invalid later when we reserve more chunks
    uint8_t *reserved_junk = ring.reserve_chunk(&ring);
    ring.unreserve_chunk(&ring, reserved_junk);
    reserved_junk = ring.reserve_chunk(&ring);
    ring.unreserve_chunk(&ring, reserved_junk);
    reserved_junk = ring.reserve_chunk(&ring);
    ring.unreserve_chunk(&ring, reserved_junk);

    uint8_t *reserved1 = ring.reserve_chunk(&ring);
    reserved1[0] = 1;
    reserved1[1] = 2;
    reserved1[2] = 3;
    ring.unreserve_chunk(&ring, reserved1);

    // Reserve 3 chunks. This will move start_addr to the start of the last chunk.
    uint8_t *reserved4 = ring.reserve_chunk(&ring);
    reserved_junk = ring.reserve_chunk(&ring);
    reserved_junk = ring.reserve_chunk(&ring);

    // Unreserve 1 chunk so that start_addr is at the start of the last chunk. end_addr is at the
    // end of the first chunk. The middle 2 chunks do not contain valid data
    //eg. arr < end_addr < unused chunk < unused chunk < start_addr < arr_end

    reserved4[0] = 4;
    reserved4[1] = 5;
    reserved4[2] = 6;
    ring.unreserve_chunk(&ring, reserved4);

    // Check that we've got everything setup correctly
    assert(reserved_junk > ring.end_addr && reserved_junk < ring.start_addr);
    assert(ring.end_addr > ring.arr && ring.end_addr < ring.start_addr);
    assert(ring.start_addr < ring.arr_end);
    assert(ring.size(&ring) == 6);

    TPRINTF(" 2 ");
    assert(ring.read_from(&ring, reserved1, 0, dest, 1) == 1);
    assert(dest[0] == 1);

    TPRINTF(" 3 ");
    assert(ring.read_from(&ring, reserved1, 1, dest, 1) == 1);
    //printf("%u\n", (unsigned)dest[0]);
    assert(dest[0] == 2);

    assert(ring.read_from(&ring, reserved1 + 2, 1, dest, 1) == 1);
    assert(dest[0] == 4);

    assert(ring.read_from(&ring, reserved1, 4, dest, 1) == 1);
    assert(dest[0] == 5);

    TPRINTF(" 4 ");
    assert(ring.read_from(&ring, reserved1 + 1, -1, dest, 3) == 3);
    assert(dest[0] == 1);
    assert(dest[1] == 2);
    assert(dest[2] == 3);

    TPRINTF(" 5 ");
    assert(ring.read_from(&ring, reserved4, -3, dest, 10) == 6); // offset takes us to the start of reserved1. This should read all the data.
    assert(dest[0] == 1);
    assert(dest[3] == 4);
    assert(dest[5] == 6);

    assert(ring.read_from(&ring, reserved4 + 2, -1, dest, 10) == 2);
    assert(dest[0] == 5);

    assert(ring.read_from(&ring, reserved4, 0, dest, 10) == 3);
    assert(dest[0] == 4);

    assert(ring.read_from(&ring, reserved4, 1, dest, 2) == 2);
    assert(dest[0] == 5);
    assert(dest[1] == 6);

    // test 'index'
    // reserved4 is located at the start of the buffer - it has wrapped. Position 0 within this
    // chunk is index 3
    //printf("%d\n", (int)ring.index(&ring, reserved4));
    assert(ring.index(&ring, reserved4) == 3);
    assert(ring.index(&ring, reserved4 + 2) == 5);

    // reserved_junk is the 3rd physical chunk in the buffer. It is between
    // end_addr and start_addr and so doesn't contain valid data
    assert(ring.index(&ring, reserved_junk) == -1);
    assert(ring.index(&ring, reserved_junk + 2) == -1);

    // reserved1 is the 1st physical chunk in the buffer
    assert(ring.index(&ring, reserved1) == 0);
    assert(ring.index(&ring, reserved1 + 2) == 2);

    // check for buffer over/underrun
    assert(dest[10] == 99);
    assert(outer_arr[0] == 101);
    assert(outer_arr[13] == 102);

    TPRINTF(" OK\n");
}

static void testx() {
    TPRINTF("testx...");

    TPRINTF(" 1 ");
    int chunk_size = 512;
    uint8_t arr[10000];
    struct scoppy_uint8_chunked_ring_buffer ring;
    scoppy_uint8_chunked_ring_buffer_init(&ring, arr, 10000, chunk_size);

    ring.arr = (uint8_t *)536886349;
    ring.arr_end = (uint8_t *)536902732;
    ring.arr_size = 16384;
    ring.start_addr = (uint8_t *)536891469;
    ring.end_addr = (uint8_t *)536890444;

    uint8_t *trigger_addr = (uint8_t *)536889450;

    uint32_t size = ring.size(&ring);
    printf("size=%u\n", (unsigned)size);

    int32_t trigger_idx = ring.index(&ring, trigger_addr);
    printf("trigger_idx=%ld\n", (long)trigger_idx);

    int min_num_post_trigger_bytes = 1000;
    if ((size - trigger_idx) >= (min_num_post_trigger_bytes + chunk_size)) {
        printf("Bigger! Wrong!\n");
    } else {
        printf("Not Bigger. OK.\n");
    }
}

void run_scoppy_chunked_ring_buffer_tests() {
    TPRINTF("run_scoppy_chunked_ring_buffer_tests...\n");
    //chunked_ring_buffer_basic_test();
    //chunked_ring_buffer_data_test();
    //chunked_ring_buffer_dma_test();
    //chunked_ring_buffer_read_from_non_wrapped_test();
    //chunked_ring_buffer_read_from_wrapped_test();
    testx();
}
