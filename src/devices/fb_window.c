/*
fb_window.c - Framebuffer window device
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

#include "fb_window.h"
#include "utils.h"

// TODO
/*void r5g6b5_to_a8r8g8b8(const void* _in, void* _out, size_t length)
{
    const uint8_t* in = (uint8_t*)_in;
    uint8_t* out = (uint8_t*)_out;
    for (size_t i=0; i<length; ++i) {
        uint8_t r5 = in[i*2] & 31;
        uint8_t g6 = ((in[i*2] >> 5) | (in[i*2 + 1] << 3)) & 63;
        uint8_t b5 = in[i*2 + 1] >> 3;

        out[i*4] = (r5 << 3) | (r5 >> 2);
        out[i*4 + 1] = (g6 << 2) | (g6 >> 4);
        out[i*4 + 2] = (b5 << 3) | (b5 >> 2);
        out[i*4 + 3] = 0xff;
    }
}

void a8r8g8b8_to_r5g6b5(const void* _in, void* _out, size_t length)
{
    const uint8_t* in = (uint8_t*)_in;
    uint8_t* out = (uint8_t*)_out;
    for (size_t i=0; i<length; ++i) {
        uint8_t r5 = in[i*4] >> 3;
        uint8_t g6 = in[i*4 + 1] >> 2;
        uint8_t b5 = in[i*4 + 2] >> 3;

        out[i * 2] = (g6 << 5) | b5;
        out[i * 2 + 1] = (r5 << 3) | (g6 >> 3);
    }
}*/

#ifdef USE_FB

static void window_update(rvvm_mmio_dev_t* device)
{
    fb_window_update((fb_window_t*)device->data);
}

static void window_remove(rvvm_mmio_dev_t* device)
{
    fb_window_close((fb_window_t*)device->data);
    free(device->data);
}

static rvvm_mmio_type_t win_dev_type = {
    .name = "vm_window",
    .remove = window_remove,
    .update = window_update,
};

bool fb_window_init_auto(rvvm_machine_t* machine, uint32_t width, uint32_t height)
{
    fb_window_t* window = safe_calloc(sizeof(fb_window_t), 1);
    window->fb.width = width;
    window->fb.height = height;
    window->fb.format = RGB_FMT_A8R8G8B8;
    if (!fb_window_create(window)) {
        rvvm_error("Window creation failed");
        free(window);
        return false;
    }
    window->machine = machine;
    window->keyboard = hid_keyboard_init_auto(machine);
    window->mouse = hid_mouse_init_auto(machine);
    
    framebuffer_init_auto(machine, &window->fb);
    
    // Placeholder for window data, region size is 0
    rvvm_mmio_dev_t win_placeholder = {
        .data = window,
        .type = &win_dev_type,
    };
    rvvm_attach_mmio(machine, &win_placeholder);
    return true;
}

#else

bool fb_window_init_auto(rvvm_machine_t* machine, uint32_t width, uint32_t height)
{
    UNUSED(machine);
    UNUSED(width);
    UNUSED(height);
    return false;
}

#endif
