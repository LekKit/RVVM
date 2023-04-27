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

static const uint8_t mouse_hid_report_descriptor[] = {
    0x05, 0x01,     /* Usage Page (Generic Desktop) */
    0x09, 0x02,     /* Usage (Mouse) */
    0xa1, 0x01,     /* Collection (Application) */
    0x09, 0x01,     /*   Usage (Pointer) */
    0xa1, 0x00,     /*   Collection (Physical) */
    0x05, 0x09,     /*     Usage Page (Button) */
    0x19, 0x01,     /*     Usage Minimum (1) */
    0x29, 0x05,     /*     Usage Maximum (5) */
    0x15, 0x00,     /*     Logical Minimum (0) */
    0x25, 0x01,     /*     Logical Maximum (1) */
    0x95, 0x05,     /*     Report Count (5) */
    0x75, 0x01,     /*     Report Size (1) */
    0x81, 0x02,     /*     Input (Data, Variable, Absolute) */
    0x95, 0x01,     /*     Report Count (1) */
    0x75, 0x03,     /*     Report Size (3) */
    0x81, 0x01,     /*     Input (Constant) */
    0x05, 0x01,     /*     Usage Page (Generic Desktop) */
    0x09, 0x30,     /*     Usage (X) */
    0x09, 0x31,     /*     Usage (Y) */
    0x09, 0x38,     /*     Usage (Wheel) */
    0x15, 0x81,     /*     Logical Minimum (-0x7f) */
    0x25, 0x7f,     /*     Logical Maximum (0x7f) */
    0x75, 0x08,     /*     Report Size (8) */
    0x95, 0x03,     /*     Report Count (3) */
    0x81, 0x06,     /*     Input (Data, Variable, Relative) */
    0xc0,       /*   End Collection */
    0xc0,       /* End Collection */
};

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
    hid_dev_t mouse_hid_dev;
    hid_dev_t tablet_hid_dev;

    spinlock_t lock;
    int32_t width;
    int32_t height;

    uint8_t input_report_mouse[6];
    uint8_t input_report_tablet[8];

    // state
    bool tablet_mode;
    int32_t tablet_x;
    int32_t tablet_y;
    int32_t mouse_delta_x;
    int32_t mouse_delta_y;
    int32_t scroll_y;
    hid_btns_t btns_mouse;
    hid_btns_t btns_tablet;
};

static void hid_mouse_reset(void* dev)
{
    hid_mouse_t* mouse = (hid_mouse_t*)dev;
    spin_lock(&mouse->lock);
    mouse->tablet_mode = true;
    mouse->tablet_x = 0;
    mouse->tablet_y = 0;
    mouse->mouse_delta_x = 0;
    mouse->mouse_delta_y = 0;
    mouse->scroll_y = 0;
    mouse->btns_mouse = 0;
    mouse->btns_tablet = 0;
    spin_unlock(&mouse->lock);
}

static void hid_mouse_read_report_mouse(void* dev,
    uint8_t report_type, uint8_t report_id, uint32_t offset, uint8_t *val)
{
    UNUSED(report_id);
    hid_mouse_t* mouse = (hid_mouse_t*)dev;
    spin_lock(&mouse->lock);
    if (report_type == REPORT_TYPE_INPUT) {
        if (offset == 0) {
            int32_t delta_x = mouse->mouse_delta_x / 3;
            int32_t delta_y = mouse->mouse_delta_y / 3;
            mouse->input_report_mouse[0] = bit_cut(sizeof(mouse->input_report_mouse), 0, 8);
            mouse->input_report_mouse[1] = bit_cut(sizeof(mouse->input_report_mouse), 8, 8);
            mouse->input_report_mouse[2] = mouse->btns_mouse;
            mouse->input_report_mouse[3] = delta_x;
            mouse->input_report_mouse[4] = delta_y;
            mouse->input_report_mouse[5] = -mouse->scroll_y;
            mouse->mouse_delta_x -= delta_x * 3;
            mouse->mouse_delta_y -= delta_y * 3;
            mouse->scroll_y = 0;
        }
        if (offset < sizeof(mouse->input_report_mouse))
            *val = mouse->input_report_mouse[offset];
    } else {
        *val = 0;
    }
    spin_unlock(&mouse->lock);
}

