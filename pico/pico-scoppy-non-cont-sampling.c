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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// For ADC input:
#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
//#include "hardware/structs/bus_ctrl.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include "pico/util/queue.h"

//
#include "scoppy-chunked-ring-buffer.h"
#include "scoppy-common.h"
#include "scoppy-message.h"
#include "scoppy-outgoing.h"
#include "scoppy.h"

#include "pico-scoppy-non-cont-sampling.h"
#include "pico-scoppy-samples.h"
#include "pico-scoppy-util.h"
#include "scoppy-pio.h"

#ifndef NDEBUG
// Enabling this can cause problems at higher sample rates
#define DEBUG_SINGLE_SHOT 0
#define STATS_ENABLED 1
#else
#define DEBUG_SINGLE_SHOT 0
#define STATS_ENABLED 0
#endif

// At samples rates of 20MSPS, a chunk size of 512 is too small and the dma_interrupt handlers
// can't keep up. But we don't want the chunk size too big because the (pio) trigger_addr might end up too
// far away from the actual trigger point.

// Also chunk size MUST be less than min_post_trigger_bytes so that if trigger_addr is within a reserved chunk (eg pio triggering) then
// enought bytes will be written to the chunk for it to become unreserved (hmmm - or will this happen anyway?)

//#define MAX_CHUNK_SIZE 512

// 1024 works at a sample rate of 20833333 in DEBUG mode. Fails at 125Ms/s
#define MAX_CHUNK_SIZE 2048

uint dma_chan1;
uint dma_chan2;

//
// The ring buffer has to be large enough so that in the time between finding the trigger point
// and locking the buffer, the trigger point data doesn't get overwritten (cos the buffer has wrapped)
// There will also be 2 reserved chunks at any one time which also decreases the effective amount of
// data that can be held in the buffer
//
// At a sample rate of 500kS/s, about 500 bytes will be written to the buffer while waiting for
// the buffer to lock.
// see: doc/perf-tests/firmware-20210421.txt
//
// We use this buffer for single shot mode so it must be big enough - including space for
// unreserved chunks
//
#define RING_BUF_ARR_SIZE SINGLE_SHOT_TOTAL_BYTES_TO_SEND + (MAX_CHUNK_SIZE * 10)
static uint8_t ring_buf1_arr[RING_BUF_ARR_SIZE];
static struct scoppy_uint8_chunked_ring_buffer ring_buf1;

// NB. This should only need to be as big as the largest chunk. However, I found a buffer overrun outside of the
// get samples function. I think that the interupt handler isn't getting called quickly enought to change the
// write address. My guess is that the process of sending over USB is generating lots of interupts and delays
// the dma interrupts (the USB interupt is higher priority). One solution might be to have the USB sending happen
// on a different core. Another is to make the 'rubbish' transfer size bigger.
// But I'm lazy and will just make a bigger buffer here;
// The first and last bytes are used to check for buffer overruns (so don't write to them!)
#define RUBBISH_SIZE (MAX_CHUNK_SIZE * 5)
static uint8_t rubbish_buf[RUBBISH_SIZE + 2];

// Given that there will always be 2 unreserved chunks in the buffer we need to keep the chunk size small
// However a small chunk size means frequent ping-pongs between the handlers so we don't want to overdo it!
// Keeping the interupt handlers as short as possible is important.
static int chunk_size = -1;

// The number of chunks processed before we timeout when looking for a trigger
static int32_t max_trigger_chunks = -1;

// The number of (possibly multi-channel samples) per chunk
// eg if chunk_size = 128 and number of channels is 2 then
// samples_per_chunk will be 64
static int samples_per_chunk = -1;

static struct scoppy_uint8_chunked_ring_buffer *active_buffer = &ring_buf1;

// Pointers to the memory location(s) that the dma channels can write to
static uint8_t *reserved1 = NULL;
static uint8_t *reserved2 = NULL;

// Using a flag rather than a mutex/sem etc 'cos we're only running on the one
// core. This will probably have to change if we move to multicore
// Not sure if these actually have to be volatile
static volatile bool waiting_for_pre_trigger_samples = false;
static volatile bool waiting_for_post_trigger_samples = false;
static volatile bool buffer_locked = false;
static volatile bool ch1_stopped = false;
static volatile bool ch2_stopped = false;

static volatile uint8_t *trigger_addr = NULL;
static volatile bool hardware_triggered = false;

#ifndef NDEBUG
// For debugging
static int in_dma_chan1_handler = 0;
static int in_dma_chan2_handler = 0;

static uint8_t first_ch1_reserved_byte_value = 0;
static uint8_t first_ch2_reserved_byte_value = 0;
#endif

// queue of chunks to be checked for trigger
static queue_t trigger_chunk_queue;
static volatile bool looking_for_software_trigger_point = false;

#ifndef NDEBUG
struct checkpoint {
    absolute_time_t timestamp;
    const char *name;
    uint8_t *trigger_addr;
    struct scoppy_uint8_chunked_ring_buffer buffer;
};

#define MAX_CHECKPOINTS 6
struct checkpoint checkpoint1;
struct checkpoint checkpoint2;
struct checkpoint checkpoint3;
struct checkpoint checkpoint4;
struct checkpoint checkpoint_dma_handler;
struct checkpoint checkpoint_abort;

static struct checkpoint *checkpoints[MAX_CHECKPOINTS];

static void clear_checkpoints() {
    for (int i = 0; i < MAX_CHECKPOINTS; i++) {
        checkpoints[i] = NULL;
    }
}

static void add_checkpoint(struct checkpoint *checkpoint, const char *name, volatile uint8_t *trigger_addr, struct scoppy_uint8_chunked_ring_buffer *buffer) {
    checkpoint->timestamp = get_absolute_time();
    checkpoint->name = name;
    checkpoint->trigger_addr = (uint8_t *)trigger_addr;
    buffer->copy(buffer, &checkpoint->buffer);

    for (int i = 0; i < MAX_CHECKPOINTS; i++) {
        if (checkpoints[i] == NULL) {
            checkpoints[i] = checkpoint;
            return;
        }
    }

    printf("Error adding checkpoint: %s\n", name);
    for (int i = 0; i < MAX_CHECKPOINTS; i++) {
        struct checkpoint *cp = checkpoints[i];
        if (cp != NULL) {
            printf(" already added=%s\n", cp->name);
        } else {
            printf(" WTF?\n");
        }
    }
    sleep_ms(1000);
    assert(false);
}

static void print_sampling_params(struct sampling_params *params) {
    printf("  preferredSampleRatePerChannelHz=%lu, ", (unsigned long)params->preferredSampleRatePerChannelHz);
    printf("  realSampleRatePerChannel=%lu, ", (unsigned long)params->realSampleRatePerChannel);
    printf("  clkdivint=%lu\n", (unsigned long)params->clkdivint);

    printf("  num_bytes_to_send=%d, ", (int)params->num_bytes_to_send);
    printf("  min_num_pre_trigger_bytes=%d, ", (int)params->min_num_pre_trigger_bytes);
    printf("  min_num_post_trigger_bytes=%d\n", (int)params->min_num_post_trigger_bytes);

    // printf("  seq=%lu", (unsigned long)params->seq);

    printf("  enabled_channels=0x%X, ", (unsigned)params->enabled_channels);
    printf("  num_enabled_channels=%u\n", (unsigned)params->num_enabled_channels);

    printf("  trigger_mode=%u, ", (unsigned)params->trigger_mode);
    printf("  run_mode=%u\n", (unsigned)params->run_mode);
}

