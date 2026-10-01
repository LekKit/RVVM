/*
rvvm_region.c - RVVM Region-based devices
Copyright (C) 2020-2026 LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <rvvm/rvvm.h>
#include <rvvm/rvvm_region.h>

#include "mem_ops.h"
#include "utils.h"

/*
 * This is a temporary implementation atop legacy rvvm_mmio_dev interface
 */

PUSH_OPTIMIZATION_SIZE

struct rvvm_reg_dev {
    rvvm_machine_t*  machine;
    rvvm_mmio_dev_t* mmio;
    rvvm_mmio_type_t type;
    rvvm_reg_desc_t  desc;
};

static bool rvvm_region_legacy_read(rvvm_mmio_dev_t* mmio, void* data, size_t off, uint8_t size)
{
    rvvm_reg_dev_t* dev = (rvvm_reg_dev_t*)mmio->data;
    memset(data, 0, size);
    if (dev->desc.type && dev->desc.type->read) {
        dev->desc.type->read(dev, data, size, off);
    }
    return true;
}

static bool rvvm_region_legacy_write(rvvm_mmio_dev_t* mmio, void* data, size_t off, uint8_t size)
{
    rvvm_reg_dev_t* dev = (rvvm_reg_dev_t*)mmio->data;
    if (dev->desc.type && dev->desc.type->write) {
        dev->desc.type->write(dev, data, size, off);
    }
    return true;
}

static void rvvm_region_legacy_remove(rvvm_mmio_dev_t* mmio)
{
    rvvm_reg_dev_t*   dev  = (rvvm_reg_dev_t*)mmio->data;
    rvvm_mmio_type_t* type = NONCONST_CAST(rvvm_mmio_type_t*, mmio->type);
    if (dev->desc.type && dev->desc.type->cleanup) {
        dev->desc.type->cleanup(dev);
    }
    free(dev);
    free(type);
}

static void rvvm_region_legacy_update(rvvm_mmio_dev_t* mmio)
{
    rvvm_reg_dev_t* dev = (rvvm_reg_dev_t*)mmio->data;
    if (dev->desc.type && dev->desc.type->poll) {
        dev->desc.type->poll(dev);
    }
}

static void rvvm_region_legacy_reset(rvvm_mmio_dev_t* mmio)
{
    rvvm_reg_dev_t* dev = (rvvm_reg_dev_t*)mmio->data;
    if (dev->desc.type && dev->desc.type->reset) {
        dev->desc.type->reset(dev);
    }
}

static void rvvm_region_legacy_suspend(rvvm_mmio_dev_t* mmio, rvvm_snapshot_t* snap, bool resume)
{
    rvvm_reg_dev_t* dev = (rvvm_reg_dev_t*)mmio->data;
    if (dev->desc.type && dev->desc.type->suspend) {
        dev->desc.type->suspend(dev, snap, resume);
    }
}

RVVM_PUBLIC rvvm_reg_dev_t* rvvm_region_init(rvvm_machine_t* machine, const rvvm_reg_desc_t* desc)
{
    rvvm_reg_dev_t* dev = safe_new_obj(rvvm_reg_dev_t);

    dev->machine = machine;
    dev->desc    = *desc;

    // TODO Port IO handling
    if (machine) {
        rvvm_mmio_type_t* mmio_type = safe_new_obj(rvvm_mmio_type_t);

        rvvm_mmio_dev_t mmio_desc = {
            .addr    = desc->addr,
            .size    = desc->size,
            .data    = dev,
            .mapping = desc->mmap,
            .type    = mmio_type,
            .read    = rvvm_region_legacy_read,
            .write   = rvvm_region_legacy_write,
        };
        if (!(desc->attr & RVVM_REG_ATTR_FIX)) {
            mmio_desc.addr = rvvm_mmio_zone_auto(machine, mmio_desc.addr, mmio_desc.size);
            dev->desc.addr = mmio_desc.addr;
        }
        if (desc->attr & RVVM_REG_ATTR_ROM) {
            mmio_desc.write = NULL;
        }
        if (desc->type) {
            mmio_type->name       = desc->type->name;
            mmio_desc.min_op_size = desc->type->min_size;
            mmio_desc.max_op_size = desc->type->max_size;
        }
        mmio_type->remove     = rvvm_region_legacy_remove;
        mmio_type->update     = rvvm_region_legacy_update;
        mmio_type->reset      = rvvm_region_legacy_reset;
        mmio_type->suspend    = rvvm_region_legacy_suspend;
        rvvm_mmio_dev_t* mmio = rvvm_attach_mmio(machine, &mmio_desc);
        if (mmio) {
            dev->mmio = mmio;
            return dev;
        }
    } else if (dev->desc.type && dev->desc.type->cleanup) {
        dev->desc.type->cleanup(dev);
        free(dev);
    }
    return NULL;
}

RVVM_PUBLIC void rvvm_region_remove(rvvm_reg_dev_t* dev)
{
    if (dev) {
        rvvm_remove_mmio(dev->mmio);
    }
}

RVVM_PUBLIC void* rvvm_region_data(rvvm_reg_dev_t* dev)
{
    if (dev) {
        return dev->desc.data;
    }
    return NULL;
}

RVVM_PUBLIC rvvm_machine_t* rvvm_region_machine(rvvm_reg_dev_t* dev)
{
    if (dev) {
        return dev->machine;
    }
    return NULL;
}

RVVM_PUBLIC bool rvvm_region_get_desc(rvvm_reg_dev_t* dev, rvvm_reg_desc_t* desc)
{
    if (dev) {
        *desc = dev->desc;
        return true;
    }
    return false;
}

RVVM_PUBLIC bool rvvm_region_set_desc(rvvm_reg_dev_t* dev, const rvvm_reg_desc_t* desc)
{
    if (dev) {
        dev->desc          = *desc;
        dev->mmio->addr    = desc->addr;
        dev->mmio->size    = desc->size;
        dev->mmio->mapping = desc->mmap;
        return true;
    }
    return false;
}

POP_OPTIMIZATION_SIZE
