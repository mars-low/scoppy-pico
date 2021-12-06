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

#include "hardware/pio.h"
#include "pico-scoppy-samples.h"

#define SAMPLING_SM 0u

// The main trigger sm is the one that asserts the irq
// one shot
#define TRIGGER_MAIN_SM 1u

// Trigger condition state machines
// continuous
#define TRIGGER_COND1_SM 2u
#define TRIGGER_COND2_SM 2u

void scoppy_pio_arm_trigger();
void scoppy_pio_disarm_trigger(struct sampling_params *params);
void scoppy_pio_init();
void scoppy_pio_prestart(struct sampling_params *params);
void scoppy_pio_start();
void scoppy_pio_stop();
const volatile void *scoppy_pio_get_dma_read_addr();
uint scoppy_pio_get_dreq();

extern volatile bool scoppy_hardware_triggered;
