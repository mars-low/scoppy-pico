/**
 * Copyright (C) 2021 FHDM Apps <scoppy@fhdm.xyz>
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#if !defined(TINYUSB_HOST_LINKED) && !defined(TINYUSB_DEVICE_LINKED)
#include "tusb.h"

#include "pico/time.h"
#include "scoppy_usb.h"
#include "hardware/irq.h"

static_assert(SCOPPY_USB_LOW_PRIORITY_IRQ > RTC_IRQ, ""); // note RTC_IRQ is currently the last one
static mutex_t scoppy_usb_mutex;

static void low_priority_worker_irq() {
    // if the mutex is already owned, then we are in user code
    // in this file which will do a tud_task itself, so we'll just do nothing
    // until the next tick; we won't starve
    if (mutex_try_enter(&scoppy_usb_mutex, NULL)) {
        tud_task();
        mutex_exit(&scoppy_usb_mutex);
    }
}

static int64_t timer_task(__unused alarm_id_t id, __unused void *user_data) {
    irq_set_pending(SCOPPY_USB_LOW_PRIORITY_IRQ);
    return SCOPPY_USB_TASK_INTERVAL_US;
}

bool scoppy_usb_out_chars(const char *buf, int length) {
    static uint64_t last_avail_time;
    uint32_t owner;
    if (!mutex_try_enter(&scoppy_usb_mutex, &owner)) {
        if (owner == get_core_num()) return false; // would deadlock otherwise
        mutex_enter_blocking(&scoppy_usb_mutex);
    }
    if (tud_cdc_connected()) {
        for (int i = 0; i < length;) {
            int n = length - i;
            int avail = tud_cdc_write_available();
            if (n > avail) n = avail;
            if (n) {
                int n2 = tud_cdc_write(buf + i, n);
                tud_task();
                tud_cdc_write_flush();
                i += n2;
                last_avail_time = time_us_64();
            } else {
                tud_task();
                tud_cdc_write_flush();
                if (!tud_cdc_connected() ||
                    (!tud_cdc_write_available() && time_us_64() > last_avail_time + SCOPPY_USB_STDOUT_TIMEOUT_US)) {
                    break;
                }
            }
        }
    } else {
        // reset our timeout
        last_avail_time = 0;
    }
    mutex_exit(&scoppy_usb_mutex);

    return true;
}

int scoppy_usb_in_chars(char *buf, int length) {
    uint32_t owner;
    if (!mutex_try_enter(&scoppy_usb_mutex, &owner)) {
        if (owner == get_core_num()) return PICO_ERROR_NO_DATA; // would deadlock otherwise
        mutex_enter_blocking(&scoppy_usb_mutex);
    }
    int rc = PICO_ERROR_NO_DATA;
    if (tud_cdc_connected() && tud_cdc_available()) {
        int count = tud_cdc_read(buf, length);
        rc =  count ? count : PICO_ERROR_NO_DATA;
    }
    mutex_exit(&scoppy_usb_mutex);
    return rc;
}

bool scoppy_usb_init(void) {

    // initialize TinyUSB
    tusb_init();

    irq_set_exclusive_handler(SCOPPY_USB_LOW_PRIORITY_IRQ, low_priority_worker_irq);
    irq_set_enabled(SCOPPY_USB_LOW_PRIORITY_IRQ, true);

    mutex_init(&scoppy_usb_mutex);
    bool rc = add_alarm_in_us(SCOPPY_USB_TASK_INTERVAL_US, timer_task, NULL, true);
    if (rc) {
        //scoppy_set_driver_enabled(&scoppy_usb, true);
    }
    return rc;
}
#else
#error scoppy USB was configured, but is being disabled as TinyUSB is explicitly linked
#endif
