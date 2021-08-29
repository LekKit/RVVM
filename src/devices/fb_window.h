/*
fb_window.h - Framebuffer window device
Copyright (C) 2021  cerg2010cerg2010 <github.com/cerg2010cerg2010>

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

#include "rvvm.h"
#include "ps2-altera.h"

/* TODO: pixel format */

void r5g6b5_to_a8r8b8g8(const void* _in, void* _out, size_t length);
void a8r8g8b8_to_r5g6b5(const void* _in, void* _out, size_t length);

struct fb_data
{
    char *framebuffer;
    struct ps2_device *mouse;
    struct ps2_device *keyboard;
    void *winsys_data; // private window system data
};

void init_fb(rvvm_machine_t* machine, paddr_t addr, uint32_t width, uint32_t height, struct ps2_device *mouse, struct ps2_device *keyboard);

#if defined(USE_FB)
void fb_create_window(struct fb_data *data, unsigned width, unsigned height, const char* name);
void fb_close_window(struct fb_data *data);
void fb_update(struct fb_data *data);
#else
/* dummy functions when no window system available */
inline void fb_create_window(struct fb_data* data, unsigned width, unsigned height, const char* name) {
    UNUSED(data);
    UNUSED(width);
    UNUSED(height);
    UNUSED(name);
}
inline void fb_close_window(struct fb_data *data) {
    // Free a dummy framebuffer
    free(data->framebuffer);
}
inline void fb_update(struct fb_data *data) {
    UNUSED(data);
}
#endif
#endif