static void print_debug() {
    printf("####\n");
    printf("active_params:\n");
    print_sampling_params(active_params);
    printf("globals:\n");
    printf("  chunk_size=%d, max_trigger_chunks=%ld, samples_per_chunk=%d\n", chunk_size, (long int)max_trigger_chunks, samples_per_chunk);
    printf("  looking_for_software_trigger_point= %d, ", (int)looking_for_software_trigger_point);
    printf("  waiting_for_pre_trigger_samples=%d, waiting_for_post_trigger_samples=%d, buffer_locked=%d, ", (int)waiting_for_pre_trigger_samples,
           (int)waiting_for_post_trigger_samples, (int)buffer_locked);
    printf("  trig_q_count=%d, rubbish_buf=0x%lx\n", (int)trigger_chunk_queue.element_count, (unsigned long)rubbish_buf);

    absolute_time_t *last_time = NULL;
    for (int i = 0; i < MAX_CHECKPOINTS; i++) {
        struct checkpoint *cp = checkpoints[i];
        if (cp == NULL) {
            continue;
        }

        if (last_time != NULL) {
            int64_t diff = absolute_time_diff_us(*last_time, cp->timestamp);
            printf("  interval=%lu us\n", (long unsigned)diff);
        }
        last_time = &cp->timestamp;

        printf("checkpoint=%s\n", cp->name);
        printf("  trigger_addr=%lx\n", (unsigned long)cp->trigger_addr);
        cp->buffer.dump(&cp->buffer);
    }
}

static void print_debug_and_abort(const char *msg) {
    // try and stop the current data being changed
    buffer_locked = true;

    printf("%s\n", msg);
    add_checkpoint(&checkpoint_abort, "abort", trigger_addr, active_buffer);
    print_debug();
    busy_wait_us(2000000);
    assert(false);
}

#endif // NDEBUG

static inline void dma_handler_unreserve(uint8_t *reserved) {
    // tell the buffer we have finished writing the chunk
    // DEBUG_PRINT("DMA: reserved=%u %u\n", (unsigned)*reserved, (unsigned)*(reserved+1));
    active_buffer->unreserve_chunk(active_buffer, reserved);
    if (looking_for_software_trigger_point) {
        if (!queue_try_add(&trigger_chunk_queue, &reserved)) {
            // this should only happen if the queue is full
            assert(false);
        }
    }
}

static inline void dma_handler_on_reserved(uint ch, uint8_t *reserved) {
    if (waiting_for_pre_trigger_samples) {
        if (active_buffer->size(active_buffer) >= active_params->min_num_pre_trigger_bytes) {
            waiting_for_pre_trigger_samples = false;
        }
    } else if (waiting_for_post_trigger_samples) {
        if (trigger_addr != NULL) {
            // NB. This should not be called between unreserve and reserve or else size will be one chunk too big
            // (bacause reserve will reduce the value returned by size if the buffer is full - ie it increments start_addr)
            // NB2. Trying to implement this code in get_samples() is very unreliable. If we try that presumably the buffer gets
            // updated in the dma handler in the middle of size or index or both and these functions return incorrect/inconsistent
            // values

            int32_t trigger_index = active_buffer->index(active_buffer, (uint8_t *)trigger_addr);
            if (trigger_index >= 0) {
                if ((active_buffer->size(active_buffer) - trigger_index) >= active_params->min_num_post_trigger_bytes) {
#ifndef NDEBUG
                    add_checkpoint(&checkpoint_dma_handler, "DMA_HANDLER", trigger_addr, active_buffer);
#endif

                    // OK. We have enough post trigger bytes
                    waiting_for_post_trigger_samples = false;

                    // We could proceed to locking the buffer directly from here
                }
            } else {
                // when using pio triggering, trigger_addr might be within a reserved chunk (and thus 'index' returns -1)
            }
        } else {
            if (active_buffer->size(active_buffer) >= active_params->num_bytes_to_send) {
                // OK. We have enough bytes to send.
                waiting_for_post_trigger_samples = false;

                // We could proceed to locking the buffer directly from here
            }
        }
    }

#ifndef NDEBUG
    memset(reserved, 99, chunk_size);
#endif

    // tell the dma channel where to write to
    dma_channel_set_write_addr(ch, reserved, false);
}

static void dma_chan1_handler() {
#ifndef NDEBUG
    // check that a dma interrupt handler is not called during execution of this handler
    in_dma_chan1_handler++;
    assert(in_dma_chan1_handler == 1);
    assert(in_dma_chan2_handler == 0);
#endif

    // DEBUG_PUTS("dma_chan1_handler()");

    if (buffer_locked) {
        // Allow the dma transfers to continue but don't write to the active buffer
        // Alternatively we could probably stop the chaining and then resume when the buffer is unlocked
        // but that might be much more complicated

        dma_channel_set_write_addr(dma_chan1, rubbish_buf + 1, false);

        // printf("1. dma_chan1_handler(): buffer locked\n");
        reserved1 = NULL;
        ch1_stopped = true;
    } else {

        if (reserved1 != NULL) {
            dma_handler_unreserve(reserved1);
            assert(*(active_buffer->next_chunk_addr) == first_ch1_reserved_byte_value);
        } else {
            // reserved1 will be NULL if the buffer has recently been locked
        }

        //
        // The other channel is now running (because of the chain_to)
        // We need to set the new write address for when this channel resumes
        //

        // reserve space in the buffer that this channel can write to
        reserved1 = active_buffer->reserve_chunk(active_buffer);

#ifndef NDEBUG
        // Save the value of the first byte of the next chunk that will be reserved by ch2
        // If this value is changed before it is reserved by ch2 it means that ch1 has overwritten it
        // (it should not be written to until it is reserved!)
        first_ch2_reserved_byte_value = *(active_buffer->next_chunk_addr);
#endif

        dma_handler_on_reserved(dma_chan1, reserved1);

        // remember 2 chunks will be reserved - so the size will never grow to the total size of
        // the buffer
        // DEBUG_PRINT("  1.active_buffer->size()=%lu\n", (unsigned long)active_buffer->size(active_buffer));
        // DEBUG_PRINT("  1.active_buffer->size()=%lu\n", (unsigned long)active_buffer->size(active_buffer));

        ch1_stopped = false;
    }

#ifndef NDEBUG
    assert(in_dma_chan1_handler == 1);
    assert(in_dma_chan2_handler == 0);
    in_dma_chan1_handler--;
#endif

    // Clear the interrupt request.
    dma_hw->ints0 = 1u << dma_chan1;
}

static void dma_chan2_handler() {
#ifndef NDEBUG
    in_dma_chan2_handler++;
    assert(in_dma_chan2_handler == 1);
    assert(in_dma_chan1_handler == 0);
#endif

    // DEBUG_PUTS("dma_chan2_handler()");

    if (buffer_locked) {
        // Allow the dma transfers to continue but don't write to the active buffer
        // Alternatively we could probably stop the chaining and then resume when the buffer is unlocked
        // but that might be much more complicated

        dma_channel_set_write_addr(dma_chan2, rubbish_buf + 1, false);

        // printf("1. dma_chan2_handler(): buffer locked\n");
        reserved2 = NULL;
        ch2_stopped = true;
    } else {

        if (reserved2 != NULL) {
            dma_handler_unreserve(reserved2);
            assert(*(active_buffer->next_chunk_addr) == first_ch2_reserved_byte_value);
        } else {
            // reserved2 will be NULL if the buffer has recently been locked
        }

        //
        // The other channel is now running (because of the chain_to)
        // We need to set the new write address for when this channel resumes
        //

        // reserve space in the buffer that this channel can write to
        reserved2 = active_buffer->reserve_chunk(active_buffer);

#ifndef NDEBUG
        // Save the value of the first byte of the next chunk that will be reserved by ch2
        // If this value is changed before it is reserved by ch2 it means that ch1 has overwritten it
        // (it should not be written to until it is reserved!)
        first_ch1_reserved_byte_value = *(active_buffer->next_chunk_addr);
#endif

        dma_handler_on_reserved(dma_chan2, reserved2);

        // remember 2 chunks will be reserved - so the size will never grow to the total size of
        // the buffer
        // DEBUG_PRINT("  2.active_buffer->size()=%lu\n", (unsigned long)active_buffer->size(active_buffer));
        // DEBUG_PRINT("  2.active_buffer->size()=%lu\n", (unsigned long)active_buffer->size(active_buffer));

        ch2_stopped = false;
    }

#ifndef NDEBUG
    assert(in_dma_chan2_handler == 1);
    assert(in_dma_chan1_handler == 0);
    in_dma_chan2_handler--;
#endif

    // Clear the interrupt request.
    dma_hw->ints0 = 1u << dma_chan2;
}

