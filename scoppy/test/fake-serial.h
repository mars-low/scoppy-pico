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

#ifndef __SCOPPY_FAKE_SERIAL_H__
#define __SCOPPY_FAKE_SERIAL_H__

#include <stdint.h>

int fake_serial_read(uint8_t *buf, int offset, int count);
void fake_serial_set_max_read_count(int count);
void fake_serial_set_data(uint8_t *data, int len);

int fake_serial_write(uint8_t *buf, int offset, int count);

#endif // __SCOPPY_FAKE_SERIAL_H__