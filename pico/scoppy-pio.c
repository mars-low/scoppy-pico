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
//
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "pico/time.h"
//
#include "pico-scoppy-non-cont-sampling.h"
#include "pico-scoppy-triggering.pio.h"
#include "scoppy-pio.h"

// All state machines are on the same pio so that they can all be started at the same time
static PIO pio = pio0;

// StateMachine used for sampling
static uint sampling_sm = SAMPLING_SM;
static uint sampling_pin_base = 6;
static uint sampling_pin_count = 8;
static uint sampling_program_offset = 999;
pio_sm_config sampling_sm_config;

// StateMachine used for triggering
static uint triggering_sm = TRIGGER_MAIN_SM;
static uint triggering_pin_base = 6;
static uint triggering_pin_count = 8;

static uint rising_edge_trigger_program_offset = 999;
pio_sm_config rising_edge_trigger_sm_config;
static uint falling_edge_trigger_program_offset = 999;
pio_sm_config falling_edge_trigger_sm_config;

static uint all_sms[] = {SAMPLING_SM, TRIGGER_MAIN_SM};
static uint num_sms = (sizeof(all_sms) / sizeof(all_sms[0]));

static uint32_t sm_mask = 0x0003;

volatile bool scoppy_hardware_triggered = false;

static void pio0_irq0_handler() {
    // printf("pio0_irq0_handler() - 9, irq=%08lx, irqf=%08lx\n", pio0->irq, pio0->irq_force);

    g_hw_trig_dma1_write_addr = (uint8_t *)dma_channel_hw_addr(dma_chan1)->write_addr;
    g_hw_trig_dma2_write_addr = (uint8_t *)dma_channel_hw_addr(dma_chan2)->write_addr;
    g_hw_trig_dma1_trans_count = dma_channel_hw_addr(dma_chan1)->transfer_count;
    g_hw_trig_dma2_trans_count = dma_channel_hw_addr(dma_chan2)->transfer_count;

    scoppy_hardware_triggered = true;
    pio_interrupt_clear(pio, 0);
}

static void load_sampling_program() {

    // Load a program to capture n pins. This is just a single `in pins, n`
    // instruction with a wrap.
    uint16_t capture_prog_instr = pio_encode_in(pio_pins, sampling_pin_count);
    struct pio_program capture_prog = {.instructions = &capture_prog_instr, .length = 1, .origin = -1};
    sampling_program_offset = pio_add_program(pio, &capture_prog);

    // Configure state machine to loop over this `in` instruction forever,
    // with autopush enabled.
    sampling_sm_config = pio_get_default_sm_config();
    sm_config_set_in_pins(&sampling_sm_config, sampling_pin_base);

    // TODO: assert that the sm is not enabled (this is a requirement of pio_sm_set_consecutive_pindirs)
    // pio_sm_set_consecutive_pindirs(pio, sampling_sm, sampling_pin_base, sampling_pin_count, false /* is_out */); // unnecessary - I think in is default
    // anyway
    sm_config_set_wrap(&sampling_sm_config, sampling_program_offset, sampling_program_offset);
    sm_config_set_clkdiv_int_frac(&sampling_sm_config, 30000, 0);

    // right-justified in the FIFO in this case, with some zeroes at the MSBs
    sm_config_set_in_shift(&sampling_sm_config, false /* shift right */, true /* autopush */, sampling_pin_count /* push threshold */);
    // make the RX fifo bigger - 'cos we can
    sm_config_set_fifo_join(&sampling_sm_config, PIO_FIFO_JOIN_RX);
}

static void load_triggering_program() {

    // Rising edge trigger

    rising_edge_trigger_program_offset = pio_add_program(pio, &pico_scoppy_rising_edge_trigger_program);
    rising_edge_trigger_sm_config = pico_scoppy_rising_edge_trigger_program_get_default_config(rising_edge_trigger_program_offset);
    sm_config_set_in_pins(&rising_edge_trigger_sm_config, triggering_pin_base);

    // Falling edge trigger
    falling_edge_trigger_program_offset = pio_add_program(pio, &pico_scoppy_falling_edge_trigger_program);
    falling_edge_trigger_sm_config = pico_scoppy_rising_edge_trigger_program_get_default_config(falling_edge_trigger_program_offset);
    sm_config_set_in_pins(&falling_edge_trigger_sm_config, triggering_pin_base);

    // =====================
    // Some initialisation that doesn't depend on the triggering program in use
    // TODO: assert that the sm is not enabled (this is a requirement of pio_sm_set_consecutive_pindirs)
    pio_sm_set_consecutive_pindirs(pio, triggering_sm, triggering_pin_count, triggering_pin_count, false /* is_out */);
    //sm_config_set_clkdiv_int_frac(&main_trigger_sm_config, 30000, 0);

    //------------------------
    // right-justified in the FIFO in this case, with some zeroes at the MSBs
    // Not necessary - we don't use the ISR
    // sm_config_set_in_shift(&c, false /* shift right */, true /* autopush */, triggering_pin_count /* push threshold */);
    // make the RX fifo bigger - 'cos we can
    // sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
}