//
// Statistics
//

#if STATS_ENABLED
// The time at which the pico_scoppy_get_non_continuous_samples function was exited
absolute_time_t end_get_samples_checkpoint;
// Cumulative time spent outside this function
int64_t total_external_time = 0;
// Cumulative time for locking buffer and waiting for dma transfer to stop
int64_t total_locking_time = 0;
// Cumulative time other stuff
int64_t total_pre_trigger_wait_time = 0;
int64_t total_trigger_wait_time = 0;
int64_t total_post_trigger_wait_time = 0;
int64_t total_buf_copy_time = 0;

int64_t total_get_samples_time = 0;
uint32_t total_get_samples_invokations = 0;
uint32_t stats_sample_rate = 0;
uint8_t stats_num_channels = 0;
uint stats_max_trigger_queue_size = 0;
uint num_timeouts = 0;
int stats_num_bytes_to_send = 0;
#endif // STATS_ENABLED

static uint8_t wait_for_software_trigger(struct scoppy_context *ctx, int trigger_channel_idx, uint8_t num_bytes_per_sample) {
    uint8_t trigger_level = scoppy.app.trigger_level;

    // Note. We don't read trigger type from active_params because changing the trigger type in oscilloscope mode
    // doesn't cause sampling to restart (and so active_params.trigger_type isn't updated)
    uint8_t trigger_type = scoppy.app.trigger_type;

    uint8_t dbg_trigger_value = 99;

    {
        bool aquisition_params_changed = false;
        int32_t trigger_chunks_processed = 0;
        uint8_t last_sample_value = trigger_level;
        while (trigger_addr == NULL && trigger_chunks_processed < max_trigger_chunks && !aquisition_params_changed && trigger_channel_idx >= 0) {
            // if (aquisition_configuration_changed()) {
            //    return;
            //}

#if STATS_ENABLED
            uint queue_size = queue_get_level(&trigger_chunk_queue);
            if (queue_size > stats_max_trigger_queue_size) {
                stats_max_trigger_queue_size = queue_size;
            }
#endif

            uint8_t *trig_check_addr;
            if (queue_try_remove(&trigger_chunk_queue, &trig_check_addr)) {
                // check chunk for trigger sample making sure we check the byte that corresponds to the
                // trigger channel
                trig_check_addr += trigger_channel_idx;
                if (trigger_chunks_processed == 0) {
                    last_sample_value = *trig_check_addr;
                }

                for (int i = 0; i < samples_per_chunk; i++) {
                    uint8_t current_sample_value = *trig_check_addr;

                    bool triggered = false;
                    if (trigger_type == TRIGGER_TYPE_RISING_EDGE) {
                        if (last_sample_value < trigger_level && current_sample_value >= trigger_level) {
                            triggered = true;
                        }
                    } else if (trigger_type == TRIGGER_TYPE_FALLING_EDGE) {
                        if (last_sample_value > trigger_level && current_sample_value <= trigger_level) {
                            triggered = true;
                        }
                    } else {
#ifndef NDEBUG
                        // wtf
                        printf("unknown trigger type");
                        sleep_ms(2000);
                        assert(false);
#endif
                    }

                    if (triggered) {
                        trigger_addr = trig_check_addr;

#ifndef NDEBUG
                        add_checkpoint(&checkpoint1, "Found trigger", trigger_addr, active_buffer);
                        dbg_trigger_value = *trigger_addr;
                        assert(dbg_trigger_value == current_sample_value);
#endif
                        // we will also break out of the while loop because trigger_addr is set
                        break;
                    } else {
                        last_sample_value = current_sample_value;
                        trig_check_addr += num_bytes_per_sample;
                    }
                }

                trigger_chunks_processed++;
            } else {
                // Nothing in trigger chunk queue
            }

            if (pico_scoppy_is_sampler_restart_required()) {
                aquisition_params_changed = true;
            }

            //
            // If the trigger_chunk_queue is growing in size (which might happen with faster ADCs than the internal pico adc)
            // then we can cull the queue here (ie we wont check all the chunks). The app should then degrade gracefully :)
            //
            // eg
            // if (qeueu.size > 3) {
            //  foreach entry in queue: delete from queue, trigger_chunks_processed++
            //}

            // every so often check for message from the app, esp in normal mode where we could
            // be stuck in this loop indefinitely
            // eg. timebase, triggerlevel etc changes
        }
    }

    return dbg_trigger_value;
}

uint8_t *g_hw_trig_dma1_write_addr = 0;
uint8_t *g_hw_trig_dma2_write_addr = 0;
uint32_t g_hw_trig_dma1_trans_count = 0;
uint32_t g_hw_trig_dma2_trans_count = 0;

