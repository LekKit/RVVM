/*
bochs-display.c - Bochs Display
Copyright (C) 2025  LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

/*
 * TODO: Snapshots
 */

#include <rvvm/rvvm_board.h>
#include <rvvm/rvvm_fb.h>
#include <rvvm/rvvm_pci.h>
#include <rvvm/rvvm_region.h>
#include <rvvm/rvvm_snapshot.h>

#include <util/atomics.h>
#include <util/mem_ops.h>
#include <util/utils.h>
#include <util/vma_ops.h>

PUSH_OPTIMIZATION_SIZE

typedef struct {
    rvvm_fbdev_t* fbdev;

    uint32_t version;
    uint32_t enable;
    uint32_t xres;
    uint32_t yres;
    uint32_t xvirt;
    uint32_t yvirt;
    uint32_t xoff;
    uint32_t yoff;
} bochs_display_t;

/*
 * Bochs Display registers
 */
#define BOCHS_REG_ID          0x0500 // Version ID (0xBOC0 - 0xBOC5)
#define BOCHS_REG_XRES        0x0502 // X Resolution
#define BOCHS_REG_YRES        0x0504 // Y Resolution
#define BOCHS_REG_BPP         0x0506 // Bits per pixel (32bpp is XRGB8888)
#define BOCHS_REG_ENABLE      0x0508 // Enable register
#define BOCHS_REG_BANK        0x050A // Current 64KiB VRAM bank exposed at 0xA0000 (x86 VGA only)
#define BOCHS_REG_VIRT_WIDTH  0x050C // Framebuffer stride
#define BOCHS_REG_VIRT_HEIGHT 0x050E // Not meaningful, should be set >= YRES by the guest
#define BOCHS_REG_X_OFFSET    0x0510 // Framebuffer X offset in VRAM
#define BOCHS_REG_Y_OFFSET    0x0512 // Framebuffer Y offset in VRAM
#define BOCHS_REG_VRAM        0x0514 // VRAM size (In 64KiB units, i.e. shifted right by 16)

/*
 * Extended registers (Added in device PCI Revision 2)
 */
#define BOCHS_REG_QEXT_SIZE   0x0600 // Extended registers region size, should be 8
#define BOCHS_REG_ENDIAN_LO   0x0604 // Framebuffer endianness register, should be 0x1E1E
#define BOCHS_REG_ENDIAN_HI   0x0606 // Framebuffer endianness register, should be 0x1E1E

/*
 * Bochs Display verions in ID register
 */
#define BOCHS_VER_ID0         0xB0C0 // Bochs VBE version 0
#define BOCHS_VER_ID5         0xB0C5 // Bochs VBE version 5

/*
 * Enable register bits
 */
#define BOCHS_ENABLE          0x0001 // Enable the display engine, applies XRES/YRES/BPP and disallows writes to them
#define BOCHS_ENABLE_CAPS     0x0002 // Enable capabilities (XRES/YRES/BPP report max values instead)
#define BOCHS_ENABLE_8BIT     0x0020 // Enable 8-bit DAC (x86 VGA only)
#define BOCHS_ENABLE_LFB      0x0040 // Enable Linear Framebuffer in BAR 0 (x86 VGA only)
#define BOCHS_ENABLE_NOCLR    0x0080 // Do not zero VRAM on enable
#define BOCHS_ENABLE_MASK     0x00E3 // Mask of valid bits

/*
 * Bochs display VRAM size
 */
#define BOCHS_VRAM_SIZE       0x1000000UL

static void bochs_display_update_mode(bochs_display_t* disp, bool upd_res)
{
    rvvm_fb_t fb   = ZERO_INIT;
    uint8_t*  vram = rvvm_fbdev_get_vram(disp->fbdev, NULL);
    uint32_t  off  = atomic_load_uint32_relax(&disp->xoff);
    rvvm_fbdev_get_scanout(disp->fbdev, &fb);
    if (upd_res) {
        fb.width  = atomic_load_uint32_relax(&disp->xres);
        fb.height = atomic_load_uint32_relax(&disp->yres);
    }
    fb.stride = EVAL_MAX(atomic_load_uint32_relax(&disp->xvirt), fb.width * 4);
    fb.format = RVVM_RGB_XRGB8888;
    fb.buffer = vram + off + (atomic_load_uint32_relax(&disp->yoff) * fb.stride);
    rvvm_fbdev_set_scanout(disp->fbdev, &fb);
}

static void bochs_display_read(rvvm_reg_dev_t* dev, void* data, size_t size, size_t off)
{
    bochs_display_t* disp = rvvm_region_data(dev);
    uint16_t         val  = 0;
    UNUSED(size);

    switch (off) {
        case BOCHS_REG_ID:
            val = atomic_load_uint32_relax(&disp->version);
            if (val < BOCHS_VER_ID0 || val > BOCHS_VER_ID5) {
                val = BOCHS_VER_ID5;
            }
            break;
        case BOCHS_REG_XRES:
            if (atomic_load_uint32_relax(&disp->enable) & BOCHS_ENABLE_CAPS) {
                val = 2560;
            } else {
                val = atomic_load_uint32_relax(&disp->xres);
            }
            break;
        case BOCHS_REG_YRES:
            if (atomic_load_uint32_relax(&disp->enable) & BOCHS_ENABLE_CAPS) {
                val = 1440;
            } else {
                val = atomic_load_uint32_relax(&disp->yres);
            }
            break;
        case BOCHS_REG_BPP:
            // NOTE: Only claim bpp32 (XRGB8888) support for now
            val = 32;
            break;
        case BOCHS_REG_ENABLE:
            val = atomic_load_uint32_relax(&disp->enable);
            break;
        case BOCHS_REG_VIRT_WIDTH:
            val = atomic_load_uint32_relax(&disp->xvirt);
            break;
        case BOCHS_REG_VIRT_HEIGHT:
            val = atomic_load_uint32_relax(&disp->yvirt);
            break;
        case BOCHS_REG_X_OFFSET:
            val = atomic_load_uint32_relax(&disp->xoff);
            break;
        case BOCHS_REG_Y_OFFSET:
            val = atomic_load_uint32_relax(&disp->yoff);
            break;
        case BOCHS_REG_VRAM: {
            size_t vram_size = BOCHS_VRAM_SIZE;
            rvvm_fbdev_get_vram(disp->fbdev, &vram_size);
            val = vram_size >> 16;
            break;
        }
        case BOCHS_REG_QEXT_SIZE:
            val = 8;
            break;
        case BOCHS_REG_ENDIAN_LO:
        case BOCHS_REG_ENDIAN_HI:
            val = 0x1E1EU;
            break;
    }

    write_uint16_le(data, val);
}

