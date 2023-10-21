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
#include "mem_ops.h"
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

static const uint8_t rvvm_logo_pix[] = {
    0xfc, 0x3f, 0xf0, 0x02, 0xcb, 0x0b, 0x2c, 0x3f, 0xf0, 0xcb,
    0xf3, 0x03, 0x2f, 0xb0, 0xbc, 0xc0, 0xf2, 0xcf, 0xbf, 0x3e,
    0xf2, 0xf9, 0x01, 0xe7, 0x07, 0xac, 0xdf, 0xcf, 0xeb, 0x23,
    0x9f, 0x1f, 0x70, 0x7e, 0xc0, 0xfa, 0x31, 0xbc, 0x3e, 0x30,
    0xe1, 0xc3, 0x86, 0x0f, 0x9b, 0x0f, 0xe0, 0xe7, 0xc3, 0x13,
    0x3e, 0x6c, 0xf8, 0xb0, 0xf9, 0x00, 0x7e, 0xfe, 0x0f, 0x81,
    0xcf, 0x01, 0x3e, 0x87, 0x0f, 0xe0, 0xe3, 0xc3, 0x03, 0xf8,
    0x1c, 0xe0, 0x73, 0xf8, 0x00, 0x3e, 0xfd, 0xf8, 0x02, 0x7e,
    0x00, 0xf8, 0x81, 0x2f, 0xd0, 0xdb, 0x8f, 0x2f, 0x20, 0x07,
    0x80, 0x1c, 0xf8, 0x02, 0xbd, 0xe1, 0xe4, 0x01, 0x71, 0x00,
    0xc4, 0x41, 0x18, 0x10, 0x16, 0x4e, 0x1e, 0x10, 0x07, 0x40,
    0x1c, 0x84, 0x01, 0x61, 0x90, 0x84, 0x01, 0x51, 0x00, 0x44,
    0x41, 0x10, 0x00, 0x04, 0x49, 0x18, 0x10, 0x05, 0x40, 0x14,
    0x04, 0x01, 0x40, 0x50, 0x40, 0x00, 0x50, 0x00, 0x40, 0x41,
    0x00, 0x10, 0x00, 0x05, 0x04, 0x00, 0x05, 0x00, 0x14, 0x04,
    0x00, 0x01, 0x40, 0x00, 0x00, 0x40, 0x00, 0x00, 0x01, 0x00,
    0x10, 0x00, 0x04, 0x00, 0x00, 0x04, 0x00, 0x10, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x10, 0x00, 0x40, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x04, 0x00, 0x00, 0x00,
};

static void window_update(rvvm_mmio_dev_t* device)
{
    fb_window_update((fb_window_t*)device->data);
}

static void window_remove(rvvm_mmio_dev_t* device)
{
    fb_window_close((fb_window_t*)device->data);
    free(device->data);
}

static void window_reset(rvvm_mmio_dev_t* device)
{
    // Draw RVVM logo before guest takes over
    // Never ask why or how this works :D
    fb_ctx_t* fb = &((fb_window_t*)device->data)->fb;
    size_t bytes = rgb_format_bytes(fb->format);
    size_t stride = framebuffer_stride(fb);
    uint32_t pos_x = fb->width / 2 - 152;
    uint32_t pos_y = fb->height / 2 - 80;
    
    for (uint32_t y=0; y<fb->height; ++y) {
        size_t tmp_stride = stride * y;
        for (uint32_t x=0; x<fb->width; ++x) {
            uint8_t pix = 0;
            if (x >= pos_x && x - pos_x < 304 && y >= pos_y && y - pos_y < 160) {
                uint32_t pos = ((y - pos_y) >> 3) * 38 + ((x - pos_x) >> 3);
                pix = ((rvvm_logo_pix[pos >> 2] >> ((pos & 0x3) << 1)) & 0x3) << 6;
            }
            memset(((uint8_t*)fb->buffer) + tmp_stride + (x * bytes), pix, bytes);
        }
    }
}

static rvvm_mmio_type_t win_dev_type = {
    .name = "vm_window",
    .remove = window_remove,
    .update = window_update,
    .reset = window_reset,
};

bool fb_window_init_auto(rvvm_machine_t* machine, uint32_t width, uint32_t height)
{
    fb_window_t* window = safe_calloc(sizeof(fb_window_t), 1);
    window->fb.width = width;
    window->fb.height = height;
    window->fb.format = RGB_FMT_A8R8G8B8;
    window->machine = machine;
    window->keyboard = hid_keyboard_init_auto(machine);
    window->mouse = hid_mouse_init_auto(machine);
    hid_mouse_resolution(window->mouse, width, height);
    if (!fb_window_create(window)) {
        rvvm_error("Window creation failed");
        free(window);
        return false;
    }
    
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