static uint8_t wait_for_hardware_trigger(struct scoppy_context *ctx) {
    uint8_t trigger_mode = active_params->trigger_mode;
    if (trigger_mode == TRIGGER_MODE_NONE) {
        trigger_addr = NULL;
        return 0u;
    }

    // we haven't told the state machine to start looking for trigger points yet
    assert(scoppy_hardware_triggered == false);

    // Tell the trigger state machine to arm (start looking for trigger points).
    scoppy_pio_arm_trigger();

    absolute_time_t last_time = get_absolute_time();
    while (!scoppy_hardware_triggered) {

        // Check for 100ms elapsed since last_time
        absolute_time_t now = get_absolute_time();
        if (absolute_time_diff_us(last_time, now) > 100000) {
            if (trigger_mode == TRIGGER_MODE_AUTO) {
                break;
            } else {
                // Normal mode. We could be going for a while so check for incoming messages
                if (pico_scoppy_is_sampler_restart_required()) {
                    break;
                }
            }
            last_time = now;
        } else {
            tight_loop_contents();
        }
    }

    // Save these values as quickly as possible because they could change under us
    // No. They are now saved in the pio interrupt handler
    // hw_trig_dma1_write_addr = (uint8_t *)dma_channel_hw_addr(dma_chan1)->write_addr;
    // hw_trig_dma2_write_addr = (uint8_t *)dma_channel_hw_addr(dma_chan2)->write_addr;
    // hw_trig_dma1_trans_count = dma_channel_hw_addr(dma_chan1)->transfer_count;
    // hw_trig_dma2_trans_count = dma_channel_hw_addr(dma_chan2)->transfer_count;
    uint8_t ret = 0u;
    if (scoppy_hardware_triggered) {
        uint8_t *hw_trig_dma1_write_addr = g_hw_trig_dma1_write_addr;
        uint8_t *hw_trig_dma2_write_addr = g_hw_trig_dma2_write_addr;
        uint32_t hw_trig_dma1_trans_count = g_hw_trig_dma1_trans_count;
        uint32_t hw_trig_dma2_trans_count = g_hw_trig_dma2_trans_count;

        if (hw_trig_dma1_trans_count > 0) {
            // NB. This addr might not have been written to yet (because it represents the NEXT write addr)
            // NB. This addr is probably in a reserved (not unreserved) chunk
            trigger_addr = hw_trig_dma1_write_addr;
        } else if (hw_trig_dma2_trans_count > 0) {
            // See above
            trigger_addr = hw_trig_dma2_write_addr;
        } else {
            // Use the end address of the last unreserved (written to) chunk - can be null (but shouldn't be!)
            trigger_addr = active_buffer->end_addr;
        }
        assert(trigger_addr != NULL);

#ifndef NDEBUG
        // We use -1 here because the trigger_addr might not have been written to yet (and so will end up with a different value when it is)
        // NB. There's a chance that (trigger_addr-1) is before the start of the buffer - but don't worry this as its just used in DEBUG mode
        uint8_t x = *(trigger_addr - 1);
        if (x == 99u || (trigger_addr < active_buffer->arr || trigger_addr > active_buffer->arr_end)) {
            add_checkpoint(&checkpoint2, "trigger addr out of bounds", trigger_addr, active_buffer);
            print_debug();
            printf("trigger_addr=0x%lx\n", (unsigned long)trigger_addr);
            printf("hw_trig_dma1_write_addr: 0x%lx\n", (unsigned long)hw_trig_dma1_write_addr);
            printf("hw_trig_dma2_write_addr: 0x%lx\n", (unsigned long)hw_trig_dma2_write_addr);
            printf("hw_trig_dma1_trans_count: 0x%lx\n", (unsigned long)hw_trig_dma1_trans_count);
            printf("hw_trig_dma2_trans_count: 0x%lx\n", (unsigned long)hw_trig_dma2_trans_count);
            printf("trigger_addr=%u, -1=%u, -2=%u, -3=%u, -4=%u\n", (unsigned)*trigger_addr, (unsigned)*(trigger_addr - 1), (unsigned)*(trigger_addr - 2),
                   (unsigned)*(trigger_addr - 3), (unsigned)*(trigger_addr - 4));
            sleep_ms(10000);
            abort();
        }
        ret = x;
#endif
    } else {
        trigger_addr = NULL;
    }

    // The sm might be looping waiting for a trigger. If it happens to find one while the dma_handlers are writing to the 
    // rubbish_buf then the hw_trig vars will be wrong...so we need to ensure no the pio isn't triggered until we write to the
    // fito again
    scoppy_pio_disarm_trigger(active_params);

    // The trigger pio should now be in a state where it's waiting for a write to the fifo

    scoppy_hardware_triggered = false;
    return ret;
}

