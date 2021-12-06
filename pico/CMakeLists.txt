cmake_minimum_required(VERSION 3.12)

#set(PICO_PLATFORM host)
include(pico_sdk_import.cmake)

##########################################
# 
# You can set custom values for some cmake variables in scoppy-local.cmake
# That file is not tracked by git and so won't cause problems when you pull the
# latest version of scoppy from github.
#

if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/scoppy-local.cmake")
    include("${CMAKE_CURRENT_SOURCE_DIR}/scoppy-local.cmake")
endif()

if(NOT DEFINED SIG_GEN_PWM_GPIO)
    # PWM signal generator output
    set(SIG_GEN_PWM_GPIO 22)
endif()

if(NOT DEFINED VOLTAGE_RANGE_START_GPIO)
    # There are 4 adjacent gpio pins used for the voltage range input.
    set(VOLTAGE_RANGE_START_GPIO 2)
endif()

# The BUILD_NUMBER is sent to the UI.
if(NOT DEFINED PICO_SCOPPY_BUILD_NUMBER)
    set(PICO_SCOPPY_BUILD_NUMBER "0")
endif()

#
# end customisation
#
##########################################

set(PICO_SCOPPY_VERSION "8")
set(SCOPPY_TARGET "scoppy-pico-v${PICO_SCOPPY_VERSION}")

project(scoppy)

pico_sdk_init()

pico_add_subdirectory(scoppy-usb)
add_subdirectory(../scoppy/lib scoppy-lib)

# applies to this directory and below
#add_compile_options(-Wall -Werror -Wno-error=unused-variable -Werror=unused-but-set-variable -DPICO_SCOPPY_VERSION=${PICO_SCOPPY_VERSION})

add_compile_definitions(
    PICO_SCOPPY_VERSION=${PICO_SCOPPY_VERSION}
    PICO_SCOPPY_BUILD_NUMBER=${PICO_SCOPPY_BUILD_NUMBER}
    VOLTAGE_RANGE_START_GPIO=${VOLTAGE_RANGE_START_GPIO}
    SIG_GEN_PWM_GPIO=${SIG_GEN_PWM_GPIO}
    )

add_executable(${SCOPPY_TARGET})

pico_generate_pio_header(${SCOPPY_TARGET} ${CMAKE_CURRENT_LIST_DIR}/pico-scoppy-triggering.pio)

target_sources(${SCOPPY_TARGET} PRIVATE
    pico-scoppy.c
    pico-scoppy.h
    pico-scoppy-core0-looper.c
    pico-scoppy-core0-looper.h
    pico-scoppy-ctx.c
    pico-scoppy-ctx.h
    pico-scoppy-cont-sampling.c
    pico-scoppy-cont-sampling.h
    pico-scoppy-non-cont-sampling.h
    pico-scoppy-non-cont-sampling.c
    pico-scoppy-samples.c
    pico-scoppy-samples.h
    pico-scoppy-pwm-sig-gen.c
    pico-scoppy-pwm-sig-gen.h
    pico-scoppy-util.c
    pico-scoppy-util.h
    scoppy-pio.c
    scoppy-pio.h
)

# https://gcc.gnu.org/onlinedocs/gcc/Warning-Options.html
# NB. This applies the options to the whole SDK

set_property(DIRECTORY APPEND PROPERTY COMPILE_DEFINITIONS
    $<$<CONFIG:Debug>:PARAM_ASSERTIONS_ENABLE_ALL=1>
  )

target_link_libraries(${SCOPPY_TARGET} PRIVATE
    pico_bootsel_via_double_reset    
    pico_multicore
    pico_stdlib
    pico_unique_id
    hardware_adc
    hardware_clocks
    hardware_dma
    hardware_irq
    hardware_pio
    hardware_pwm
    scoppy_usb
    scoppy-libs
)

# Only set this for DEBUG builds!
#pico_set_binary_type(${SCOPPY_TARGET} no_flash)

# create map/bin/hex file etc.
pico_add_extra_outputs(${SCOPPY_TARGET})