static void bochs_display_write(rvvm_reg_dev_t* dev, const void* data, size_t size, size_t off)
{
    bochs_display_t* disp = rvvm_region_data(dev);
    uint16_t         val  = read_uint16_le(data);
    UNUSED(size);

    switch (off) {
        case BOCHS_REG_ID:
            atomic_store_uint32_relax(&disp->version, val);
            break;
        case BOCHS_REG_XRES:
            if (!(atomic_load_uint32_relax(&disp->enable) & BOCHS_ENABLE)) {
                atomic_store_uint32_relax(&disp->xres, val);
            }
            break;
        case BOCHS_REG_YRES:
            if (!(atomic_load_uint32_relax(&disp->enable) & BOCHS_ENABLE)) {
                atomic_store_uint32_relax(&disp->yres, val);
            }
            break;
        case BOCHS_REG_VIRT_WIDTH:
            atomic_store_uint32_relax(&disp->xvirt, val);
            bochs_display_update_mode(disp, false);
            break;
        case BOCHS_REG_VIRT_HEIGHT:
            atomic_store_uint32_relax(&disp->yvirt, val);
            break;
        case BOCHS_REG_X_OFFSET:
            atomic_store_uint32_relax(&disp->xoff, val);
            bochs_display_update_mode(disp, false);
            break;
        case BOCHS_REG_Y_OFFSET:
            atomic_store_uint32_relax(&disp->yoff, val);
            bochs_display_update_mode(disp, false);
            break;
        case BOCHS_REG_ENABLE:
            atomic_store_uint32_relax(&disp->enable, val & BOCHS_ENABLE_MASK);
            if (val & BOCHS_ENABLE) {
                if (!(val & BOCHS_ENABLE_NOCLR)) {
                    // Clear VRAM
                    size_t vsiz = 0;
                    void*  vram = rvvm_fbdev_get_vram(disp->fbdev, &vsiz);
                    vma_clean(vram, vsiz, false);
                }
                // Fully update video mode
                bochs_display_update_mode(disp, true);
            }
            break;
    }
}

static void bochs_vram_write(rvvm_reg_dev_t* dev, const void* data, size_t size, size_t off)
{
    bochs_display_t* disp = rvvm_region_data(dev);
    rvvm_fbdev_dirty(disp->fbdev);
    UNUSED(data && off && size);
}

static void bochs_display_cleanup(rvvm_reg_dev_t* dev)
{
    bochs_display_t* disp = rvvm_region_data(dev);
    if (rvvm_fbdev_dec_ref(disp->fbdev)) {
        safe_free(disp);
    }
}

static void bochs_display_poll(rvvm_reg_dev_t* dev)
{
    bochs_display_t* disp = rvvm_region_data(dev);
    rvvm_fbdev_update(disp->fbdev);
}

static const rvvm_reg_type_t bochs_vram_type = {
    .name    = "bochs-vram",
    .write   = bochs_vram_write,
    .cleanup = bochs_display_cleanup,
};

static const rvvm_reg_type_t bochs_display_type = {
    .name     = "bochs-display",
    .read     = bochs_display_read,
    .write    = bochs_display_write,
    .poll     = bochs_display_poll,
    .cleanup  = bochs_display_cleanup,
    .min_size = 2,
    .max_size = 2,
};

rvvm_pci_func_t* rvvm_bochs_display_init(rvvm_machine_t* machine, rvvm_fbdev_t* fbdev, rvvm_pci_addr_t addr)
{
    if (!fbdev) {
        return NULL;
    }

    bochs_display_t* disp = safe_new_obj(bochs_display_t);

    size_t vram_size = BOCHS_VRAM_SIZE;
    void*  vram      = rvvm_fbdev_get_vram(fbdev, &vram_size);

    // Handle is released twice
    rvvm_fbdev_inc_ref(fbdev);
    disp->fbdev = fbdev;

    rvvm_reg_desc_t bochs_vram = {
        .size = vram_size,
        .data = disp,
        .mmap = vram,
        .type = &bochs_vram_type,
    };
    rvvm_reg_desc_t bochs_disp = {
        .size = 0x1000,
        .data = disp,
        .type = &bochs_display_type,
    };
    rvvm_pci_func_desc_t bochs_desc = {
        .vendor_id  = 0x1234, // Not in PCI ID database yet, should be Bochs
        .device_id  = 0x1111, // Not in PCI ID database yet, should be Bochs-Display
        .class_code = 0x0380, // Display controller, Legacy-free (no VGA)
        .revision   = 0x02,   // Rev. 2
        .bar[0]     = &bochs_vram,
        .bar[2]     = &bochs_disp,
    };

    return rvvm_pci_func_init(machine, &bochs_desc, addr);
}

POP_OPTIMIZATION_SIZE