void pico_scoppy_get_non_continuous_samples(struct scoppy_context *ctx) {
    // DEBUG_PRINT("pico_scoppy_get_non_continuous_samples(): dma_chan=%u\n", (unsigned)dma_chan);

#ifndef NDEBUG
    // check for buffer overrun
    if (rubbish_buf[RUBBISH_SIZE] != 104) {
        printf("rb238=%u\n", (unsigned)rubbish_buf[RUBBISH_SIZE]);
        sleep_ms(100);
    }
    clear_checkpoints();
#endif

#if STATS_ENABLED
    total_get_samples_invokations++;
    absolute_time_t start_get_samples_checkpoint = get_absolute_time();
    if (end_get_samples_checkpoint._private_us_since_boot != 0) {
        total_external_time += absolute_time_diff_us(end_get_samples_checkpoint, start_get_samples_checkpoint);
    }
#endif // STATS_ENABLED

    // Initialise variables and data structures shared between this method and the interrupt handlers
    trigger_addr = NULL;

    // empty the trigger chunk queue
    while (!queue_is_empty(&trigger_chunk_queue)) {
        uint8_t *tmp;
        queue_try_remove(&trigger_chunk_queue, &tmp); // yes -deliberately passing a pointer to the pointer
    }

    assert(buffer_locked == false);
    assert(waiting_for_pre_trigger_samples == false);
    assert(waiting_for_post_trigger_samples == false);
    assert(looking_for_software_trigger_point == false);
    assert(hardware_triggered == false);

    // Ensure the dma channels are writing to the active buffer (a transfer to rubbish_buf might still be in progress)
    while (ch1_stopped || ch2_stopped) {
        tight_loop_contents();
    }

    waiting_for_pre_trigger_samples = true;

    //
    bool is_logic_mode = active_params->is_logic_mode;

    // The index of the trigger channel byte within the (possibly multichannel) sample
    // eg. if there are 3 channels (all enabled) and the we're triggering on the last channel then the index will
    // be 2
    // If channel 0 is disabled and we're triggering on channel 1 then the index will be 0
    // If channel 0 is enabled and we're triggering on channel 1 then the index will be 1
    int trigger_channel_idx = -1;
    {
        if (!is_logic_mode) {
            int channel_idx = -1;
            for (int channel_id = 0; channel_id < MAX_CHANNELS; channel_id++) {
                if (active_params->enabled_channels & (1 << channel_id)) {
                    channel_idx++;
                    if (channel_id == scoppy.app.trigger_channel) {
                        trigger_channel_idx = channel_idx;
                        break;
                    }
                }
            }
        } else {
            // There's only ever one byte per sample in logic mode
            trigger_channel_idx = 0;
        }
    }

    uint8_t num_channels = active_params->num_enabled_channels;
    uint8_t total_bytes_per_sample = is_logic_mode ? 1 : num_channels;


#if DEBUG_SINGLE_SHOT
    if (active_params->run_mode == RUN_MODE_SINGLE) {
        printf("-- SINGLE SHOT - waiting for pre trigger samples --\n");
        active_buffer->dump(active_buffer);
    }
#endif

    // Aquire required number of pretrigger samples/bytes
    // The dma handler will update 'waiting_for_pre_trigger_samples' flag
    while (waiting_for_pre_trigger_samples) {
        tight_loop_contents();
    }

    assert((active_buffer->size(active_buffer) % total_bytes_per_sample) == 0);

#if STATS_ENABLED
    absolute_time_t finished_pre_trigger_wait_checkpoint = get_absolute_time();
    total_pre_trigger_wait_time += absolute_time_diff_us(start_get_samples_checkpoint, finished_pre_trigger_wait_checkpoint);
#endif // STATS_ENABLED

    assert(queue_get_level(&trigger_chunk_queue) == 0);

    if (!is_logic_mode) {
        // tell the interrupt handlers that we're now looking for trigger points
        looking_for_software_trigger_point = true;
    }

#if DEBUG_SINGLE_SHOT
    if (active_params->run_mode == RUN_MODE_SINGLE) {
        printf("-- SINGLE SHOT - waiting for trigger point --\n");
        active_buffer->dump(active_buffer);
    }
#endif

    // Keep aquiring samples until triggered or timeout (auto triggering)
    //
    // This is probably the most time/performance critical part of the app because we need to be able
    // to search for a trigger point faster than the samples can be generated.
    // At a sample rate of 6 MS/s (eg. using 2x3MS/s external ADCs eg MAX11116), a sample is generated every 167ns. At 125MHz each clock cycle is 8ns which
    // gives us only 20 cycles to work with.
    //
    // For really fast ADCs we won't be able to process all the chunks. Either we forget about triggering here (and just send frames to
    // the app and hope they contain a trigger point that the app can detect) or implement hardware triggering.
    //

    uint8_t dbg_trigger_value;
    if (is_logic_mode) {
        dbg_trigger_value = wait_for_hardware_trigger(ctx);
    } else {
        dbg_trigger_value = wait_for_software_trigger(ctx, trigger_channel_idx, total_bytes_per_sample);
    }

    looking_for_software_trigger_point = false;

    // printf("Trigger chunks processed=%ld\n", trigger_chunks_processed);

    assert((active_buffer->size(active_buffer) % total_bytes_per_sample) == 0);

#if STATS_ENABLED
    absolute_time_t finished_trigger_wait_checkpoint = get_absolute_time();
    total_trigger_wait_time += absolute_time_diff_us(finished_pre_trigger_wait_checkpoint, finished_trigger_wait_checkpoint);
#endif // STATS_ENABLED

    //
    //  Get post-trigger samples
    //
#if DEBUG_SINGLE_SHOT
    if (active_params->run_mode == RUN_MODE_SINGLE) {
        printf("-- SINGLE SHOT - waiting for post trigger samples --\n");
        active_buffer->dump(active_buffer);
    }
#endif

    waiting_for_post_trigger_samples = true;
    while (waiting_for_post_trigger_samples) {
        tight_loop_contents();
    }
    assert(waiting_for_post_trigger_samples == false);

#ifndef NDEBUG
    add_checkpoint(&checkpoint2, "Got post trigger samples", trigger_addr, active_buffer);
#endif

    assert((active_buffer->size(active_buffer) % total_bytes_per_sample) == 0);

#if STATS_ENABLED
    absolute_time_t finished_post_trigger_wait_checkpoint = get_absolute_time();
    total_post_trigger_wait_time += absolute_time_diff_us(finished_trigger_wait_checkpoint, finished_post_trigger_wait_checkpoint);
#endif // STATS_ENABLED

    //
    //  Lock buffer
    //

    //
    // We need access to the buffer that is currently being used.
    // However a dma write might be in progress or a dma interrupt handler might be called during execution
    // of this method.
    //
    // Unlike continuous mode, we don't need to continue writing to a buffer while sending the samples
    // to the app. So instead of swapping buffers, we just need dma to stop writing to the buffer.
    //
    // So we let the next interrupt handlers 'tell us' that the buffer can be safely accessed'.
    // Because of this we need the interrupt handlers to be called
    // fairly frequently eg. less than 100ms (per dma channel) so that we can send at least 5 frames per second
    //
    assert(!ch1_stopped && !ch2_stopped && !buffer_locked);
    buffer_locked = true;

#if DEBUG_SINGLE_SHOT
    if (active_params->run_mode == RUN_MODE_SINGLE) {
        printf("-- SINGLE SHOT - waiting to lock buffer --\n");
        active_buffer->dump(active_buffer);
    }
#endif

    // Wait for the dma channels to stop writing to the buffer
    while (!ch1_stopped || !ch2_stopped) {
        tight_loop_contents();
    }
    // printf("  both channels stopped\n");

#ifndef NDEBUG
    add_checkpoint(&checkpoint3, "Locked", trigger_addr, active_buffer);
#endif

#if STATS_ENABLED
    absolute_time_t finished_locking_checkpoint = get_absolute_time();
    total_locking_time += absolute_time_diff_us(finished_post_trigger_wait_checkpoint, finished_locking_checkpoint);
#endif // STATS_ENABLED

    //
    //  Create outgoing message with sample data
    //

#if DEBUG_SINGLE_SHOT
    if (active_params->run_mode == RUN_MODE_SINGLE) {
        printf("-- SINGLE SHOT - sending samples to app --\n");
        active_buffer->dump(active_buffer);
    }
#endif

    // We can now safely access the buffer
    assert((active_buffer->size(active_buffer) % total_bytes_per_sample) == 0);

#ifndef NDEBUG
    // save some attributes of the buffer so that we can check if it has been modified (it shouldn't be) while
    // we are processing it.
    uint32_t saved_size = active_buffer->size(active_buffer);
    uint8_t *saved_start_addr = active_buffer->start_addr;
#endif

    // Check that we have enough bytes before the trigger_addr (this should always be the case)
    if (trigger_addr != NULL) {
        int32_t trigger_byte_index = active_buffer->index(active_buffer, (uint8_t *)trigger_addr);
        if (trigger_byte_index < active_params->min_num_pre_trigger_bytes) {
            // if (trigger_addr != NULL ) we previously had enough pre-trigger bytes but....
            // start_addr has progressed too far
#ifndef NDEBUG
            buffer_locked = true; // try and prevent things changing while we write debug message
            printf("trigg_addr != NULL: trigg_idx=%lu < min_num_pre_trigger_bytes=%d\n", (unsigned long)trigger_byte_index,
                   (int)active_params->min_num_pre_trigger_bytes);
            print_debug_and_abort("too few pre-trigger-bytes");
#endif

            // We'll just end up using the last n bytes in the buffer
            trigger_addr = NULL;
        }
    }

    // copy sample data to the outgoing message buffer
    volatile uint8_t *copy_from;
    int32_t copy_from_offset;
    int32_t trigger_idx = -1; // the nth sample (not nth byte)
    if (trigger_addr != NULL) {

        // Move the trigger address back to the first byte in the (possibly multichannel) sample so that we're copying the whole sample
        copy_from = trigger_addr - trigger_channel_idx;
        // We always copy min_num_pre_trigger_bytes before the trigger sample so it's easy to find the offset of the trigger sample
        copy_from_offset = active_params->min_num_pre_trigger_bytes * -1;

        // Keep track of the trigger sample (not byte)
        trigger_idx = active_params->min_num_pre_trigger_bytes / total_bytes_per_sample;

#ifndef NDEBUG
        if (active_buffer->end_addr >= active_buffer->start_addr) {
            // Buffer has not wrapped
            if (trigger_addr < active_buffer->start_addr || trigger_addr > active_buffer->end_addr) {
                printf("invalid trigger_addr: outside unwrapped data\n");
                print_debug();
                sleep_ms(5000);
                assert(false);
            }
        } else {
            // Buffer has wrapped
            if (trigger_addr < active_buffer->start_addr && trigger_addr > active_buffer->end_addr) {
                printf("invalid trigger_addr: outside wrapped data\n");
                print_debug();
                sleep_ms(5000);
                assert(false);
            }
        }

        // Re-enable this. It is getting triggered at the highest sample rates
        // if ((is_logic_mode && *(trigger_addr - 1) != dbg_trigger_value) || (!is_logic_mode && *trigger_addr != dbg_trigger_value)) {
        //    printf("expected trig_value=%u, got=%u\n", (unsigned)dbg_trigger_value, (unsigned)*trigger_addr);
        //    print_debug();
        //    sleep_ms(5000);
        //    assert(false);
        //}

        if (active_params->run_mode == RUN_MODE_SINGLE && is_logic_mode) {
            // See how far behind the actual trigger point is the trigger address
            printf("!!! trigger_addr=%lx, *trigger_addr=%x\n", (unsigned long)trigger_addr, (unsigned)*trigger_addr);
            volatile uint8_t *edge_addr = NULL;
            volatile uint8_t *test_edge_addr;
            unsigned long distance = 0;

            // working backwards from trigger addr
            // for rising-edge trigger we're looking for a transition from high (255) to low (0)
            // for falling-edge trigger we're looking for a transition from low (0) to high (0)
            uint8_t test_value = active_params->trigger_type == TRIGGER_TYPE_FALLING_EDGE ? 255u : 0u;

            uint8_t trig_ch_mask = 1u << active_params->trigger_channel;
            if (active_buffer->end_addr >= active_buffer->start_addr) {
                // buffer is unwrapped
                for (test_edge_addr = trigger_addr; test_edge_addr >= active_buffer->start_addr; test_edge_addr--) {
                    if (((*test_edge_addr) & trig_ch_mask) == test_value) {
                        edge_addr = test_edge_addr;
                        distance = trigger_addr - edge_addr;
                        break;
                    }
                }
            } else {
                // buffer is wrapped
                if (trigger_addr < active_buffer->end_addr) {
                    assert(trigger_addr >= active_buffer->arr);
                    for (test_edge_addr = trigger_addr; test_edge_addr >= active_buffer->arr; test_edge_addr--) {
                        if (((*test_edge_addr) & trig_ch_mask) == test_value) {
                            edge_addr = test_edge_addr;
                            distance = trigger_addr - edge_addr;
                            break;
                        }
                    }

                    if (edge_addr == NULL) {
                        // continue searching backwards from the end of the buffer
                        for (test_edge_addr = active_buffer->arr_end; test_edge_addr >= active_buffer->start_addr; test_edge_addr--) {
                            if (((*test_edge_addr) & trig_ch_mask) == test_value) {
                                edge_addr = test_edge_addr;
                                distance = (trigger_addr - active_buffer->arr) + (active_buffer->arr_end - edge_addr);
                                break;
                            }
                        }
                    }
                } else {
                    assert(trigger_addr >= active_buffer->start_addr);
                    assert(trigger_addr <= active_buffer->arr_end);

                    // Trigger point is between start and arrayend
                    for (test_edge_addr = active_buffer->arr_end; test_edge_addr >= active_buffer->start_addr; test_edge_addr--) {
                        if (((*test_edge_addr) & trig_ch_mask) == test_value) {
                            edge_addr = test_edge_addr;
                            distance = active_buffer->arr_end - edge_addr;
                            break;
                        }
                    }
                }
            }

            if (edge_addr == NULL) {
                printf(" !!! edge not found\n");
            } else {
                printf(" !!! edge_addr=%lx, distance=%lu, value=%x\n", (unsigned long)edge_addr, distance, (unsigned)*edge_addr);
            }
        }
#endif

        if (is_logic_mode) {
            // The trigger_addr lags behind the physical trigger point by about 4us so adjust by that amount (plus a bit extra)
            uint32_t lag_samples = active_params->realSampleRatePerChannel * 45 / 10000000;
            if (lag_samples < 10) {
                lag_samples = 10;
            }
            trigger_idx -= lag_samples;

#ifndef NDEBUG
            if (active_params->run_mode == RUN_MODE_SINGLE) {
                printf("!!!lag_samples = %lu\n", (unsigned long)lag_samples);
            }
#endif
        }

        // DEBUG_PRINT("== trigger_channel_idx=%d, trigger_value=%u, copy_from_value=%u\n", (int)trigger_channel_idx, (unsigned)*trigger_addr,
        // (unsigned)*copy_from); DEBUG_PRINT("First bytes: %u %u %u %u\n", (unsigned)dest_addr[0], (unsigned)dest_addr[1], (unsigned)dest_addr[2],
        // (unsigned)dest_addr[3]);

        // #ifndef NDEBUG
        //         if (active_params->trigger_mode == TRIGGER_MODE_NORMAL && active_params->num_enabled_channels == 1 && trigger_channel_idx == 0) {
        //             // In normal mode the trigger sample should ALWAYS be at the centre
        //             int i_centre = num_bytes_to_send / 2;

        //             // if (last_sample_value < trigger_level && current_sample_value >= trigger_level)
        //             if (!(dest_addr[i_centre - 1] < scoppy.app.trigger_level && dest_addr[i_centre] >= scoppy.app.trigger_level)) {
        //                 printf("Wrong centre bytes: %u %u => %u <= %u %u\n", (unsigned)dest_addr[i_centre - 2], (unsigned)dest_addr[i_centre - 1],
        //                 (unsigned)dest_addr[i_centre], (unsigned)dest_addr[i_centre + 1], (unsigned)dest_addr[i_centre + 2]);
        //             }
        //         }
        // #endif
    } else {
#if STATS_ENABLED
        num_timeouts++;
#endif

        // Copy the last n samples from the buffer
        copy_from = active_buffer->end_addr;
        copy_from_offset = (active_params->num_bytes_to_send - 1) * -1;

        if (active_params->trigger_mode != TRIGGER_MODE_NONE) {
            // This indicates we looked for a trigger but didn't find one
            trigger_idx = -2;
        }
    }

#if DEBUG_SINGLE_SHOT
    if (active_params->run_mode == RUN_MODE_SINGLE) {
        printf("-- SINGLE SHOT --\n");
        active_buffer->dump(active_buffer);
    }
#endif

    bool is_new_wavepoint_record = true;
    uint32_t total_num_copied = 0;
    int remaining = active_params->num_bytes_to_send;
    while (remaining > 0) {

        int this_message_size;
        if (remaining <= SCOPPY_OUTGOING_MAX_SAMPLE_BYTES) {
            this_message_size = remaining;
        } else {
            // The message data size must be an exact multiple of the number of channels - so that a sample can't span multiple
            // messages. NB. for logic mode total_bytes_per_sample is always 1
            this_message_size = (SCOPPY_OUTGOING_MAX_SAMPLE_BYTES / total_bytes_per_sample) * total_bytes_per_sample;
        }

#if DEBUG_SINGLE_SHOT
        if (active_params->run_mode == RUN_MODE_SINGLE) {
            printf("read_from(): copy_from=0x%lX, offset=%ld, size=%d\n", (unsigned long)copy_from, (long)copy_from_offset, (int)this_message_size);
            // sleep_ms(1000); made no difference
        }
#endif

        remaining -= this_message_size;
        assert(remaining >= 0);
        bool is_last_message = remaining <= 0;
        struct scoppy_outgoing *msg =
            scoppy_new_outgoing_samples_msg(active_params->realSampleRatePerChannel, active_params->channels, is_new_wavepoint_record, is_last_message,
                                            false /* not cont mode */, active_params->run_mode == RUN_MODE_SINGLE, trigger_idx, is_logic_mode);

        uint8_t *dest_addr = msg->payload + msg->payload_len;
        uint32_t num_copied = active_buffer->read_from(active_buffer, (uint8_t *)copy_from, copy_from_offset, dest_addr, this_message_size);
        msg->payload_len += num_copied;
        total_num_copied += num_copied;

        // add_checkpoint(&checkpoint4, "Copied", trigger_addr, active_buffer);

        scoppy_write_outgoing(ctx->write_serial, msg);

        copy_from_offset += this_message_size;
        is_new_wavepoint_record = false;
    }

    assert(remaining == 0);

    if (total_num_copied != active_params->num_bytes_to_send) {
        printf("Error. num_copied=%lu, num_bytes_to_send=%d\n", (unsigned long)total_num_copied, active_params->num_bytes_to_send);
#ifndef NDEBUG
        print_debug();
        sleep_ms(1000);
        abort();
#endif
    }

#ifndef NDEBUG
/*
    if (active_params->run_mode == RUN_MODE_SINGLE) {
        print_debug();
        printf("hw_trig_dma1_write_addr: 0x%lx\n", (unsigned long)hw_trig_dma1_write_addr);
        printf("hw_trig_dma2_write_addr: 0x%lx\n", (unsigned long)hw_trig_dma2_write_addr);
        printf("hw_trig_dma1_trans_count: 0x%lx\n", (unsigned long)hw_trig_dma1_trans_count);
        printf("hw_trig_dma2_trans_count: 0x%lx\n", (unsigned long)hw_trig_dma2_trans_count);
    }
    */
#endif

#ifndef NDEBUG
    // check if the buffer has been illegally accessed while we were using it
    assert(active_buffer->size(active_buffer) == saved_size);
    assert(active_buffer->start_addr == saved_start_addr);
#endif

    //
    // Clean up in preparation for next invokation of this method
    //

    // empty the buffer in preparation for new sample data to be written to it
    active_buffer->clear(active_buffer);

#ifndef NDEBUG
    // Not sure which handler will be called next
    first_ch1_reserved_byte_value = *(active_buffer->next_chunk_addr);
    first_ch2_reserved_byte_value = *(active_buffer->next_chunk_addr);
#endif

    // Check for buffer overruns
    assert(ring_buf1_arr[0] == 101);
    assert(ring_buf1_arr[sizeof(ring_buf1_arr) - 1] == 102);
    assert(rubbish_buf[0] == 103);
    assert(rubbish_buf[RUBBISH_SIZE] == 104);

    // Resume normal dma transfers
    buffer_locked = false;

#if STATS_ENABLED
    end_get_samples_checkpoint = get_absolute_time();
    total_buf_copy_time += absolute_time_diff_us(finished_locking_checkpoint, end_get_samples_checkpoint);
    total_get_samples_time += absolute_time_diff_us(start_get_samples_checkpoint, end_get_samples_checkpoint);
#endif // STATS_ENABLED
}

