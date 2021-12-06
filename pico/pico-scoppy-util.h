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

// https://stackoverflow.com/questions/1644868/define-macro-for-debug-printing-in-c
// https://stackoverflow.com/questions/1941307/debug-print-macro-in-c
#ifndef NDEBUG
#define DEBUG_PRINT(fmt, args...) printf(fmt, ##args)
#define DEBUG_PUTS(arg) puts(arg)
#else
#define DEBUG_PRINT(fmt, args...)
#define DEBUG_PUTS(arg)
#endif

// For log messages that it is OK for the user to see
#define LOG_PRINT(fmt, args...) printf(fmt, ##args)

// For error messages that it is OK for the user to see
#define ERROR_PRINT(fmt, args...) \
    printf("Error: ");            \
    printf(fmt, ##args)

void pico_scoppy_seed_random();