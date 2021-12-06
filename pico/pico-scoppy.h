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

// PWM signal generator output gpio is configured in CMakeLists.txt
// default is 22
#if SIG_GEN_PWM_GPIO <= 1
    #error invalid SIG_GEN_PWM_GPIO - conflict with stdio uart
#endif

#if SIG_GEN_PWM_GPIO >= 6 && SIG_GEN_PWM_GPIO <= 13
    #error invalid SIG_GEN_PWM_GPIO - conflict with logic analyzer gpio
#endif

#if SIG_GEN_PWM_GPIO >= 26 && SIG_GEN_PWM_GPIO <= 27
    #error invalid SIG_GEN_PWM_GPIO - conflict with adc gpio
#endif

// Input pins used to deterimine the currently selected voltage range
// The first pin can be configured in CMakeLists.txt and the rest are assumed
// to be on consecutive pins
// default is 2
#if VOLTAGE_RANGE_START_GPIO <= 1
    #error invalid VOLTAGE_RANGE_START_GPIO - conflict with stdio uart
#endif

#if VOLTAGE_RANGE_START_GPIO >= 3 && VOLTAGE_RANGE_START_GPIO <= 13
    #error invalid VOLTAGE_RANGE_START_GPIO - conflict with logic analyzer gpio
#endif

#if VOLTAGE_RANGE_START_GPIO >= 23 && VOLTAGE_RANGE_START_GPIO >= 27
    #error invalid VOLTAGE_RANGE_START_GPIO - conflict with adc gpio
#endif

#if VOLTAGE_RANGE_START_GPIO >= (SIG_GEN_PWM_GPIO - 3) && VOLTAGE_RANGE_START_GPIO <= SIG_GEN_PWM_GPIO
    #error invalid VOLTAGE_RANGE_START_GPIO - conflict with signal generator output
#endif

#define VOLTAGE_RANGE_PIN_CH_0_BIT_1 (VOLTAGE_RANGE_START_GPIO)
#define VOLTAGE_RANGE_PIN_CH_0_BIT_0 (VOLTAGE_RANGE_START_GPIO + 1)

#define VOLTAGE_RANGE_PIN_CH_1_BIT_1 (VOLTAGE_RANGE_START_GPIO + 2)
#define VOLTAGE_RANGE_PIN_CH_1_BIT_0 (VOLTAGE_RANGE_START_GPIO + 3)

#define LED_PIN 25

#define MULTICORE_MSG_NONE 0
#define MULTICORE_MSG_RESTART_REQUIRED 1
#define MULTICORE_MSG_SAMPLING_STOPPED 2
#define MULTICORE_MSG_RESTART_SAMPLING 3
