/*
mtd-physmap.c - Memory Technology Device Mapping
Copyright (C) 2023  LekKit <github.com/LekKit>

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

#include "mtd-physmap.h"
#include "blk_io.h"
#include "fdtlib.h"
#include "utils.h"

typedef struct {
    blkdev_t* blk;
} mtd_dev_t;

static void mtd_remove(rvvm_mmio_dev_t* dev)
{
    mtd_dev_t* mtd = dev->data;
    blk_close(mtd->blk);
    free(mtd);
}

static void mtd_reset(rvvm_mmio_dev_t* dev)
{
    mtd_dev_t* mtd = dev->data;
    void* ptr = rvvm_get_dma_ptr(dev->machine, rvvm_get_opt(dev->machine, RVVM_OPT_MEM_BASE), blk_getsize(mtd->blk));
    if (ptr) blk_read(mtd->blk, ptr, blk_getsize(mtd->blk), 0);
}

static rvvm_mmio_type_t mtd_type = {
    .name = "mtd_physmap",
    .remove = mtd_remove,
    .reset = mtd_reset,
};

static bool mtd_mmio_read(rvvm_mmio_dev_t* dev, void* data, size_t offset, uint8_t size)
{
    mtd_dev_t* mtd = dev->data;
    return blk_read(mtd->blk, data, size, offset) == size;
}

static bool mtd_mmio_write(rvvm_mmio_dev_t* dev, void* data, size_t offset, uint8_t size)
{
    mtd_dev_t* mtd = dev->data;
    return blk_write(mtd->blk, data, size, offset) == size;
}

PUBLIC rvvm_mmio_handle_t mtd_physmap_init_blk(rvvm_machine_t* machine, rvvm_addr_t addr, void* blk_dev)
{
    mtd_dev_t* mtd = safe_new_obj(mtd_dev_t);
    mtd->blk = blk_dev;

    rvvm_mmio_dev_t mtd_mmio = {
        .addr = addr,
        .size = blk_getsize(mtd->blk),
        .min_op_size = 1,
        .max_op_size = 8,
        .read = mtd_mmio_read,
        .write = mtd_mmio_write,
        .data = mtd,
        .type = &mtd_type,
    };
    rvvm_mmio_handle_t handle = rvvm_attach_mmio(machine, &mtd_mmio);
    if (handle == RVVM_INVALID_MMIO) return handle;
#ifdef USE_FDT
    struct fdt_node* mtd_fdt = fdt_node_create_reg("flash", mtd_mmio.addr);
    fdt_node_add_prop_reg(mtd_fdt, "reg", mtd_mmio.addr, mtd_mmio.size);
    fdt_node_add_prop_str(mtd_fdt, "compatible", "mtd-ram");
    fdt_node_add_prop_u32(mtd_fdt, "bank-width", 0x1);
    {
        struct fdt_node* partition0 = fdt_node_create("partition@0");
        fdt_node_add_prop_reg(partition0, "reg", 0, mtd_mmio.size);
        fdt_node_add_prop_str(partition0, "label", "firmware");
        fdt_node_add_child(mtd_fdt, partition0);
    }
    fdt_node_add_child(rvvm_get_fdt_soc(machine), mtd_fdt);
#endif
    return handle;
}

PUBLIC rvvm_mmio_handle_t mtd_physmap_init(rvvm_machine_t* machine, rvvm_addr_t addr, const char* image_path, bool rw)
{
    blkdev_t* blk = blk_open(image_path, rw ? BLKDEV_RW : 0);
    if (blk == NULL) return RVVM_INVALID_MMIO;
    return mtd_physmap_init_blk(machine, addr, blk);
}

PUBLIC rvvm_mmio_handle_t mtd_physmap_init_auto(rvvm_machine_t* machine, const char* image_path, bool rw)
{
    return mtd_physmap_init(machine, MTD_PHYSMAP_DEFAULT_MMIO, image_path, rw);
}