static void init_dma_channel(uint ch, bool is_logic_mode) {
    dma_channel_config cfg = dma_channel_get_default_config(ch);

    if (ch == dma_chan1) {
        // Tell the DMA to raise IRQ line 0 when the channel finishes a block
        dma_channel_set_irq0_enabled(ch, true);

        // Configure the processor to run dma_handler() when DMA IRQ 0 is asserted
        irq_set_exclusive_handler(DMA_IRQ_0, dma_chan1_handler);
        irq_set_enabled(DMA_IRQ_0, true);

    } else if (ch == dma_chan2) {
        // Tell the DMA to raise IRQ line 1 when the channel finishes a block
        dma_channel_set_irq1_enabled(ch, true);

        // Configure the processor to run dma_handler() when DMA IRQ 1 is asserted
        irq_set_exclusive_handler(DMA_IRQ_1, dma_chan2_handler);
        irq_set_enabled(DMA_IRQ_1, true);
    } else {
        printf("WTF?\n");
        assert(false);
    }

    // Reading from constant address, writing to incrementing byte addresses
    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_8);
    channel_config_set_read_increment(&cfg, false);
    channel_config_set_write_increment(&cfg, true);

    // Pace transfers based on availability of ADC/PIO samples
    channel_config_set_dreq(&cfg, (is_logic_mode ? scoppy_pio_get_dreq() : DREQ_ADC));
    dma_channel_set_config(ch, &cfg, false);
    dma_channel_set_read_addr(ch, (is_logic_mode ? scoppy_pio_get_dma_read_addr() : &adc_hw->fifo), false);

    // write address and count is set when initialising the adc/pio and is also updated in the interrupt handler

    // printf("Setting write address for ch %u to %lu\n", (unsigned)ch, (long unsigned)capture_buf);
    // dma_channel_set_write_addr(ch, capture_buf, false);
    // dma_channel_set_trans_count(ch, CAPTURE_DEPTH - 1, false);
}

