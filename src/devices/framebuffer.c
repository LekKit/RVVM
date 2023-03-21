/*
framebuffer.c - Simple Framebuffer
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

#include "framebuffer.h"
#include "utils.h"

#ifdef USE_FDT
#include "fdtlib.h"
#endif

static void fb_remove(rvvm_mmio_dev_t* device)
{
    UNUSED(device);
}

static rvvm_mmio_type_t fb_dev_type = {
    .name = "framebuffer",
    .remove = fb_remove,
};

PUBLIC void framebuffer_init(rvvm_machine_t* machine, rvvm_addr_t addr, const fb_ctx_t* fb)
{
    rvvm_mmio_dev_t fb_region = {0};
    
    // Map the framebuffer into physical memory
    fb_region.data = fb->buffer;
    fb_region.addr = addr;
    fb_region.size = framebuffer_size(fb);
    fb_region.type = &fb_dev_type;
    rvvm_attach_mmio(machine, &fb_region);
#ifdef USE_FDT
    struct fdt_node* fb_fdt = fdt_node_create_reg("framebuffer", addr);
    fdt_node_add_prop_reg(fb_fdt, "reg", addr, fb_region.size);
    fdt_node_add_prop_str(fb_fdt, "compatible", "simple-framebuffer");
    switch (fb->format) {
        case RGB_FMT_R5G6B5: 
            fdt_node_add_prop_str(fb_fdt, "format", "r5g6b5");
            break;
        case RGB_FMT_R8G8B8: 
            fdt_node_add_prop_str(fb_fdt, "format", "r8g8b8");
            break;
        case RGB_FMT_A8R8G8B8: 
            fdt_node_add_prop_str(fb_fdt, "format", "a8r8g8b8");
            break;
        case RGB_FMT_A8B8G8R8: 
            fdt_node_add_prop_str(fb_fdt, "format", "a8b8g8r8");
            break;
        default:
            rvvm_warn("Unknown RGB format in framebuffer_init()!");
            break;
    }
    fdt_node_add_prop_u32(fb_fdt, "width",  fb->width);
    fdt_node_add_prop_u32(fb_fdt, "height", fb->height);
    fdt_node_add_prop_u32(fb_fdt, "stride", fb->width * rgb_format_bytes(fb->format));

    fdt_node_add_child(rvvm_get_fdt_soc(machine), fb_fdt);
#endif
}

PUBLIC void framebuffer_init_auto(rvvm_machine_t* machine, const fb_ctx_t* fb)
{
    rvvm_addr_t addr = rvvm_mmio_zone_auto(machine, 0x28000000, framebuffer_size(fb));
    framebuffer_init(machine, addr, fb);
    rvvm_append_cmdline(machine, "console=tty0");
}
