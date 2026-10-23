/*
rtc-ds7142.c - Dallas DS1742 Real-time Clock
Copyright (C) 2023  LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <rvvm/rvvm_board.h>
#include <rvvm/rvvm_fdt.h>
#include <rvvm/rvvm_region.h>
#include <rvvm/rvvm_snapshot.h>

#include <util/mem_ops.h>
#include <util/rvtimer.h>
#include <util/utils.h>

PUSH_OPTIMIZATION_SIZE

#define DS1742_REGS_SIZE     0x08 // Registers size

#define DS1742_REG_CENTURY   0x00 // Control, Century
#define DS1742_REG_SECONDS   0x01 // Seconds [0, 59]
#define DS1742_REG_MINUTES   0x02 // Minutes [0, 59]
#define DS1742_REG_HOURS     0x03 // Hours [0, 23]
#define DS1742_REG_WDAY      0x04 // Day of week [1, 7]
#define DS1742_REG_MDAY      0x05 // Day of month [1, 31]
#define DS1742_REG_MONTH     0x06 // Month [1, 12]
#define DS1742_REG_YEAR      0x07 // Year [0, 99]

#define DS1742_CENTURY_READ  0x40 // Lock registers for read
#define DS1742_CENTURY_WRITE 0x80 // Lock registers for write, no-op
#define DS1742_CENTURY_MASK  0xC0 // Mask of control registers

#define DS1742_WDAY_BATT_OK  0x80 // Battery OK

typedef struct {
    uint32_t ctl;
    uint32_t regs[DS1742_REGS_SIZE];
} ds1742_dev_t;

static inline uint8_t bcd_conv_u8(uint8_t val)
{
    return (val % 10) | ((val / 10) << 4);
}

static inline uint64_t div_time_units(uint64_t* units, uint32_t div)
{
    uint64_t rem = *units % div;
    *units       = *units / div;
    return rem;
}

static inline void rtc_ds1742_wr(ds1742_dev_t* rtc, size_t reg, uint8_t val)
{
    atomic_store_uint32_relax(&rtc->regs[reg], bcd_conv_u8(val));
}

void rtc_ds1742_update_regs(ds1742_dev_t* rtc)
{
    uint64_t tmp  = rvtimer_unixtime();
    uint32_t sec  = div_time_units(&tmp, 60);          // Seconds [0, 59]
    uint32_t min  = div_time_units(&tmp, 60);          // Minutes [0, 59]
    uint32_t hour = div_time_units(&tmp, 24);          // Hours [0, 23]
    uint64_t days = tmp + 719468;                      // Days since 01.03.0000
    uint32_t wday = ((days + 2) % 7) + 1;              // Day of week [1, 7] (Sunday = 1)
    uint32_t era  = days / 146097;                     // Era since 01.03.0000 (400 year unit)
    uint32_t doe  = days - (era * 146097);             // Day of era [0, 146096]
    uint32_t coe  = doe / 36524;                       // Century of era [0, 3]
    uint32_t lde  = (doe / 1460) - coe;                // Leap days of era
    uint32_t doel = doe - lde;                         // Day of era (Leap-compensated)
    uint32_t year = ((doel + 59) / 365) + (era * 400); // Year
    uint32_t doy  = doel % 365;                        // Day of year [0, 365] since 01.03.XXXX
    uint32_t mmf  = ((5 * doy) + 2) / 153;             // Month [0, 11] [Mar, Feb]
    uint32_t mday = doy - (((153 * mmf) + 2) / 5) + 1; // Day of month [1, 31]
    uint32_t mon  = mmf + (mmf < 10 ? 3 : -9);         // Month [1, 12] [Jan, Dec]

    rtc_ds1742_wr(rtc, DS1742_REG_CENTURY, year / 100);
    rtc_ds1742_wr(rtc, DS1742_REG_SECONDS, sec);
    rtc_ds1742_wr(rtc, DS1742_REG_MINUTES, min);
    rtc_ds1742_wr(rtc, DS1742_REG_HOURS, hour);
    rtc_ds1742_wr(rtc, DS1742_REG_MDAY, mday);
    rtc_ds1742_wr(rtc, DS1742_REG_WDAY, wday);
    rtc_ds1742_wr(rtc, DS1742_REG_MONTH, mon);
    rtc_ds1742_wr(rtc, DS1742_REG_YEAR, year % 100);
}

static void rtc_ds1742_mmio_read(rvvm_reg_dev_t* dev, void* data, size_t size, size_t off)
{
    rvvm_reg_desc_t desc = {0};
    ds1742_dev_t*   rtc  = rvvm_region_data(dev);
    rvvm_region_get_desc(dev, &desc);
    UNUSED(size);

    off = off + DS1742_REGS_SIZE - desc.size;

    if (off < DS1742_REGS_SIZE) {
        uint8_t val = atomic_load_uint32_relax(&rtc->regs[off]);
        if (off == DS1742_REG_CENTURY) {
            val |= atomic_load_uint32_relax(&rtc->ctl);
        } else if (off == DS1742_REG_WDAY) {
            val |= DS1742_WDAY_BATT_OK;
        }
        write_uint8(data, val);
    }
}

static void rtc_ds1742_mmio_write(rvvm_reg_dev_t* dev, const void* data, size_t size, size_t off)
{
    rvvm_reg_desc_t desc = {0};
    ds1742_dev_t*   rtc  = rvvm_region_data(dev);
    rvvm_region_get_desc(dev, &desc);
    UNUSED(size);

    off = off + DS1742_REGS_SIZE - desc.size;

    if (off == DS1742_REG_CENTURY) {
        uint8_t val = read_uint8(data);
        uint8_t ctl = atomic_swap_uint32(&rtc->ctl, val & DS1742_CENTURY_MASK);
        if (!(ctl & DS1742_CENTURY_READ) && (val & DS1742_CENTURY_READ)) {
            rtc_ds1742_update_regs(rtc);
        }
    }
}

static void rtc_ds1742_suspend(rvvm_reg_dev_t* dev, rvvm_snapshot_t* snap, bool resume)
{
    if (snap) {
        ds1742_dev_t* rtc = rvvm_region_data(dev);
        rvvm_snapshot_section(snap, "rtc-ds1742");
        rvvm_snapshot_field(snap, rtc->ctl);
        for (size_t i = 0; i < DS1742_REGS_SIZE; ++i) {
            rvvm_snapshot_field(snap, rtc->regs[i]);
        }
    }
    UNUSED(resume);
}

static void rtc_ds1742_cleanup(rvvm_reg_dev_t* dev)
{
    ds1742_dev_t* rtc = rvvm_region_data(dev);
    free(rtc);
}

static const rvvm_reg_type_t rtc_ds1742_type = {
    .name     = "rtc-ds1742",
    .read     = rtc_ds1742_mmio_read,
    .write    = rtc_ds1742_mmio_write,
    .suspend  = rtc_ds1742_suspend,
    .cleanup  = rtc_ds1742_cleanup,
    .min_size = 1,
    .max_size = 1,
};

RVVM_PUBLIC rvvm_reg_dev_t* rvvm_rtc_ds1742_init(rvvm_machine_t* machine, rvvm_addr_t addr)
{
    ds1742_dev_t* rtc = safe_new_obj(ds1742_dev_t);

    rvvm_reg_desc_t desc = {
        .addr = addr,
        .size = 0x800,
        .data = rtc,
        .type = &rtc_ds1742_type,
    };

    rtc_ds1742_update_regs(rtc);

    rvvm_reg_dev_t*  dev = rvvm_region_init_auto(machine, &desc);
    rvvm_fdt_node_t* soc = rvvm_get_fdt_soc(machine);

    if (dev && soc) {
        rvvm_fdt_node_t* fdt = rvvm_fdt_init_reg("rtc", desc.addr);
        rvvm_fdt_prop_set_reg(fdt, "reg", desc.addr, desc.size);
        rvvm_fdt_prop_set_str(fdt, "compatible", "maxim,ds1742");
        rvvm_fdt_attach(soc, fdt);
    }
    return dev;
}

POP_OPTIMIZATION_SIZE
