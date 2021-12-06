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

float scoppy_float_from_4_network_bytes(const void *buf);

uint64_t scoppy_uint64_from_8_network_bytes(const void *buf);

int32_t scoppy_int32_from_4_network_bytes(const void *buf);
uint32_t scoppy_uint32_from_4_network_bytes(const void *buf);

int16_t scoppy_int16_from_2_network_bytes(const void *buf);
uint16_t scoppy_uint16_from_2_network_bytes(const void *buf);
uint8_t scoppy_uint8_from_1_network_byte(const void *buf);

void scoppy_uint32_to_4_network_bytes(void *buf, uint32_t value);
void scoppy_int32_to_4_network_bytes(void *buf, int32_t value);
void scoppy_uint16_to_2_network_bytes(void *buf, uint16_t value);