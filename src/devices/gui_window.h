/*
gui_window.h - Framebuffer GUI Window
Copyright (C) 2021  LekKit <github.com/LekKit>

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

#ifndef GUI_WINDOW_H
#define GUI_WINDOW_H

#include "framebuffer.h"
#include "hid_api.h"

typedef struct gui_window_t gui_window_t;

struct gui_window_t {
    void* win_data;
    void* data;
    fb_ctx_t fb;

    // Calls into GUI implementation
    void (*draw)(gui_window_t* win);
    void (*poll)(gui_window_t* win);
    void (*remove)(gui_window_t* win);
    void (*grab_input)(gui_window_t* win, bool grab);
    void (*set_title)(gui_window_t* win, const char* title);
    void (*set_fullscreen)(gui_window_t* win, bool fullscreen); // TODO: Borderless fullscreen

    // Calls from GUI implementation
    void (*on_close)(gui_window_t* win);
    void (*on_focus_lost)(gui_window_t* win);
    void (*on_key_press)(gui_window_t* win, hid_key_t key);
    void (*on_key_release)(gui_window_t* win, hid_key_t key);
    void (*on_mouse_press)(gui_window_t* win, hid_btns_t btns);
    void (*on_mouse_release)(gui_window_t* win, hid_btns_t btns);
    void (*on_mouse_place)(gui_window_t* win, int32_t x, int32_t y);
    void (*on_mouse_move)(gui_window_t* win, int32_t x, int32_t y);
    void (*on_mouse_scroll)(gui_window_t* win, int32_t offset);
};

// Internal use
bool x11_window_init(gui_window_t* win);
bool wayland_window_init(gui_window_t* win); // TODO: Wayland GUI backend
bool win32_window_init(gui_window_t* win);
bool haiku_window_init(gui_window_t* win);
bool sdl_window_init(gui_window_t* win);

// Probe windowing backends and create a window. Returns false on failure.
bool gui_window_create(gui_window_t* win);

// Attach a framebuffer & HID mouse/keyboard to the VM. Returns false on failure.
bool gui_window_init_auto(rvvm_machine_t* machine, uint32_t width, uint32_t height);

#endif