static void hid_mouse_read_report_tablet(void* dev,
    uint8_t report_type, uint8_t report_id, uint32_t offset, uint8_t *val)
{
    UNUSED(report_id);
    hid_mouse_t* mouse = (hid_mouse_t*)dev;
    spin_lock(&mouse->lock);
    if (report_type == REPORT_TYPE_INPUT) {
        if (offset == 0) {
            mouse->input_report_tablet[0] = bit_cut(sizeof(mouse->input_report_tablet), 0, 8);
            mouse->input_report_tablet[1] = bit_cut(sizeof(mouse->input_report_tablet), 8, 8);
            mouse->input_report_tablet[2] = mouse->btns_tablet;
            mouse->input_report_tablet[3] = bit_cut(mouse->tablet_x, 0, 8);
            mouse->input_report_tablet[4] = bit_cut(mouse->tablet_x, 8, 8);
            mouse->input_report_tablet[5] = bit_cut(mouse->tablet_y, 0, 8);
            mouse->input_report_tablet[6] = bit_cut(mouse->tablet_y, 8, 8);
            mouse->input_report_tablet[7] = -mouse->scroll_y;
            mouse->scroll_y = 0;
        }
        if (offset < sizeof(mouse->input_report_tablet))
            *val = mouse->input_report_tablet[offset];
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

static void hid_mouse_setup(hid_dev_t *dev, hid_mouse_t *mouse, bool tablet)
{
    dev->dev = mouse;
    dev->report_desc = tablet
        ? tablet_hid_report_descriptor
        : mouse_hid_report_descriptor;
    dev->report_desc_size = tablet
        ? sizeof(tablet_hid_report_descriptor)
        : sizeof(mouse_hid_report_descriptor);
    dev->max_input_size = tablet
        ? sizeof(mouse->input_report_tablet)
        : sizeof(mouse->input_report_mouse);
    dev->max_output_size = 0;
    dev->vendor_id = 1;
    dev->product_id = 1;
    dev->version_id = 1;
    dev->reset = hid_mouse_reset;
    dev->read_report = tablet
        ? hid_mouse_read_report_tablet
        : hid_mouse_read_report_mouse;
    dev->write_report = hid_mouse_write_report;
    dev->remove = tablet
        ? NULL // Don't free the same object twice
        : hid_mouse_remove;
}

PUBLIC hid_mouse_t* hid_mouse_init_auto(rvvm_machine_t* machine)
{
    hid_mouse_t* mouse = safe_new_obj(hid_mouse_t);

    spin_init(&mouse->lock);

    hid_mouse_setup(&mouse->tablet_hid_dev, mouse, true);
    hid_mouse_setup(&mouse->mouse_hid_dev, mouse, false);

    i2c_hid_init_auto(machine, &mouse->tablet_hid_dev);
    i2c_hid_init_auto(machine, &mouse->mouse_hid_dev);

    return mouse;
}

PUBLIC void hid_mouse_press(hid_mouse_t* mouse, hid_btns_t btns)
{
    spin_lock(&mouse->lock);
    hid_dev_t *hid_dev;
    if (mouse->tablet_mode) {
        mouse->btns_tablet |= btns;
        hid_dev = &mouse->tablet_hid_dev;
    } else {
        mouse->btns_mouse |= btns;
        hid_dev = &mouse->mouse_hid_dev;
    }
    spin_unlock(&mouse->lock);
    hid_dev->input_available(hid_dev->host, 0);
}

PUBLIC void hid_mouse_release(hid_mouse_t* mouse, hid_btns_t btns)
{
    spin_lock(&mouse->lock);
    hid_dev_t *hid_dev = mouse->tablet_mode ? &mouse->tablet_hid_dev : &mouse->mouse_hid_dev;
    mouse->btns_mouse &= ~btns;
    mouse->btns_tablet &= ~btns;
    spin_unlock(&mouse->lock);
    hid_dev->input_available(hid_dev->host, 0);
}

PUBLIC void hid_mouse_scroll(hid_mouse_t* mouse, int32_t offset)
{
    spin_lock(&mouse->lock);
    hid_dev_t *hid_dev = mouse->tablet_mode ? &mouse->tablet_hid_dev : &mouse->mouse_hid_dev;
    mouse->scroll_y += offset;
    spin_unlock(&mouse->lock);
    hid_dev->input_available(hid_dev->host, 0);
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
    spin_lock(&mouse->lock);
    mouse->mouse_delta_x += x;
    mouse->mouse_delta_y += y;
    bool is_input_avail = mouse->mouse_delta_x != 0 || mouse->mouse_delta_y != 0 || mouse->tablet_mode;
    mouse->tablet_mode = false;
    spin_unlock(&mouse->lock);
    if (is_input_avail)
        mouse->mouse_hid_dev.input_available(mouse->mouse_hid_dev.host, 0);
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
        mouse->tablet_x = (int32_t)((int64_t)x * 0x7fff / mouse->width);
        mouse->tablet_y = (int32_t)((int64_t)y * 0x7fff / mouse->height);
        is_input_avail = true;
    }
    mouse->tablet_mode = true;
    spin_unlock(&mouse->lock);
    if (is_input_avail)
        mouse->tablet_hid_dev.input_available(mouse->tablet_hid_dev.host, 0);
}
