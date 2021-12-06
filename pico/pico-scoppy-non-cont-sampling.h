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

//
#include "scoppy-context.h"
#include "scoppy-outgoing.h"

void pico_scoppy_non_continuous_sampling_init();
void pico_scoppy_start_non_continuous_sampling();
void pico_scoppy_get_non_continuous_samples(struct scoppy_context *ctx);
void pico_scoppy_stop_non_continuous_sampling();

extern uint dma_chan1;
extern uint dma_chan2;

extern uint8_t *g_hw_trig_dma1_write_addr;
extern uint8_t *g_hw_trig_dma2_write_addr;
extern uint32_t g_hw_trig_dma1_trans_count;
extern uint32_t g_hw_trig_dma2_trans_count;

