/*
fb_window.h - Framebuffer window device
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

#ifndef FB_WINDOW_H
#define FB_WINDOW_H

#include "framebuffer.h"
#include "hid_api.h"

typedef struct win_data win_data_t;

typedef struct {
    win_data_t*     data;
    fb_ctx_t        fb;
    // If we want specific rgb format, apply conversion
    fb_ctx_t        guest_fb;
    rvvm_machine_t* machine;
    hid_keyboard_t* keyboard;
    hid_mouse_t*    mouse;
} fb_window_t;

// Allocates fb, sets up rgb format
bool fb_window_create(fb_window_t* window);
void fb_window_close(fb_window_t* window);
void fb_window_update(fb_window_t* window);

PUBLIC bool fb_window_init_auto(rvvm_machine_t* machine, uint32_t width, uint32_t height);

#endif