/*
 *
 * pico_scoppy_start_non_continuous_sampling
 *
 */
void pico_scoppy_start_non_continuous_sampling() {
    DEBUG_PRINT("  pico_scoppy_start_non_continuous_sampling()\n");

    assert(scoppy_hardware_triggered == false);

#if STATS_ENABLED
    // Display stats from pevious run
    if (total_get_samples_invokations > 1) {
        printf("=========\n");
        printf("STATS: SR=%u Hz, #ch=%u, bytes_to_send=%d, chunk_size=%d, max_trig_chunks=%ld, count=%u\n", (unsigned)stats_sample_rate,
               (unsigned)stats_num_channels, stats_num_bytes_to_send, chunk_size, (long int)max_trigger_chunks, (unsigned)total_get_samples_invokations);
        printf(" get_samples      : %ld us\n", (long int)(total_get_samples_time / total_get_samples_invokations));
        printf("  pre trigger wait : %ld us\n", (long int)(total_post_trigger_wait_time / total_get_samples_invokations));
        printf("  trigger wait     : %ld us\n", (long int)(total_trigger_wait_time / total_get_samples_invokations));
        printf("  post trigger wait: %ld us\n", (long int)(total_post_trigger_wait_time / total_get_samples_invokations));
        printf("  locking wait     : %ld us\n", (long int)(total_locking_time / total_get_samples_invokations));
        printf("  buf copy         : %ld us\n", (long int)(total_buf_copy_time / total_get_samples_invokations));
        printf(" external          : %ld us\n", (long int)(total_external_time / (total_get_samples_invokations - 1)));
        printf(" max trig q size   : %u\n", (unsigned)stats_max_trigger_queue_size);
        printf(" %% timeouts       : %lu\n", (long unsigned)((num_timeouts * 100) / total_get_samples_invokations));
        printf("=========\n");
    }

    // Initialise statistics for new run
    end_get_samples_checkpoint._private_us_since_boot = 0;
    total_external_time = 0;
    total_locking_time = 0;
    total_pre_trigger_wait_time = 0;
    total_trigger_wait_time = 0;
    total_post_trigger_wait_time = 0;
    total_buf_copy_time = 0;
    total_get_samples_time = 0;
    total_get_samples_invokations = 0;
    stats_sample_rate = active_params->realSampleRatePerChannel;
    stats_num_channels = active_params->num_enabled_channels;
    stats_max_trigger_queue_size = 0;
    stats_num_bytes_to_send = active_params->num_bytes_to_send;
    num_timeouts = 0;
#endif // STATS_ENABLED

#ifndef NDEBUG
    in_dma_chan1_handler = 0;
    in_dma_chan2_handler = 0;
#endif

    // initialise global flags and counters - some of this initialisation might not be necessary - but do it just to be safe
    buffer_locked = false;
    ch1_stopped = true;
    ch2_stopped = true;
    looking_for_software_trigger_point = false;
    waiting_for_pre_trigger_samples = false;
    waiting_for_post_trigger_samples = false;

    // When we lock the active_buffer we need to wait for the 2 dma channels to finish tranferring
    // and so it's best to keep each individual transfer as short as practically possible.
    // The transfer time is a function of the sample rate and the chunk size (number of bytes to transfer)
    // time = chunk_size/freq
    // => chunk_size = freq * time
    // eg if freq is 1000Hz and we want the transfer time to be 10ms then the chunk size = 1000 * 0.010 = 10

    bool is_logic_mode = active_params->is_logic_mode;
    DEBUG_PRINT("    is_logic_mode=%d\n", is_logic_mode);
    uint8_t total_bytes_per_sample = is_logic_mode ? 1 : active_params->num_enabled_channels;

    // Lets aim for a transfer time of 10ms by adjusting the chunk size
    chunk_size = active_params->realSampleRatePerChannel * total_bytes_per_sample * 0.01;
    // chunk_size = active_params->realSampleRatePerChannel * active_params->num_enabled_channels * 0.001;
    //#ifdef NDEBUG
    // fix above to use proper chunk size
    //    abort();
    //#endif

    // For ease of processing the chunk size is a multiple of the number of channels. This prevents multichannel samples
    // spanning more than one chunk
    chunk_size = (chunk_size / total_bytes_per_sample) * total_bytes_per_sample;

    if (chunk_size < total_bytes_per_sample) {
        chunk_size = total_bytes_per_sample;
    } else if (chunk_size > MAX_CHUNK_SIZE) {
        // does it really matter if the chunk size gets bigger than this? just need to make sure ring buffer is big enough.
        chunk_size = (MAX_CHUNK_SIZE / total_bytes_per_sample) * total_bytes_per_sample;
    }
    DEBUG_PRINT("    chunk_size=%d\n", chunk_size);
    assert(chunk_size > 0 && chunk_size <= MAX_CHUNK_SIZE);
    assert((chunk_size % total_bytes_per_sample) == 0);
    // assert(chunk_size < active_params->min_num_post_trigger_bytes); // to ensure trigger_addr chunk becomes unreserved (pio triggering)

    samples_per_chunk = chunk_size / total_bytes_per_sample;

    scoppy_uint8_chunked_ring_buffer_init(&ring_buf1, ring_buf1_arr + 1, sizeof(ring_buf1_arr) - 2, chunk_size);

    if (active_params->trigger_mode == TRIGGER_MODE_NONE) {
        max_trigger_chunks = -1;
    } else if (active_params->trigger_mode == TRIGGER_MODE_NORMAL) {
        max_trigger_chunks = INT32_MAX;
    } else if (active_params->trigger_mode == TRIGGER_MODE_AUTO) {
        //
        // Calculate how much time to wait (in chunks) before timing out when looking for a trigger when in auto trigger mode
        // Lets aim for 150ms. That leaves 50ms to do other stuff and still acheive 5 frames per second
        // time = num_bytes / total_sample_rate
        // num_bytes = time * total_sample_rate
        // num_chunks = time * total_sample_rate / chunk_size;
        // eg if sample_rate == 1000Hz and chunk_size == 10 then num_chunks = (0.150 * 1000)/10 = 15 chunks
        // To avoid floating point arithmetic the above can be written as: (15 * 1000)/(10 * 10)
        // ie. (15 * total_sample_rate)/(chunk_size * 10)
        //
        // NB. At low frequencies we might not get enough samples to detect a trigger point within .15ms. Though a period of
        // .15ms is less than 7 Hz so we'll be using continuous mode at that point I think.
        // If this is an issue we'll see normal mode failing.
        //
        max_trigger_chunks = (15U * active_params->realSampleRatePerChannel * total_bytes_per_sample) / ((uint32_t)chunk_size * 100U);
        if (max_trigger_chunks < 1) {
            max_trigger_chunks = 1;
        }
    } else {
        ERROR_PRINT("    invalid trigger mode=%d\n", (int)active_params->trigger_mode);
    }
    DEBUG_PRINT("    max_trigger_chunks=%ld\n", (long int)max_trigger_chunks);

    active_buffer->clear(active_buffer);

    init_dma_channel(dma_chan1, is_logic_mode);
    init_dma_channel(dma_chan2, is_logic_mode);

    // reserve space in the active buffer for the dma channels to write to
    reserved1 = active_buffer->reserve_chunk(active_buffer);
    dma_channel_set_write_addr(dma_chan1, reserved1, false);
    dma_channel_set_trans_count(dma_chan1, chunk_size, false);

    reserved2 = active_buffer->reserve_chunk(active_buffer);
    dma_channel_set_write_addr(dma_chan2, reserved2, false);
    dma_channel_set_trans_count(dma_chan2, chunk_size, false);

    {
        // chain channel1 to channel2
        dma_channel_config ch1_config = dma_get_channel_config(dma_chan1);
        channel_config_set_chain_to(&ch1_config, dma_chan2);
        dma_channel_set_config(dma_chan1, &ch1_config, false);
    }

    {
        // chain channel2 to channel1
        dma_channel_config ch2_config = dma_get_channel_config(dma_chan2);
        channel_config_set_chain_to(&ch2_config, dma_chan1);
        dma_channel_set_config(dma_chan2, &ch2_config, false);
    }

#ifndef NDEBUG
    // Used for checking that the dma int handler got called in time to change
    // the write address
    first_ch1_reserved_byte_value = *(active_buffer->next_chunk_addr);
    first_ch2_reserved_byte_value = 200; // this should be set when ch1 int handler is called
#endif

    if (is_logic_mode) {
        assert(scoppy_hardware_triggered == false);
        scoppy_pio_prestart(active_params);
        assert(scoppy_hardware_triggered == false);
    } else {
        adc_fifo_setup(true,  // Write each completed conversion to the sample FIFO
                       true,  // Enable DMA data request (DREQ)
                       1,     // DREQ (and IRQ) asserted when at least 1 sample present
                       false, // We won't see the ERR bit because of 8 bit reads; disable.
                       true   // Shift each sample to 8 bits when pushing to FIFO
        );

        // DEBUG_PRINT("    adc_set_clkdiv: %lu\n", (unsigned long)active_params->clkdivint);
        // //sleep_ms(50);
        adc_set_clkdiv((float)active_params->clkdivint);
        // DEBUG_PRINT("    adc_hw->div: %lx, DIV.INT: %lu\n", (unsigned long)adc_hw->div, (unsigned long)((adc_hw->div >> 8) & 0xFFFF));

        // DEBUG_PRINT("    adc_set_round_robin: 0x%0lX, num_enabled=%u\n", (unsigned long)active_params->enabled_channels,
        // (unsigned)active_params->num_enabled_channels);
        // //sleep_ms(50);

        // adc_set_round_robin(active_params->enabled_channels); // bug in param check code means we can't call this when PARAM_ASSERTIONS_ENABLE_ALL is defined
        uint input_mask = active_params->enabled_channels;
        // DEBUG_PRINT("  RROBIN MASK=0x%X\n", (unsigned)input_mask);
        invalid_params_if(ADC, (input_mask << ADC_CS_RROBIN_LSB) & ~ADC_CS_RROBIN_BITS);
        hw_write_masked(&adc_hw->cs, input_mask << ADC_CS_RROBIN_LSB, ADC_CS_RROBIN_BITS);
    }

    // the dma_chan2 will start automatically when dma_chan1 finishes. The chan1 interrupt
    // will then be called.
    //  then
    // the dma_chan1 will start automatically when dma_chan2 finishes. The chan2 interrupt
    // will then be called.
    //  ... ad infinitem...
    dma_channel_start(dma_chan1);

    if (is_logic_mode) {
        assert(scoppy_hardware_triggered == false);
        scoppy_pio_start();
        assert(scoppy_hardware_triggered == false);
    } else {
        adc_run(true);
    }
}

