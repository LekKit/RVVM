/*
i2c-keyboard.c - HID Keyboard
Copyright (C) 2022  X512 <github.com/X547>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "i2c-hid.h"
#include "hid_api.h"
#include "spinlock.h"
#include "utils.h"
#include "bit_ops.h"

#include <string.h>

#define MAX_PRESSED_KEYS 6

static const uint8_t keyboard_hid_report_descriptor[] = {
    0x05, 0x01,     /* Usage Page (Generic Desktop) */
    0x09, 0x06,     /* Usage (Keyboard) */
    0xa1, 0x01,     /* Collection (Application) */
    0x75, 0x01,     /*   Report Size (1) */
    0x95, 0x08,     /*   Report Count (8) */
    0x05, 0x07,     /*   Usage Page (Key Codes) */
    0x19, 0xe0,     /*   Usage Minimum (224) */
    0x29, 0xe7,     /*   Usage Maximum (231) */
    0x15, 0x00,     /*   Logical Minimum (0) */
    0x25, 0x01,     /*   Logical Maximum (1) */
    0x81, 0x02,     /*   Input (Data, Variable, Absolute) */
    0x95, 0x01,     /*   Report Count (1) */
    0x75, 0x08,     /*   Report Size (8) */
    0x81, 0x01,     /*   Input (Constant) */
    0x95, 0x05,     /*   Report Count (5) */
    0x75, 0x01,     /*   Report Size (1) */
    0x05, 0x08,     /*   Usage Page (LEDs) */
    0x19, 0x01,     /*   Usage Minimum (1) */
    0x29, 0x05,     /*   Usage Maximum (5) */
    0x91, 0x02,     /*   Output (Data, Variable, Absolute) */
    0x95, 0x01,     /*   Report Count (1) */
    0x75, 0x03,     /*   Report Size (3) */
    0x91, 0x01,     /*   Output (Constant) */
    0x95, 0x06,     /*   Report Count (6) */
    0x75, 0x08,     /*   Report Size (8) */
    0x15, 0x00,     /*   Logical Minimum (0) */
    0x25, 0xff,     /*   Logical Maximum (255) */
    0x05, 0x07,     /*   Usage Page (Key Codes) */
    0x19, 0x00,     /*   Usage Minimum (0) */
    0x29, 0xff,     /*   Usage Maximum (255) */
    0x81, 0x00,     /*   Input (Data, Array) */
    0xc0,       /* End Collection */
};

struct hid_keyboard {
    hid_dev_t hid_dev;

    spinlock_t lock;

    uint8_t input_report[10];
    uint8_t output_report[3];

    // state
    uint32_t keys[8];
    uint32_t leds;
};


static void hid_keyboard_reset(void* dev)
{
    hid_keyboard_t* kb = (hid_keyboard_t*)dev;
    spin_lock(&kb->lock);
    memset(kb->keys, 0, sizeof(kb->keys));
    kb->leds = 0;
    spin_unlock(&kb->lock);
}

static void hid_keyboard_fill_pressed_keys(hid_keyboard_t* kb, uint8_t* pressed)
{
    uint32_t idx = 0;
    for (uint32_t code_hi = 0; code_hi < 8; code_hi++) {
        uint32_t keys = kb->keys[code_hi];
        if (keys == 0) continue;
        for (uint32_t code_lo = 0; code_lo < 32; code_lo++) {
            if (keys & (1U << code_lo)) {
                pressed[idx++] = code_hi*32 + code_lo;
                if (idx == MAX_PRESSED_KEYS) return;
            }
        }
    }
    while (idx < MAX_PRESSED_KEYS)
        pressed[idx++] = HID_KEY_NONE;
}

static void hid_keyboard_read_report(void* dev,
    uint8_t report_type, uint8_t report_id, uint32_t offset, uint8_t *val)
{
    UNUSED(report_id);
    hid_keyboard_t* kb = (hid_keyboard_t*)dev;
    spin_lock(&kb->lock);
    switch (report_type) {
    case REPORT_TYPE_INPUT:
        if (offset == 0) {
            kb->input_report[0] = bit_cut(sizeof(kb->input_report), 0, 8);
            kb->input_report[1] = bit_cut(sizeof(kb->input_report), 8, 8);
            kb->input_report[2] = bit_cut(kb->keys[7], 0, 8);
            kb->input_report[3] = 0;
            hid_keyboard_fill_pressed_keys(kb, &kb->input_report[4]);
        }
        if (offset < sizeof(kb->input_report))
            *val = kb->input_report[offset];
        break;
    default:
        *val = 0;
        break;
    }
    spin_unlock(&kb->lock);
}

static void hid_keyboard_write_report(void* dev,
    uint8_t report_type, uint8_t report_id, uint32_t offset, uint8_t val)
{
    UNUSED(report_id);
    hid_keyboard_t* kb = (hid_keyboard_t*)dev;
    spin_lock(&kb->lock);
    switch (report_type) {
    case REPORT_TYPE_OUTPUT:
        if (offset < sizeof(kb->output_report)) {
            kb->output_report[offset] = val;
            if (offset == sizeof(kb->output_report) - 1)
                kb->leds = kb->output_report[2];
        }
        break;
    }
    spin_unlock(&kb->lock);
}

static void hid_keyboard_remove(void* dev)
{
    hid_keyboard_t* kb = (hid_keyboard_t*)dev;
    free(kb);
}

PUBLIC hid_keyboard_t* hid_keyboard_init_auto(rvvm_machine_t* machine)
{
    hid_keyboard_t* kb = safe_new_obj(hid_keyboard_t);

    spin_init(&kb->lock);

    kb->hid_dev.dev = kb;

    kb->hid_dev.report_desc = keyboard_hid_report_descriptor;
    kb->hid_dev.report_desc_size = sizeof(keyboard_hid_report_descriptor);
    kb->hid_dev.max_input_size = sizeof(kb->input_report);
    kb->hid_dev.max_output_size = sizeof(kb->output_report);
    kb->hid_dev.vendor_id = 1;
    kb->hid_dev.product_id = 1;
    kb->hid_dev.version_id = 1;

    kb->hid_dev.reset = hid_keyboard_reset;
    kb->hid_dev.read_report = hid_keyboard_read_report;
    kb->hid_dev.write_report = hid_keyboard_write_report;
    kb->hid_dev.remove = hid_keyboard_remove;

    i2c_hid_init_auto(machine, &kb->hid_dev);

    return kb;
}

PUBLIC void hid_keyboard_press(hid_keyboard_t* kb, hid_key_t key)
{
    bool is_input_avail = false;
    spin_lock(&kb->lock);
    // key is guaranteed to be 1 byte according to HID spec
    if (key != HID_KEY_NONE) {
        kb->keys[key/32] |= 1U << (key%32);
        is_input_avail = true;
    }
    spin_unlock(&kb->lock);
    if (is_input_avail)
        kb->hid_dev.input_available(kb->hid_dev.host, 0);
}

PUBLIC void hid_keyboard_release(hid_keyboard_t* kb, hid_key_t key)
{
    bool is_input_avail = false;
    spin_lock(&kb->lock);
    if (key != HID_KEY_NONE) {
        kb->keys[key/32] &= ~(1U << (key%32));
        is_input_avail = true;
    }
    spin_unlock(&kb->lock);
    if (is_input_avail)
        kb->hid_dev.input_available(kb->hid_dev.host, 0);
}
