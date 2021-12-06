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

#include <math.h>
#include <stdio.h>
//
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/pwm.h"
#include "pico/stdlib.h"
//
#include "pico-scoppy-pwm-sig-gen.h"
#include "pico-scoppy-util.h"

// Pins need to be on different slices to be able to set different
// PWM frequencies

//#define PWM_OUT_PIN_2 18

static const uint8_t PWM_FUNC_NONE = 0;
static const uint8_t PWM_FUNC_SQUARE = 1;
static const uint8_t PWM_FUNC_SINE = 2;

int dma_cc_chan;
int dma_control_chan;

#define samples_per_period 250
static uint16_t levels[samples_per_period];
static uint16_t *levels_addr = levels;
static bool is_dma_configured = false;

// TOP = 16bit
// Divider = 8 bit
// clock = 125mHz

// For a 50% duty cycle, level should be set to TOP+1/2
//
// Period in clockcycles is
// period = TOP+1 * DIV_INT
// TOP+1 = period / DIV_INT
//
// To maximise TOP+1 - and thus make the duty cycle more precise - we want DIV_INT to be the lowest
// value that doesn't overflow top:
// So calculate: DIV_INT = period / UINT_MAX-1
// eg. for period = 32
// DIV_INT = 0.nnn (so round up to 1) => TOP+1 == 32
//
// eg. for period = 2^16+32 (would overflow TOP at DIV_INT==1)
// DIV_INT = 1.nnn (so round up to 2)
// => TOP+1 == 2^16+32/2 == 32784
//
// eg. for period = (2^16 * 2)-32
// DIV_INT = 1.nnn (so round up to 2)
// => TOP+1 ==  (2^16 * 2)-32 / 2  == 65520

// freq = fsys/period
// period = fsys/freq

//	Freq	  Period	DIV_INT	    CEIL(DIV_INT)	TOP+1	    TOP	        50% duty
//	1	      125000000	1907.37774	1908	        65513.63	65512.627	32756.813
//	10	      12500000	190.73777	191	            65445.03	65444.026	32722.513
//	100	      1250000	19.07378	20	            62500	    62499	    31250
//	1000	  125000	1.90738	    2	            62500	    62499	    31250
//	10000	  12500	    0.19074	    1	            12500	    12499	    6250
//	100000	  1250	    0.01907 	1	            1250	    1249	    625
//	500000	  250	    0.00381	    1	            250	        249	        125
//	1000000	  125	    0.00191	    1	            125	        124	        62.5
//	1250000	  100	    0.00153	    1	            100	        99	        50
//	3125000	  40	    0.00061	    1	            40	        39	        20
//	3906250	  32	    0.00049	    1	            32	        31	        16
//	5000000	  25	    0.00038	    1	            25	        24	        12.5
//	6250000	  20	    0.00031	    1	            20	        19	        10
//	7812500	  16	    0.00024	    1	            16	        15	        8
//	12500000  10	    0.00015	    1	            10	        9	        5
//	15625000  8	        0.00012	    1	            8	        7	        4
//
// Above:
// DIV_INT = Period/(UINT_MAX-1)
// TOP + 1 = Period/CEIL(DIV_INT)
// 50% Dury = TOP+1/2

static void init_sine_wave(uint max_level) {
    for (int i = 0; i < samples_per_period; i++) {
        // levels[i] = 125;
        double two_pi_radians = M_PI * 2;
        double angle = two_pi_radians * i / samples_per_period;

        // sine will be between -1 and 1
        double sine = sin(angle);

        // normalise sine to be between 0 and 1
        sine = (sine + 1.0) / 2;

        // levels[i] will be between -max_level and max_level
        levels[i] = sine * max_level;
        //printf("%d, %lf, %lf, %d\n", i, angle, sine, (int)levels[i]);
    }
}

// cc = counter compare
// this channel updates the counter compare register
static void configure_cc_channel(uint gpio) {
    uint slice_num = pwm_gpio_to_slice_num(gpio);
    dma_channel_config chan_config = dma_channel_get_default_config(dma_cc_chan);

    channel_config_set_transfer_data_size(&chan_config, DMA_SIZE_16);
    channel_config_set_read_increment(&chan_config, true);
    channel_config_set_write_increment(&chan_config, false);
    channel_config_set_dreq(&chan_config, DREQ_PWM_WRAP0 + slice_num);
    channel_config_set_chain_to(&chan_config, dma_control_chan);

    // Don't generate IRQs that we don't care about??????
    channel_config_set_irq_quiet(&chan_config, true);

    dma_channel_configure(dma_cc_chan,                  // Channel to be configured
                          &chan_config,                 // The configuration we just created
                          &pwm_hw->slice[slice_num].cc, // The initial write address
                          levels,                       // The initial read address
                          samples_per_period,           // Number of transfers
                          false                         // Start immediately.
    );
}

// this channel updates the first channel's read address and triggers the
// first channel to start another transfer
static void configure_control_channel() {
    dma_channel_config chan_config = dma_channel_get_default_config(dma_control_chan);

    channel_config_set_transfer_data_size(&chan_config, DMA_SIZE_32);
    channel_config_set_read_increment(&chan_config, false);
    channel_config_set_write_increment(&chan_config, false);

    // Don't generate IRQs that we don't care about??????
    channel_config_set_irq_quiet(&chan_config, true);

    dma_channel_configure(dma_control_chan, // Channel to be configured
                          &chan_config,     // The configuration we just created

                          // The first channel's read addr (this alias has the read addr as the trigger register)
                          &dma_hw->ch[dma_cc_chan].al3_read_addr_trig,

                          &levels_addr, // The initial read address
                          1,            // Transfer a single 32 bit word
                          false         // Start immediately.
    );
}