#ifdef PIO_TRIGGER_TEST_PROGRAM_ENABLE
static void load_triggering_test_program() {
    uint offset = pio_add_program(pio, &pico_scoppy_triggerer_test_program);

    pio_sm_config c = pico_scoppy_triggerer_test_program_get_default_config(offset);
    sm_config_set_in_pins(&c, triggering_pin_base);

    // TODO: assert that the sm is not enabled (this is a requirement of pio_sm_set_consecutive_pindirs)
    pio_sm_set_consecutive_pindirs(pio, triggering_sm, triggering_pin_count, triggering_pin_count, false /* is_out */);
    sm_config_set_clkdiv_int_frac(&c, 30000, 0);

    // right-justified in the FIFO in this case, with some zeroes at the MSBs
    // Not necessary - we don't use the ISR
    // sm_config_set_in_shift(&c, false /* shift right */, true /* autopush */, triggering_pin_count /* push threshold */);
    // make the RX fifo bigger - 'cos we can
    // sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
    pio_sm_init(pio, triggering_sm, offset, &c);
}
#endif

const volatile void *scoppy_pio_get_dma_read_addr() { return &pio->rxf[sampling_sm]; }

static uint get_dreq(PIO pio, uint sm) {
    assert(pio == pio0);
    switch (sm) {
    case 0:
        return DREQ_PIO0_RX0;
    case 1:
        return DREQ_PIO0_RX1;
    case 2:
        return DREQ_PIO0_RX2;
    case 3:
        return DREQ_PIO0_RX3;
    default:
        assert(false);
        return DREQ_PIO0_RX0;
    }
}

uint scoppy_pio_get_dreq() { return get_dreq(pio, sampling_sm); }

void scoppy_pio_stop() {
    scoppy_hardware_triggered = false;
    for (int sm = 0; sm < num_sms; sm++) {
        pio_sm_set_enabled(pio, sm, false);
    }
    assert(scoppy_hardware_triggered == false);
}

void scoppy_pio_start() {
    scoppy_hardware_triggered = false;
    assert(pio_sm_is_tx_fifo_empty(pio, TRIGGER_MAIN_SM));
    assert(scoppy_hardware_triggered == false);
    pio_enable_sm_mask_in_sync(pio, sm_mask);
    sleep_ms(100);
    assert(scoppy_hardware_triggered == false);
    // pio_sm_set_enabled(pio, sampling_sm, true);
}

static uint get_trigger_program_offset(struct sampling_params *params) {
    if (params->trigger_type == TRIGGER_TYPE_FALLING_EDGE) {
        return falling_edge_trigger_program_offset;
    }
    else {
        assert(params->trigger_type == TRIGGER_TYPE_RISING_EDGE);
        return rising_edge_trigger_program_offset;
    }
}

static pio_sm_config *get_trigger_config(struct sampling_params *params) {
    if (params->trigger_type == TRIGGER_TYPE_FALLING_EDGE) {
        return &falling_edge_trigger_sm_config;
    }
    else {
        assert(params->trigger_type == TRIGGER_TYPE_RISING_EDGE);
        return &rising_edge_trigger_sm_config;
    }
}

void scoppy_pio_arm_trigger() {
    pio_sm_put(pio0, TRIGGER_MAIN_SM, 1u);
}

void scoppy_pio_disarm_trigger(struct sampling_params *params) {
    //
    // Get the trigger pio into a state where it is waiting for the next write to the fifo
    //

    pio_sm_clear_fifos(pio0, TRIGGER_MAIN_SM);

    uint trigger_program_offset = get_trigger_program_offset(params);
    pio_sm_exec(pio, TRIGGER_MAIN_SM, pio_encode_jmp(trigger_program_offset));
}

void scoppy_pio_prestart(struct sampling_params *params) {

    assert(params->clkdivint <= UINT16_MAX); // casting from uint32_t to uint16_t

    uint trigger_program_offset = get_trigger_program_offset(params);
    pio_sm_config *trigger_config = get_trigger_config(params);

    uint trigger_gpio = triggering_pin_base + scoppy.app.trigger_channel;
    sm_config_set_jmp_pin(trigger_config, trigger_gpio);

    sm_config_set_clkdiv_int_frac(&sampling_sm_config, params->clkdivint, 0);
    sm_config_set_clkdiv_int_frac(trigger_config, params->clkdivint, 0);

    // Does lots of re-initialisation including setting the PC back to the start of the program
    pio_sm_init(pio, sampling_sm, sampling_program_offset, &sampling_sm_config);
    pio_sm_init(pio, triggering_sm, trigger_program_offset, trigger_config);
}

void scoppy_pio_init() {

    for (uint gpio = sampling_pin_base; gpio < sampling_pin_base + sampling_pin_count; gpio++) {
        // Only required for output
        // pio_gpio_init(pio, sampling_pin_base + i);

        // adc_init does this - is it required for digital pins? it actually clears output disable which i'm
        // not sure is what we want
        // We don't enable output so we probably dont need to make the output driver hi-Z
        //
        // Select NULL function to make output driver hi-Z
        // gpio_set_function(gpio, GPIO_FUNC_NULL);

        // The default is pulldown - results in input impedance that is way too low ~50k
        gpio_disable_pulls(gpio);
    }

    irq_set_exclusive_handler(PIO0_IRQ_0, &pio0_irq0_handler);
    irq_set_enabled(PIO0_IRQ_0, true);

    // interrupt 0 from the pio (pio0 here), will be routed to the PIO0_IRQ_0 handler
    pio_set_irq0_source_enabled(pio, pis_interrupt0, true);

    load_sampling_program();
    load_triggering_program();
    // load_triggering_test_program();
}
