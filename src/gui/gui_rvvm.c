/*
gui_rvvm.c - RVVM Display GUI
Copyright (C) 2021  LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include "gui_window.h"

PUSH_OPTIMIZATION_SIZE

#if defined(USE_GUI)

typedef struct {
    rvvm_machine_t* machine;
    hid_keyboard_t* keyboard;
    hid_mouse_t*    mouse;

    uint32_t width;
    uint32_t height;

    bool ctrl;
    bool alt;
    bool grab;
} rvvm_window_t;

static const uint8_t rvvm_logo_pix[] = {
    0xfc, 0x3f, 0xf0, 0x02, 0xcb, 0x0b, 0x2c, 0x3f, 0xf0, 0xcb, 0xf3, 0x03, 0x2f, 0xb0, 0xbc, 0xc0, 0xf2, 0xcf, 0xbf,
    0x3e, 0xf2, 0xf9, 0x01, 0xe7, 0x07, 0xac, 0xdf, 0xcf, 0xeb, 0x23, 0x9f, 0x1f, 0x70, 0x7e, 0xc0, 0xfa, 0x31, 0xbc,
    0x3e, 0x30, 0xe1, 0xc3, 0x86, 0x0f, 0x9b, 0x0f, 0xe0, 0xe7, 0xc3, 0x13, 0x3e, 0x6c, 0xf8, 0xb0, 0xf9, 0x00, 0x7e,
    0xfe, 0x0f, 0x81, 0xcf, 0x01, 0x3e, 0x87, 0x0f, 0xe0, 0xe3, 0xc3, 0x03, 0xf8, 0x1c, 0xe0, 0x73, 0xf8, 0x00, 0x3e,
    0xfd, 0xf8, 0x02, 0x7e, 0x00, 0xf8, 0x81, 0x2f, 0xd0, 0xdb, 0x8f, 0x2f, 0x20, 0x07, 0x80, 0x1c, 0xf8, 0x02, 0xbd,
    0xe1, 0xe4, 0x01, 0x71, 0x00, 0xc4, 0x41, 0x18, 0x10, 0x16, 0x4e, 0x1e, 0x10, 0x07, 0x40, 0x1c, 0x84, 0x01, 0x61,
    0x90, 0x84, 0x01, 0x51, 0x00, 0x44, 0x41, 0x10, 0x00, 0x04, 0x49, 0x18, 0x10, 0x05, 0x40, 0x14, 0x04, 0x01, 0x40,
    0x50, 0x40, 0x00, 0x50, 0x00, 0x40, 0x41, 0x00, 0x10, 0x00, 0x05, 0x04, 0x00, 0x05, 0x00, 0x14, 0x04, 0x00, 0x01,
    0x40, 0x00, 0x00, 0x40, 0x00, 0x00, 0x01, 0x00, 0x10, 0x00, 0x04, 0x00, 0x00, 0x04, 0x00, 0x10, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x10, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x04, 0x00, 0x00, 0x00,
};

static void rvvm_gui_update_title(gui_window_t* win)
{
    rvvm_window_t* rvvm = win->data;
    if (rvvm->grab) {
        gui_window_set_title(win, "RVVM - Press Ctrl+Alt+G to release grab");
    } else {
        gui_window_set_title(win, "RVVM");
    }
}

static void rvvm_gui_grab_input(gui_window_t* win, bool grab)
{
    rvvm_window_t* rvvm = gui_window_get_data(win);
    if (rvvm->grab != grab && gui_window_grab_input(win, grab)) {
        rvvm->grab = grab;
        rvvm_gui_update_title(win);
    }
}

static void rvvm_gui_free(gui_window_t* win)
{
    rvvm_window_t* rvvm = gui_window_get_data(win);
    safe_free(rvvm);
}

static void rvvm_gui_on_close(gui_window_t* win)
{
    rvvm_window_t* rvvm = gui_window_get_data(win);
    if (rvvm_has_arg("poweroff_key")) {
        // Send poweroff request to the guest via keyboard key
        hid_keyboard_press(rvvm->keyboard, HID_KEY_POWER);
        hid_keyboard_release(rvvm->keyboard, HID_KEY_POWER);
    } else {
        rvvm_pause_machine(rvvm->machine);
    }
}

static void rvvm_gui_on_focus_lost(gui_window_t* win)
{
    rvvm_window_t* rvvm = gui_window_get_data(win);

    // Fix stuck buttons after lost focus (Alt+Tab, etc)
    for (hid_key_t key = 0x00; key < 0xFF; ++key) {
        hid_keyboard_release(rvvm->keyboard, key);
    }

    // Ungrab input
    rvvm_gui_grab_input(win, false);
}

static void rvvm_gui_handle_modkeys(rvvm_window_t* rvvm, hid_key_t key, bool pressed)
{
    switch (key) {
        case HID_KEY_LEFTALT:
        case HID_KEY_RIGHTALT:
            rvvm->alt = pressed;
            break;
        case HID_KEY_LEFTCTRL:
        case HID_KEY_RIGHTCTRL:
            rvvm->ctrl = pressed;
            break;
    }
}

static void rvvm_gui_on_key_press(gui_window_t* win, hid_key_t key)
{
    rvvm_window_t* rvvm = gui_window_get_data(win);
    rvvm_gui_handle_modkeys(rvvm, key, true);
    if (rvvm->ctrl && rvvm->alt) {
        switch (key) {
            case HID_KEY_G:
                // Grab mouse & keyboard
                rvvm_gui_grab_input(win, !rvvm->grab);
                return;
        }
    }
    hid_keyboard_press(rvvm->keyboard, key);
}

static void rvvm_gui_on_key_release(gui_window_t* win, hid_key_t key)
{
    rvvm_window_t* rvvm = gui_window_get_data(win);
    rvvm_gui_handle_modkeys(rvvm, key, false);
    hid_keyboard_release(rvvm->keyboard, key);
}

static void rvvm_gui_on_mouse_press(gui_window_t* win, hid_btns_t btns)
{
    rvvm_window_t* rvvm = gui_window_get_data(win);
    hid_mouse_press(rvvm->mouse, btns);
}

static void rvvm_gui_on_mouse_release(gui_window_t* win, hid_btns_t btns)
{
    rvvm_window_t* rvvm = gui_window_get_data(win);
    hid_mouse_release(rvvm->mouse, btns);
}

static void rvvm_gui_on_mouse_place(gui_window_t* win, int32_t x, int32_t y)
{
    rvvm_window_t* rvvm = gui_window_get_data(win);

    // Update tablet resolution if needed
    rvvm_fb_t fb = ZERO_INIT;
    rvvm_fbdev_get_scanout(win->fbdev, &fb);
    if (rvvm_fb_width(&fb) != rvvm->width || rvvm_fb_height(&fb) != rvvm->height) {
        rvvm->width  = rvvm_fb_width(&fb);
        rvvm->height = rvvm_fb_height(&fb);
        hid_mouse_resolution(rvvm->mouse, rvvm->width, rvvm->height);
    }

    hid_mouse_place(rvvm->mouse, x, y);
}

static void rvvm_gui_on_mouse_move(gui_window_t* win, int32_t x, int32_t y)
{
    rvvm_window_t* rvvm = gui_window_get_data(win);
    hid_mouse_move(rvvm->mouse, x, y);
}

static void rvvm_gui_on_mouse_scroll(gui_window_t* win, int32_t offset)
{
    rvvm_window_t* rvvm = gui_window_get_data(win);
    hid_mouse_scroll(rvvm->mouse, offset);
}

static void rvvm_gui_draw_logo(gui_window_t* win)
{
    // Draw RVVM logo before guest takes over
    rvvm_fb_t fb = ZERO_INIT;
    rvvm_fbdev_get_scanout(win->fbdev, &fb);

    uint8_t* buffer = rvvm_fb_buffer(&fb);
    uint32_t bytes  = rvvm_fb_rgb_bytes(&fb);
    uint32_t width  = rvvm_fb_width(&fb);
    uint32_t height = rvvm_fb_height(&fb);
    uint32_t stride = rvvm_fb_stride(&fb);
    uint32_t pos_x  = rvvm_fb_width(&fb) / 2 - 152;
    uint32_t pos_y  = rvvm_fb_height(&fb) / 2 - 80;

    for (uint32_t y = 0; y < height; ++y) {
        uint32_t tmp_stride = stride * y;
        for (uint32_t x = 0; x < width; ++x) {
            uint8_t pix = 0;
            if (x >= pos_x && x - pos_x < 304 && y >= pos_y && y - pos_y < 160) {
                uint32_t pos = ((y - pos_y) >> 3) * 38 + ((x - pos_x) >> 3);
                pix          = ((rvvm_logo_pix[pos >> 2] >> ((pos & 0x3) << 1)) & 0x3) << 6;
            }
            for (uint32_t i = 0; i < bytes; ++i) {
                buffer[i + tmp_stride + (x * bytes)] = pix;
            }
        }
    }
}

static gui_event_cb_t rvvm_gui_cb = {
    .free             = rvvm_gui_free,
    .on_close         = rvvm_gui_on_close,
    .on_focus_lost    = rvvm_gui_on_focus_lost,
    .on_key_press     = rvvm_gui_on_key_press,
    .on_key_release   = rvvm_gui_on_key_release,
    .on_mouse_press   = rvvm_gui_on_mouse_press,
    .on_mouse_release = rvvm_gui_on_mouse_release,
    .on_mouse_place   = rvvm_gui_on_mouse_place,
    .on_mouse_move    = rvvm_gui_on_mouse_move,
    .on_mouse_scroll  = rvvm_gui_on_mouse_scroll,
};

void gui_rvvm_register(gui_window_t* win, rvvm_machine_t* machine)
{
    rvvm_window_t* rvvm = safe_new_obj(rvvm_window_t);

    rvvm->machine  = machine;
    rvvm->keyboard = hid_keyboard_init_auto(machine);
    rvvm->mouse    = hid_mouse_init_auto(machine);

    gui_window_set_data(win, rvvm);
    gui_window_register(win, &rvvm_gui_cb);

    rvvm_gui_update_title(win);
    rvvm_gui_draw_logo(win);
}

gui_window_t* gui_rvvm_init(size_t vram_size, const rvvm_fb_t* fb, rvvm_machine_t* machine)
{
    gui_window_t* win = gui_window_init(vram_size, fb);
    if (win) {
        gui_rvvm_register(win, machine);
    }
    return win;
}

#else

void gui_rvvm_register(gui_window_t* win, rvvm_machine_t* machine)
{
    UNUSED(win && machine);
}

gui_window_t* gui_rvvm_init(size_t vram_size, const rvvm_fb_t* fb, rvvm_machine_t* machine)
{
    UNUSED(vram_size && fb && machine);
    return NULL;
}

#endif

POP_OPTIMIZATION_SIZE
