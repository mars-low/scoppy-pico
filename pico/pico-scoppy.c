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

//
#include <stdio.h>
//
#include "hardware/clocks.h"
#include "hardware/pwm.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
//
#include "pico-scoppy-ctx.h"
#include "pico-scoppy-pwm-sig-gen.h"
#include "pico-scoppy-samples.h"
#include "pico-scoppy-util.h"
#include "pico-scoppy.h"
#include "scoppy_usb.h"


int main() {

    sleep_ms(200);

    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 1);

    // voltage range pins
    for (uint i = VOLTAGE_RANGE_PIN_CH_0_BIT_1; i <= VOLTAGE_RANGE_PIN_CH_1_BIT_0; i++) {
        gpio_init(i);
        gpio_set_dir(i, GPIO_IN);
        gpio_pull_down(i);
    }

    LOG_PRINT("Initialising stdio\n");
    stdio_init_all();

    LOG_PRINT("Initialising USB\n");
    scoppy_usb_init();

    struct scoppy_context *ctx = pico_scoppy_get_context();

    LOG_PRINT("Initialising ADC\n");
    pico_scoppy_init_samplers();

    LOG_PRINT("Starting PWM\n");
    pwm_sig_gen_init();

    LOG_PRINT("Starting\n");
    gpio_put(LED_PIN, 0);
    sleep_ms(100);

    DEBUG_PRINT("... launching core1\n");
    multicore_launch_core1(pico_scoppy_sampling_loop);
    multicore_fifo_push_blocking((uint32_t)ctx);

    DEBUG_PRINT("... starting scoppy_main\n");
    scoppy_main(ctx);

    return 0;
}
