/*
hid-mouse.c - HID Mouse/Tablet
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

static const uint8_t tablet_hid_report_descriptor[] = {
    0x05, 0x01,     /* Usage Page (Generic Desktop) */
    0x09, 0x02,     /* Usage (Mouse) */
    0xa1, 0x01,     /* Collection (Application) */
    0x09, 0x01,     /*   Usage (Pointer) */
    0xa1, 0x00,     /*   Collection (Physical) */
    0x05, 0x09,     /*     Usage Page (Button) */
    0x19, 0x01,     /*     Usage Minimum (1) */
    0x29, 0x03,     /*     Usage Maximum (3) */
    0x15, 0x00,     /*     Logical Minimum (0) */
    0x25, 0x01,     /*     Logical Maximum (1) */
    0x95, 0x03,     /*     Report Count (3) */
    0x75, 0x01,     /*     Report Size (1) */
    0x81, 0x02,     /*     Input (Data, Variable, Absolute) */
    0x95, 0x01,     /*     Report Count (1) */
    0x75, 0x05,     /*     Report Size (5) */
    0x81, 0x01,     /*     Input (Constant) */
    0x05, 0x01,     /*     Usage Page (Generic Desktop) */
    0x09, 0x30,     /*     Usage (X) */
    0x09, 0x31,     /*     Usage (Y) */
    0x15, 0x00,     /*     Logical Minimum (0) */
    0x26, 0xff, 0x7f,   /*     Logical Maximum (0x7fff) */
    0x35, 0x00,     /*     Physical Minimum (0) */
    0x46, 0xff, 0x7f,   /*     Physical Maximum (0x7fff) */
    0x75, 0x10,     /*     Report Size (16) */
    0x95, 0x02,     /*     Report Count (2) */
    0x81, 0x02,     /*     Input (Data, Variable, Absolute) */
    0x05, 0x01,     /*     Usage Page (Generic Desktop) */
    0x09, 0x38,     /*     Usage (Wheel) */
    0x15, 0x81,     /*     Logical Minimum (-0x7f) */
    0x25, 0x7f,     /*     Logical Maximum (0x7f) */
    0x35, 0x00,     /*     Physical Minimum (same as logical) */
    0x45, 0x00,     /*     Physical Maximum (same as logical) */
    0x75, 0x08,     /*     Report Size (8) */
    0x95, 0x01,     /*     Report Count (1) */
    0x81, 0x06,     /*     Input (Data, Variable, Relative) */
    0xc0,       /*   End Collection */
    0xc0,       /* End Collection */
};

struct hid_mouse {
    hid_dev_t hid_dev;

    spinlock_t lock;
    int32_t width;
    int32_t height;

    uint8_t input_report[8];

    // state
    int32_t x;
    int32_t y;
    int32_t scroll_y;
    hid_btns_t btns;
};

static void hid_mouse_reset(void* dev)
{
    hid_mouse_t* mouse = (hid_mouse_t*)dev;
    spin_lock(&mouse->lock);
    mouse->x = 0;
    mouse->y = 0;
    mouse->scroll_y = 0;
    mouse->btns = 0;
    spin_unlock(&mouse->lock);
}

static void hid_mouse_read_report(void* dev,
    uint8_t report_type, uint8_t report_id, uint32_t offset, uint8_t *val)
{
    UNUSED(report_id);
    hid_mouse_t* mouse = (hid_mouse_t*)dev;
    spin_lock(&mouse->lock);
    if (report_type == REPORT_TYPE_INPUT) {
        if (offset == 0) {
            mouse->input_report[0] = bit_cut(sizeof(mouse->input_report), 0, 8);
            mouse->input_report[1] = bit_cut(sizeof(mouse->input_report), 8, 8);
            mouse->input_report[2] = mouse->btns;
            mouse->input_report[3] = bit_cut(mouse->x, 0, 8);
            mouse->input_report[4] = bit_cut(mouse->x, 8, 8);
            mouse->input_report[5] = bit_cut(mouse->y, 0, 8);
            mouse->input_report[6] = bit_cut(mouse->y, 8, 8);
            mouse->input_report[7] = -mouse->scroll_y;
            mouse->scroll_y = 0;
        }
        if (offset < sizeof(mouse->input_report))
            *val = mouse->input_report[offset];
    } else {
        *val = 0;
    }
    spin_unlock(&mouse->lock);
}

