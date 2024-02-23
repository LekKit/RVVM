/*
framebuffer.h - Framebuffer context, RGB format handling
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
 
#ifndef RVVM_FRAMEBUFFER_H
#define RVVM_FRAMEBUFFER_H

#include "rvvmlib.h"

#define RGB_FMT_INVALID  0x0
#define RGB_FMT_R5G6B5   0x02
#define RGB_FMT_R8G8B8   0x03
#define RGB_FMT_A8R8G8B8 0x04 // Little-endian: BGRA, Big-endian: ARGB
#define RGB_FMT_A8B8G8R8 0x14 // Little-endian: RGBA, Big-endian: ABGR

typedef uint8_t rgb_fmt_t;

typedef struct {
    void*     buffer;
    uint32_t  width;
    uint32_t  height;
    uint32_t  stride;
    rgb_fmt_t format;
} fb_ctx_t;

static inline size_t rgb_format_bytes(rgb_fmt_t format)
{
    switch (format) {
        case RGB_FMT_R5G6B5:   return 2;
        case RGB_FMT_R8G8B8:   return 3;
        case RGB_FMT_A8R8G8B8: return 4;
        case RGB_FMT_A8B8G8R8: return 4;
    }
    return 0;
}

static inline size_t rgb_format_bpp(rgb_fmt_t format)
{
    return rgb_format_bytes(format) << 3;
}

static inline size_t rgb_format_from_bpp(size_t bpp)
{
    switch (bpp) {
        case 16: return RGB_FMT_R5G6B5;
        case 24: return RGB_FMT_R8G8B8;
        // Default to ARGB when bpp = 32,
        // this is what most standards suppose
        case 32: return RGB_FMT_A8R8G8B8;
    }
    return RGB_FMT_INVALID;
}

static inline size_t framebuffer_stride(const fb_ctx_t* fb)
{
    return fb->stride ? fb->stride : fb->width * rgb_format_bytes(fb->format);
}

static inline size_t framebuffer_size(const fb_ctx_t* fb)
{
    return framebuffer_stride(fb) * fb->height;
}

// Attach initialized framebuffer context to the machine
// The buffer is not freed automatically
PUBLIC rvvm_mmio_handle_t framebuffer_init(rvvm_machine_t* machine, rvvm_addr_t addr, const fb_ctx_t* fb);
PUBLIC rvvm_mmio_handle_t framebuffer_init_auto(rvvm_machine_t* machine, const fb_ctx_t* fb);

#endif
