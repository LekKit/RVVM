/*
mtd-physmap.c - Memory Technology Device Mapping
Copyright (C) 2023  LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/


#include <rvvm/rvvm_blk.h>
#include <rvvm/rvvm_board.h>
#include <rvvm/rvvm_fdt.h>
#include <rvvm/rvvm_region.h>

#include <util/mem_ops.h>
#include <util/utils.h>

PUSH_OPTIMIZATION_SIZE

typedef struct {
    rvvm_blk_dev_t* blk;

    uint8_t  buffer[0x1000];
    uint32_t dirty;

    bool fw;
} mtd_ram_t;

TSAN_SUPPRESS
static void mtd_ram_read(rvvm_reg_dev_t* dev, void* data, size_t size, size_t off)
{
    mtd_ram_t* mtd = rvvm_region_data(dev);
    if ((off >> 12) != (rvvm_blk_tell_head(mtd->blk) >> 12)) {
        rvvm_blk_seek_head(mtd->blk, (off >> 12) << 12, RVVM_BLK_SEEK_SET);
        rvvm_blk_read_head(mtd->blk, mtd->buffer, sizeof(mtd->buffer));
    }
    memcpy(data, mtd->buffer + (off & 0xFFF), size);
}

TSAN_SUPPRESS
static void mtd_ram_write(rvvm_reg_dev_t* dev, const void* data, size_t size, size_t off)
{
    mtd_ram_t* mtd = rvvm_region_data(dev);
    if ((off >> 12) != (rvvm_blk_tell_head(mtd->blk) >> 12)) {
        rvvm_blk_write_head(mtd->blk, mtd->buffer, sizeof(mtd->buffer));
        rvvm_blk_seek_head(mtd->blk, (off >> 12) << 12, RVVM_BLK_SEEK_SET);
    }
    memcpy(mtd->buffer + (off & 0xFFF), data, size);
    atomic_store_uint32_relax(&mtd->dirty, true);
}

static void mtd_ram_reset(rvvm_reg_dev_t* dev)
{
    mtd_ram_t*      mtd     = rvvm_region_data(dev);
    rvvm_machine_t* machine = rvvm_region_machine(dev);
    if (mtd->fw) {
        rvvm_addr_t addr = rvvm_get_opt(machine, RVVM_OPT_RESET_PC);
        rvvm_addr_t size = rvvm_blk_get_size(mtd->blk);
        void*       ptr  = rvvm_get_dma_ptr(machine, addr, size);
        if (ptr) {
            rvvm_blk_read(mtd->blk, ptr, size, 0);
        }
    }
}

static void mtd_ram_suspend(rvvm_reg_dev_t* dev, rvvm_snapshot_t* snap, bool resume)
{
    mtd_ram_t* mtd = rvvm_region_data(dev);
    UNUSED(snap && resume);
    if (atomic_load_uint32_relax(&mtd->dirty)) {
        atomic_store_uint32_relax(&mtd->dirty, 0);
        rvvm_blk_write_head(mtd->blk, mtd->buffer, sizeof(mtd->buffer));
    }
}

static void mtd_ram_cleanup(rvvm_reg_dev_t* dev)
{
    mtd_ram_t* mtd = rvvm_region_data(dev);
    mtd_ram_suspend(dev, NULL, false);
    rvvm_blk_close(mtd->blk);
    free(mtd);
}

static const rvvm_reg_type_t mtd_ram_type = {
    .name    = "mtd-ram",
    .read    = mtd_ram_read,
    .write   = mtd_ram_write,
    .reset   = mtd_ram_reset,
    .suspend = mtd_ram_suspend,
    .cleanup = mtd_ram_cleanup,
};

RVVM_PUBLIC rvvm_reg_dev_t* rvvm_mtd_ram_init(rvvm_machine_t* machine, rvvm_blk_dev_t* blk, rvvm_addr_t addr, bool fw)
{
    mtd_ram_t* mtd = safe_new_obj(mtd_ram_t);

    rvvm_reg_desc_t desc = {
        .addr = addr,
        .size = rvvm_blk_get_size(mtd->blk),
        .data = mtd,
        .type = &mtd_ram_type,
    };

    mtd->blk = blk;
    mtd->fw  = fw;

    rvvm_blk_read_head(mtd->blk, mtd->buffer, sizeof(mtd->buffer));

    rvvm_reg_dev_t*  dev = rvvm_region_init_auto(machine, &desc);
    rvvm_fdt_node_t* soc = rvvm_get_fdt_soc(machine);

    if (dev && soc) {
        rvvm_fdt_node_t* fdt = rvvm_fdt_init_reg("flash", desc.addr);
        rvvm_fdt_prop_set_reg(fdt, "reg", desc.addr, desc.size);
        rvvm_fdt_prop_set_str(fdt, "compatible", "mtd-ram");
        rvvm_fdt_prop_set_u32(fdt, "bank-width", 0x1);
        rvvm_fdt_prop_set_u32(fdt, "#address-cells", 1);
        rvvm_fdt_prop_set_u32(fdt, "#size-cells", 1);
        {
            rvvm_fdt_node_t* part0  = rvvm_fdt_init_reg("partition", 0);
            uint32_t         reg[2] = {0, desc.size};
            rvvm_fdt_prop_set_cells(part0, "reg", reg, 2);
            rvvm_fdt_prop_set_str(part0, "label", "flash");
            rvvm_fdt_attach(fdt, part0);
        }
        rvvm_fdt_attach(soc, fdt);
    }
    return dev;
}

POP_OPTIMIZATION_SIZE