static void hid_mouse_write_report(void* dev,
    uint8_t report_type, uint8_t report_id, uint32_t offset, uint8_t val)
{
    hid_mouse_t* mouse = (hid_mouse_t*)dev;
    UNUSED(mouse);
    UNUSED(report_type);
    UNUSED(report_id);
    UNUSED(offset);
    UNUSED(val);
}

static void hid_mouse_remove(void* dev)
{
    hid_mouse_t* mouse = (hid_mouse_t*)dev;
    free(mouse);
}

PUBLIC hid_mouse_t* hid_mouse_init_auto(rvvm_machine_t* machine)
{
    hid_mouse_t* mouse = safe_new_obj(hid_mouse_t);

    spin_init(&mouse->lock);

    mouse->hid_dev.dev = mouse;

    mouse->hid_dev.report_desc = tablet_hid_report_descriptor;
    mouse->hid_dev.report_desc_size = sizeof(tablet_hid_report_descriptor);
    mouse->hid_dev.max_input_size = sizeof(mouse->input_report);
    mouse->hid_dev.max_output_size = 0;
    mouse->hid_dev.vendor_id = 1;
    mouse->hid_dev.product_id = 1;
    mouse->hid_dev.version_id = 1;

    mouse->hid_dev.reset = hid_mouse_reset;
    mouse->hid_dev.read_report = hid_mouse_read_report;
    mouse->hid_dev.write_report = hid_mouse_write_report;
    mouse->hid_dev.remove = hid_mouse_remove;

    i2c_hid_init_auto(machine, &mouse->hid_dev);

    return mouse;
}

PUBLIC void hid_mouse_press(hid_mouse_t* mouse, hid_btns_t btns)
{
    spin_lock(&mouse->lock);
    mouse->btns |= btns;
    spin_unlock(&mouse->lock);
    mouse->hid_dev.input_available(mouse->hid_dev.host, 0);
}

PUBLIC void hid_mouse_release(hid_mouse_t* mouse, hid_btns_t btns)
{
    spin_lock(&mouse->lock);
    mouse->btns &= ~btns;
    spin_unlock(&mouse->lock);
    mouse->hid_dev.input_available(mouse->hid_dev.host, 0);
}

PUBLIC void hid_mouse_scroll(hid_mouse_t* mouse, int32_t offset)
{
    spin_lock(&mouse->lock);
    mouse->scroll_y += offset;
    spin_unlock(&mouse->lock);
    mouse->hid_dev.input_available(mouse->hid_dev.host, 0);
}

PUBLIC void hid_mouse_resolution(hid_mouse_t* mouse, uint32_t width, uint32_t height)
{
    spin_lock(&mouse->lock);
    mouse->width = width;
    mouse->height = height;
    spin_unlock(&mouse->lock);
}

PUBLIC void hid_mouse_move(hid_mouse_t* mouse, int32_t x, int32_t y)
{
    bool is_input_avail = false;
    spin_lock(&mouse->lock);
    if (mouse->width > 0 && mouse->height > 0) {
        mouse->x += (int32_t)((int64_t)x * 0x7fff / mouse->width);
        mouse->y += (int32_t)((int64_t)y * 0x7fff / mouse->height);
        if (mouse->x < 0) mouse->x = 0;
        else if (mouse->x > 0x7fff) mouse->x = 0x7fff;
        if (mouse->y < 0) mouse->y = 0;
        else if (mouse->y > 0x7fff) mouse->y = 0x7fff;
        is_input_avail = true;
    }
    spin_unlock(&mouse->lock);
    if (is_input_avail)
        mouse->hid_dev.input_available(mouse->hid_dev.host, 0);
}

PUBLIC void hid_mouse_place(hid_mouse_t* mouse, int32_t x, int32_t y)
{
    bool is_input_avail = false;
    spin_lock(&mouse->lock);
    if (mouse->width > 0 && mouse->height > 0) {
        if (x < 0) x = 0;
        else if (x > mouse->width) x = mouse->width;
        if (y < 0) y = 0;
        else if (y > mouse->height) y = mouse->height;
        mouse->x = (int32_t)((int64_t)x * 0x7fff / mouse->width);
        mouse->y = (int32_t)((int64_t)y * 0x7fff / mouse->height);
        is_input_avail = true;
    }
    spin_unlock(&mouse->lock);
    if (is_input_avail)
        mouse->hid_dev.input_available(mouse->hid_dev.host, 0);
}
