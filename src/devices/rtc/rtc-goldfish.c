/*
rtc-goldfish.c - Goldfish Real-time Clock
Copyright (C) 2021  LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <rvvm/rvvm_board.h>
#include <rvvm/rvvm_fdt.h>
#include <rvvm/rvvm_irq.h>
#include <rvvm/rvvm_region.h>
#include <rvvm/rvvm_snapshot.h>

#include <util/mem_ops.h>
#include <util/rvtimer.h>
#include <util/utils.h>

PUSH_OPTIMIZATION_SIZE

#define RTC_TIME_LOW     0x00
#define RTC_TIME_HIGH    0x04
#define RTC_ALARM_LOW    0x08
#define RTC_ALARM_HIGH   0x0C
#define RTC_IRQ_ENABLED  0x10
#define RTC_ALARM_CLEAR  0x14
#define RTC_ALARM_STATUS 0x18
#define RTC_IRQ_CLEAR    0x1C

typedef struct {
    rvvm_irq_dev_t* irq_dev;
    rvvm_irq_t      irq;

    uint32_t time_low;
    uint32_t time_high;
    uint32_t alarm_low;
    uint32_t alarm_high;
    uint32_t alarm_enabled;
    uint32_t irq_enabled;
} rtc_goldfish_dev_t;

static void rtc_goldfish_update(rtc_goldfish_dev_t* rtc)
{
    uint64_t timer64 = rvtimer_unixtime() * 1000000000ULL;
    atomic_store_uint32_relax(&rtc->time_low, timer64);
    atomic_store_uint32_relax(&rtc->time_high, timer64 >> 32);

    if (atomic_load_uint32_relax(&rtc->alarm_enabled) && atomic_load_uint32_relax(&rtc->irq_enabled)) {
        uint64_t alarm64  = atomic_load_uint32_relax(&rtc->alarm_low);
        alarm64          |= (((uint64_t)atomic_load_uint32_relax(&rtc->alarm_high)) << 32);
        if (timer64 >= alarm64) {
            rvvm_irq_raise(rtc->irq_dev, rtc->irq);
        }
    } else {
        rvvm_irq_lower(rtc->irq_dev, rtc->irq);
    }
}

static void rtc_goldfish_mmio_read(rvvm_reg_dev_t* dev, void* data, size_t size, size_t off)
{
    rtc_goldfish_dev_t* rtc = rvvm_region_data(dev);
    uint32_t            val = 0;
    UNUSED(size);

    switch (off) {
        case RTC_TIME_LOW:
            rtc_goldfish_update(rtc);
            val = atomic_load_uint32_relax(&rtc->time_low);
            break;
        case RTC_TIME_HIGH:
            val = atomic_load_uint32_relax(&rtc->time_high);
            break;
        case RTC_ALARM_LOW:
            val = atomic_load_uint32_relax(&rtc->alarm_low);
            break;
        case RTC_ALARM_HIGH:
            val = atomic_load_uint32_relax(&rtc->alarm_high);
            break;
        case RTC_IRQ_ENABLED:
            val = atomic_load_uint32_relax(&rtc->irq_enabled);
            break;
        case RTC_ALARM_STATUS:
            val = atomic_load_uint32_relax(&rtc->alarm_enabled);
            break;
    }

    write_uint32_le(data, val);
}

static void rtc_goldfish_mmio_write(rvvm_reg_dev_t* dev, const void* data, size_t size, size_t off)
{
    rtc_goldfish_dev_t* rtc = rvvm_region_data(dev);
    uint32_t            val = read_uint32_le(data);
    UNUSED(size);

    switch (off) {
        case RTC_ALARM_LOW:
            atomic_store_uint32_relax(&rtc->alarm_enabled, true);
            atomic_store_uint32_relax(&rtc->alarm_low, val);
            rtc_goldfish_update(rtc);
            break;
        case RTC_ALARM_HIGH:
            atomic_store_uint32_relax(&rtc->alarm_high, val);
            break;
        case RTC_IRQ_ENABLED:
            atomic_store_uint32_relax(&rtc->irq_enabled, val & 1);
            rtc_goldfish_update(rtc);
            break;
        case RTC_ALARM_CLEAR:
            atomic_store_uint32_relax(&rtc->alarm_enabled, false);
            rtc_goldfish_update(rtc);
            break;
        case RTC_IRQ_CLEAR:
            rtc_goldfish_update(rtc);
            break;
    }
}

static void rtc_goldfish_suspend(rvvm_reg_dev_t* dev, rvvm_snapshot_t* snap, bool resume)
{
    if (snap) {
        rtc_goldfish_dev_t* rtc = rvvm_region_data(dev);
        rvvm_snapshot_section(snap, "rtc-goldfish");
        rvvm_snapshot_field(snap, rtc->time_low);
        rvvm_snapshot_field(snap, rtc->time_high);
        rvvm_snapshot_field(snap, rtc->alarm_low);
        rvvm_snapshot_field(snap, rtc->alarm_high);
        rvvm_snapshot_field(snap, rtc->alarm_enabled);
        rvvm_snapshot_field(snap, rtc->irq_enabled);
    }
    UNUSED(resume);
}

static void rtc_goldfish_cleanup(rvvm_reg_dev_t* dev)
{
    rtc_goldfish_dev_t* rtc = rvvm_region_data(dev);
    rvvm_irq_dealloc(rtc->irq_dev, rtc->irq);
    free(rtc);
}

static const rvvm_reg_type_t rtc_goldfish_type = {
    .name     = "rtc-goldfish",
    .read     = rtc_goldfish_mmio_read,
    .write    = rtc_goldfish_mmio_write,
    .suspend  = rtc_goldfish_suspend,
    .cleanup  = rtc_goldfish_cleanup,
    .min_size = 4,
    .max_size = 4,
};

RVVM_PUBLIC rvvm_reg_dev_t* rvvm_rtc_goldfish_init(rvvm_machine_t* machine, //
                                                   rvvm_addr_t     addr,    //
                                                   rvvm_irq_dev_t* irq_dev, //
                                                   rvvm_irq_t      irq)
{
    rtc_goldfish_dev_t* rtc = safe_new_obj(rtc_goldfish_dev_t);

    rvvm_reg_desc_t desc = {
        .addr = addr,
        .size = 0x1000,
        .data = rtc,
        .type = &rtc_goldfish_type,
    };

    if (!irq_dev) {
        irq_dev = rvvm_get_intc(machine);
    }

    rtc->irq_dev = irq_dev;
    rtc->irq     = rvvm_irq_alloc(irq_dev, irq);

    rvvm_reg_dev_t*  dev = rvvm_region_init_auto(machine, &desc);
    rvvm_fdt_node_t* soc = rvvm_get_fdt_soc(machine);

    if (dev && soc) {
        rvvm_fdt_node_t* fdt = rvvm_fdt_init_reg("rtc", desc.addr);
        rvvm_fdt_prop_set_reg(fdt, "reg", desc.addr, desc.size);
        rvvm_fdt_prop_set_str(fdt, "compatible", "google,goldfish-rtc");
        rvvm_irq_fdt_describe(fdt, rtc->irq_dev, rtc->irq);
        rvvm_fdt_attach(soc, fdt);
    }

    return dev;
}

POP_OPTIMIZATION_SIZE
