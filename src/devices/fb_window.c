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

static void fb_remove(rvvm_mmio_dev_t* device)
{
    UNUSED(device);
}

static void window_update(rvvm_mmio_dev_t* device)
{
    fb_update((struct fb_data*)device->data);
}

static void window_remove(rvvm_mmio_dev_t* device)
{
    fb_close_window((struct fb_data*)device->data);
    free(device->data);
}

static rvvm_mmio_type_t fb_dev_type = {
    .name = "framebuffer",
    .remove = fb_remove,
};

static rvvm_mmio_type_t win_dev_type = {
    .name = "vm_window",
    .remove = window_remove,
    .update = window_update,
};

void init_fb(rvvm_machine_t* machine, paddr_t addr, uint32_t width, uint32_t height, struct ps2_device *mouse, struct ps2_device *keyboard)
{
    struct fb_data* data = safe_calloc(sizeof(struct fb_data), 1);
    uint32_t fb_size = width * height * 4;
    rvvm_mmio_dev_t fb_region = {0};
    rvvm_mmio_dev_t win_placeholder = {0};
    
    data->mouse = mouse;
    data->keyboard = keyboard;
    fb_create_window(data, width, height, "RVVM");
    if (data->framebuffer == NULL) {
        /* Initialize empty buffer so that the MMIO handler would not crash */
        data->framebuffer = safe_calloc(1, fb_size);
    }
    
    // Map the framebuffer into memory
    fb_region.data = data->framebuffer;
    fb_region.begin = addr;
    fb_region.end = addr + fb_size;
    fb_region.type = &fb_dev_type;
    rvvm_attach_mmio(machine, &fb_region);
    
    // Placeholder for window data, region size is 0
    win_placeholder.data = data;
    win_placeholder.type = &win_dev_type;
    rvvm_attach_mmio(machine, &win_placeholder);

#ifdef USE_FDT
    struct fdt_node* soc = fdt_node_find(machine->fdt, "soc");
    if (soc == NULL) {
        rvvm_warn("Missing soc node in FDT!");
        return;
    }
    
    struct fdt_node* fb = fdt_node_create_reg("framebuffer", addr);
    fdt_node_add_prop_reg(fb, "reg", addr, fb_size);
    fdt_node_add_prop_str(fb, "compatible", "simple-framebuffer");
    fdt_node_add_prop_str(fb, "format", "a8r8g8b8");
    fdt_node_add_prop_u32(fb, "width", width);
    fdt_node_add_prop_u32(fb, "height", height);
    fdt_node_add_prop_u32(fb, "stride", width * 4);
    
    fdt_node_add_child(soc, fb);
#endif
}