static void configure_dma(uint gpio) {
    configure_cc_channel(gpio);
    configure_control_channel();
}

static void generate_sine_wave(uint gpio) {
    DEBUG_PRINT("generate_sine_wave: gpio=%u\n");

    // With samples_per_period of 250 and top+1 of 250, a div_int of 2 will give a waveform frequency of 1kHz
    static const uint div_int = 2u;

    //static const uint div_int = 255u; // for when debugging
    static const uint max_level = 250; // A duty cycle of 100% - a level of 0 is a duty cycle of 0%
    static const uint top = max_level - 1;

    uint slice_num = pwm_gpio_to_slice_num(gpio);
    pwm_set_clkdiv_int_frac(slice_num, div_int, 0u);
    pwm_set_wrap(slice_num, top);

    // Set channel A output low to start with
    pwm_set_chan_level(slice_num, PWM_CHAN_A, 0);

    // Set the PWM running. The DMA transfer will update the levels.
    pwm_set_enabled(slice_num, true);

    // Now the DMA stuff
    if (!is_dma_configured) {
        configure_dma(gpio);
        init_sine_wave(max_level);
        is_dma_configured = true;
    } else {
        // re-enable chain_to (which may have been disabled by the pwm_sig_gen_reset function)
        dma_channel_config chan_config = dma_get_channel_config(dma_cc_chan);
        channel_config_set_chain_to(&chan_config, dma_control_chan);
        dma_channel_set_config(dma_cc_chan, &chan_config, false);
    }
    dma_channel_start(dma_cc_chan);
}

static void generate_square_wave(uint gpio, uint32_t freq_hz, uint16_t duty_per_cent) {
    DEBUG_PRINT("generate_square_wave: gpio=%u, freq=%lu, duty=%lu\n", (unsigned)gpio, (unsigned long)freq_hz, (unsigned long)duty_per_cent);

    uint32_t clk_freq = clock_get_hz(clk_sys);

    // See: https://www.raspberrypi.org/forums/viewtopic.php?f=144&t=317593

    // We want to keep DIV_INT at low as possible so the the duty cycle can be as precise
    // as possible
    // A a general formula for calulating DIV_INT for a given frequency (freq) is:
    // DIV_INT = ceil(period/(UINT_MAX-1))
    // where period (in clock cycles for freq) = clocksys/freq
    uint16_t div_int = clk_freq / (freq_hz * (UINT16_MAX - 1));
    div_int++;

    uint32_t top = (clk_freq / (freq_hz * div_int)) - 1;
    uint32_t level = ((top + 1UL) * duty_per_cent) / 100UL;

    DEBUG_PRINT("  div_int=%u, top=%lu, level=%lu\n", (unsigned)div_int, (unsigned long)top, (unsigned long)level);

    uint slice_num = pwm_gpio_to_slice_num(gpio);
    pwm_set_clkdiv_int_frac(slice_num, div_int, 0);
    pwm_set_wrap(slice_num, top);
    pwm_set_gpio_level(gpio, level);
    pwm_set_enabled(slice_num, true);
}

static void pwm_sig_gen_reset(uint gpio) {
    // Set channel A output low
    uint slice_num = pwm_gpio_to_slice_num(gpio);
    pwm_set_chan_level(slice_num, PWM_CHAN_A, 0);

    // Stop the PWM running
    pwm_set_enabled(slice_num, false);

    if (is_dma_configured) {
        dma_channel_config chan_config = dma_get_channel_config(dma_cc_chan);
        // stop chain_to by chaining to itself
        channel_config_set_chain_to(&chan_config, dma_cc_chan);
        dma_channel_abort(dma_control_chan);
        dma_channel_abort(dma_cc_chan);

        // No need to do this because dma_channel_abort will not return until any in-flight transfers have
        // completed. In fact if we call these functions they can???? back indefinitely for some reason.
        //dma_channel_wait_for_finish_blocking(dma_cc_chan);
        //dma_channel_wait_for_finish_blocking(dma_control_chan);
    }
}

void pwm_sig_gen(uint8_t function, unsigned gpio, uint32_t freq, uint16_t duty) {

    // 255 == the default pwm gpio
    if (gpio == 255) {
        gpio = SIG_GEN_PWM_GPIO;
    } else if (gpio > 30) {
        LOG_PRINT("Invalid pwm gpio: %u\n", (unsigned)gpio);
        return;
    }

    pwm_sig_gen_reset(gpio);

    switch (function) {
    case PWM_FUNC_NONE:
        pwm_sig_gen_reset(gpio);
        break;
    case PWM_FUNC_SQUARE:
        generate_square_wave(gpio, freq, duty);
        break;
    case PWM_FUNC_SINE:
        generate_sine_wave(gpio);
        break;
    default:
        break;
    }
}

void pwm_sig_gen_init() {
    unsigned gpio = SIG_GEN_PWM_GPIO;
    gpio_set_function(gpio, GPIO_FUNC_PWM);

    // Get free channels, panic() if there are none
    dma_cc_chan = dma_claim_unused_channel(true);
    dma_control_chan = dma_claim_unused_channel(true);

    pwm_sig_gen(PWM_FUNC_SQUARE, gpio, 1000U, 50U);
}
