/*
fb_window.c - Framebuffer window device
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

#include "riscv32.h"
#include "riscv32_mmu.h"
#include "fb_window.h"

void r5g6b5_to_a8r8g8b8(const void* _in, void* _out, size_t length)
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
}

static bool fb_mmio_handler(rvvm_hart_t* vm, riscv32_mmio_device_t* device, uint32_t offset, void* data, uint32_t size, uint8_t op)
{
    char* devptr = ((char*)device->data) + offset;
    char* dataptr = (char*)data;

    char *destptr;
    const char *srcptr;

    UNUSED(vm);
    if (op == MMU_WRITE) {
        destptr = devptr;
        srcptr = dataptr;
    } else {
        destptr = dataptr;
        srcptr = devptr;
    }

    memcpy(destptr, srcptr, size);
    return true;
}

void init_fb(rvvm_hart_t* vm, struct fb_data *data, unsigned width, unsigned height, uint32_t addr, struct ps2_device *mouse, struct ps2_device *keyboard)
{
    uint32_t fb_size = width * height * 4;
    data->mouse = mouse;
    data->keyboard = keyboard;
    fb_create_window(data, width, height, "RVVM");
    if (data->framebuffer == NULL) {
        /* Initialize empty buffer so that the MMIO handler would not crash */
        data->framebuffer = safe_calloc(1, fb_size);
    }
    riscv32_mmio_add_device(vm, addr, addr + fb_size, fb_mmio_handler, data->framebuffer);
}
