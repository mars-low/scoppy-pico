/*
 * Copyright (C) 2021 FHDM Apps <scoppy@fhdm.xyz>
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _SCOPPY_USB_H
#define _SCOPPY_USB_H

/*
 *
 *  Linking this library or calling `pico_enable_scoppy_usb(TARGET)` in the CMake (which
 *  achieves the same thing) will add USB CDC to the drivers used for standard output
 *
 *  Note this library is a developer convenience. It is not applicable in all cases; for one it takes full control of the USB device precluding your
 *  use of the USB in device or host mode. For this reason, this library will automatically disengage if you try to using it alongside tinyusb_device or
 *  tinyusb_host. It also takes control of a lower level IRQ and sets up a periodic background task.
 */

// PICO_CONFIG: SCOPPY_USB_DEFAULT_CRLF, Default state of CR/LF translation for USB output, type=bool, default=SCOPPY_DEFAULT_CRLF, group=pico_scoppy_usb
#ifndef SCOPPY_USB_DEFAULT_CRLF
#define SCOPPY_USB_DEFAULT_CRLF SCOPPY_DEFAULT_CRLF
#endif

// PICO_CONFIG: SCOPPY_USB_STDOUT_TIMEOUT_US, Number of microseconds to be blocked trying to write USB output before assuming the host has disappeared and discarding data, default=500000, group=pico_scoppy_usb
#ifndef SCOPPY_USB_STDOUT_TIMEOUT_US
//#define SCOPPY_USB_STDOUT_TIMEOUT_US 500000
// scoppy - 2 seconds
#define SCOPPY_USB_STDOUT_TIMEOUT_US 2000000
#endif

// todo perhaps unnecessarily high?
// PICO_CONFIG: SCOPPY_USB_TASK_INTERVAL_US, Period of microseconds between calling tud_task in the background, default=1000, advanced=true, group=pico_scoppy_usb
#ifndef SCOPPY_USB_TASK_INTERVAL_US
#define SCOPPY_USB_TASK_INTERVAL_US 1000
#endif

// PICO_CONFIG: SCOPPY_USB_LOW_PRIORITY_IRQ, low priority (non hardware) IRQ number to claim for tud_task() background execution, default=31, advanced=true, group=pico_scoppy_usb
#ifndef SCOPPY_USB_LOW_PRIORITY_IRQ
#define SCOPPY_USB_LOW_PRIORITY_IRQ 31
#endif

/*! \brief Explicitly initialize USB stdio and add it to the current set of stdin drivers
 *  \ingroup pico_stdio_uart
 */
bool scoppy_usb_init();
bool scoppy_usb_out_chars(const char *buf, int length);
int scoppy_usb_in_chars(char *buf, int length);

#endif