static inline void cancel_chain_to(uint dma_channel) {
    dma_channel_config config = dma_get_channel_config(dma_channel);
    channel_config_set_chain_to(&config, dma_channel);
    dma_channel_set_config(dma_channel, &config, false);
}

void pico_scoppy_stop_non_continuous_sampling() {
    DEBUG_PRINT("  pico_scoppy_stop_non_continuous_sampling()\n");

    cancel_chain_to(dma_chan1);
    cancel_chain_to(dma_chan2);

    dma_channel_wait_for_finish_blocking(dma_chan1);
    dma_channel_wait_for_finish_blocking(dma_chan2);

    scoppy_pio_stop();
}

void pico_scoppy_non_continuous_sampling_init() {
    DEBUG_PRINT("  pico_scoppy_non_continuous_sampling_init()\n");

    // don't use the first and last bytes of the arrays. We can check these for
    // illegal writes outside of the buffer
    ring_buf1_arr[0] = 101;
    ring_buf1_arr[sizeof(ring_buf1_arr) - 1] = 102;
    rubbish_buf[0] = 103;
    rubbish_buf[RUBBISH_SIZE] = 104;

    queue_init(&trigger_chunk_queue, sizeof(uint8_t *), 100);

    // Set up the DMA to start transferring data as soon as it appears in FIFO
    dma_chan1 = dma_claim_unused_channel(true);
    DEBUG_PRINT("    dma_chan1=%u\n", dma_chan1);

    dma_chan2 = dma_claim_unused_channel(true);
    DEBUG_PRINT("    dma_chan2=%u\n", dma_chan2);

    // Grant high bus priority to the DMA, so it can shove the processors out
    // of the way. This should only be needed if you are pushing things up to
    // >16bits/clk here, i.e. if you need to saturate the bus completely.
    // bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;

    scoppy_pio_init();
}
