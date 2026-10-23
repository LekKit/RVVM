/*
simple-fb.c - Simple Framebuffer
Copyright (C) 2021  LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <rvvm/rvvm_board.h>
#include <rvvm/rvvm_fb.h>
#include <rvvm/rvvm_fdt.h>
#include <rvvm/rvvm_region.h>
#include <rvvm/rvvm_snapshot.h>

#include <rvvm/rvvm.h>

#include "compiler.h"

PUSH_OPTIMIZATION_SIZE

static void simplefb_write(rvvm_reg_dev_t* dev, const void* data, size_t size, size_t off)
{
    rvvm_fbdev_t* fbdev = rvvm_region_data(dev);
    rvvm_fbdev_dirty(fbdev);
    UNUSED(data && size && off);
}

static void simplefb_poll(rvvm_reg_dev_t* dev)
{
    rvvm_fbdev_t* fbdev = rvvm_region_data(dev);
    rvvm_fbdev_update(fbdev);
}

static void simplefb_suspend(rvvm_reg_dev_t* dev, rvvm_snapshot_t* snap, bool resume)
{
    rvvm_fb_t fb = ZERO_INIT;
    if (snap) {
        rvvm_fbdev_t* fbdev = rvvm_region_data(dev);
        rvvm_fbdev_get_scanout(fbdev, &fb);
        rvvm_snapshot_section(snap, "framebuffer");
        rvvm_snapshot_data(snap, rvvm_fb_buffer(&fb), rvvm_fb_size(&fb));
    }
    UNUSED(resume);
}

static void simplefb_cleanup(rvvm_reg_dev_t* dev)
{
    rvvm_fbdev_t* fbdev = rvvm_region_data(dev);
    rvvm_fbdev_dec_ref(fbdev);
}

static const rvvm_reg_type_t simplefb_type = {
    .name    = "framebuffer",
    .write   = simplefb_write,
    .poll    = simplefb_poll,
    .suspend = simplefb_suspend,
    .cleanup = simplefb_cleanup,
};

RVVM_PUBLIC rvvm_reg_dev_t* rvvm_simplefb_init(rvvm_machine_t* machine, rvvm_fbdev_t* fbdev, rvvm_addr_t addr)
{
    rvvm_fb_t fb = ZERO_INIT;
    rvvm_fbdev_get_scanout(fbdev, &fb);

    if (!rvvm_fb_size(&fb) || !rvvm_fb_buffer(&fb)) {
        return NULL;
    }

    rvvm_reg_desc_t desc = {
        .addr = addr,
        .size = rvvm_fb_size(&fb),
        .data = fbdev,
        .mmap = rvvm_fb_buffer(&fb),
        .type = &simplefb_type,
    };

    rvvm_reg_dev_t*  dev = rvvm_region_init_auto(machine, &desc);
    rvvm_fdt_node_t* soc = rvvm_get_fdt_soc(machine);

    if (dev && soc) {
        rvvm_fdt_node_t* fdt = rvvm_fdt_init_reg("framebuffer", desc.addr);
        rvvm_fdt_prop_set_reg(fdt, "reg", desc.addr, desc.size);
        rvvm_fdt_prop_set_str(fdt, "compatible", "simple-framebuffer");
        switch (rvvm_fb_format(&fb)) {
            case RVVM_RGB_RGB565:
                rvvm_fdt_prop_set_str(fdt, "format", "r5g6b5");
                break;
            case RVVM_RGB_RGB888:
                rvvm_fdt_prop_set_str(fdt, "format", "r8g8b8");
                break;
            case RVVM_RGB_BGR888:
                rvvm_fdt_prop_set_str(fdt, "format", "b8g8r8");
                break;
            case RVVM_RGB_XRGB8888:
                rvvm_fdt_prop_set_str(fdt, "format", "a8r8g8b8");
                break;
            case RVVM_RGB_XBGR8888:
                rvvm_fdt_prop_set_str(fdt, "format", "a8b8g8r8");
                break;
        }
        rvvm_fdt_prop_set_u32(fdt, "width", rvvm_fb_width(&fb));
        rvvm_fdt_prop_set_u32(fdt, "height", rvvm_fb_height(&fb));
        rvvm_fdt_prop_set_u32(fdt, "stride", rvvm_fb_stride(&fb));

        rvvm_fdt_attach(soc, fdt);
    }

    return dev;
}

POP_OPTIMIZATION_SIZE
