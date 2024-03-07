/*
rtc-ds7142.c - Dallas DS1742 Real-time Clock
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

#include "rtc-ds1742.h"
#include "mem_ops.h"
#include "utils.h"
#include "fdtlib.h"
#include <time.h>

#define DS1742_REG_CTL_CENT 0x0 // Control, Century
#define DS1742_REG_SECONDS  0x1 // Seconds [0, 59]
#define DS1742_REG_MINUTES  0x2 // Minutes [0, 59]
#define DS1742_REG_HOURS    0x3 // Hours [0, 23]
#define DS1742_REG_DAY      0x4 // Day of week [1, 7]
#define DS1742_REG_DATE     0x5 // Day of month [1, 31]
#define DS1742_REG_MONTH    0x6 // Month [1, 12]
#define DS1742_REG_YEAR     0x7 // Year [0, 99]

#define DS1742_MMIO_SIZE 0x8

#define DS1742_DAY_BATT 0x80 // Battery OK
#define DS1742_CTL_READ 0x40 // Lock registers for read
#define DS1742_CTL_MASK 0xC0 // Mask of control registers

typedef struct {
    uint8_t ctl;
    uint8_t regs[DS1742_MMIO_SIZE];
} ds1742_dev_t;

static inline uint8_t bcd_conv_u8(uint8_t val)
{
    return (val % 10) | ((val / 10) << 4);
}

void rtc_ds1742_update_regs(ds1742_dev_t* rtc)
{
    time_t unix_time = time(NULL);
    struct tm* calendar = gmtime(&unix_time);
    rtc->regs[DS1742_REG_CTL_CENT] = bcd_conv_u8(calendar->tm_year / 100 + 19);
    rtc->regs[DS1742_REG_SECONDS] = bcd_conv_u8(EVAL_MIN(calendar->tm_sec, 59));
    rtc->regs[DS1742_REG_MINUTES] = bcd_conv_u8(calendar->tm_min);
    rtc->regs[DS1742_REG_HOURS] = bcd_conv_u8(calendar->tm_hour);
    rtc->regs[DS1742_REG_DATE] = bcd_conv_u8(calendar->tm_mday);
    rtc->regs[DS1742_REG_DAY] = bcd_conv_u8(calendar->tm_wday + 1);
    rtc->regs[DS1742_REG_MONTH] = bcd_conv_u8(calendar->tm_mon + 1);
    rtc->regs[DS1742_REG_YEAR] = bcd_conv_u8(calendar->tm_year % 100);
}

static bool rtc_ds1742_mmio_read(rvvm_mmio_dev_t* dev, void* data, size_t offset, uint8_t size)
{
    ds1742_dev_t* rtc = dev->data;
    uint8_t reg = rtc->regs[offset];
    UNUSED(size);
    if (offset == DS1742_REG_CTL_CENT) reg |= rtc->ctl;
    if (offset == DS1742_REG_DAY) reg |= DS1742_DAY_BATT;
    write_uint8(data, reg);
    return true;
}

static bool rtc_ds1742_mmio_write(rvvm_mmio_dev_t* dev, void* data, size_t offset, uint8_t size)
{
    ds1742_dev_t* rtc = dev->data;
    UNUSED(size);
    if (offset == DS1742_REG_CTL_CENT) {
        uint8_t ctl = read_uint8(data) & DS1742_CTL_MASK;
        if (!(rtc->ctl & DS1742_CTL_READ) && (ctl & DS1742_CTL_READ)) rtc_ds1742_update_regs(rtc);
        rtc->ctl = ctl;
    }
    return true;
}

static rvvm_mmio_type_t rtc_ds1742_dev_type = {
    .name = "rtc_ds1742",
};

PUBLIC rvvm_mmio_handle_t rtc_ds1742_init(rvvm_machine_t* machine, rvvm_addr_t base_addr)
{
    ds1742_dev_t* rtc = safe_new_obj(ds1742_dev_t);
    rvvm_mmio_dev_t rtc_ds1742 = {
        .addr = base_addr,
        .size = DS1742_MMIO_SIZE,
        .data = rtc,
        .read = rtc_ds1742_mmio_read,
        .write = rtc_ds1742_mmio_write,
        .type = &rtc_ds1742_dev_type,
        .min_op_size = 1,
        .max_op_size = 1,
    };
    rtc_ds1742_update_regs(rtc);
    rvvm_mmio_handle_t handle = rvvm_attach_mmio(machine, &rtc_ds1742);
    if (handle == RVVM_INVALID_MMIO) return handle;
#ifdef USE_FDT
    struct fdt_node* rtc_fdt = fdt_node_create_reg("rtc", base_addr);
    fdt_node_add_prop_reg(rtc_fdt, "reg", base_addr, DS1742_MMIO_SIZE);
    fdt_node_add_prop_str(rtc_fdt, "compatible", "maxim,ds1742");
    fdt_node_add_child(rvvm_get_fdt_soc(machine), rtc_fdt);
#endif
    return handle;
}

PUBLIC rvvm_mmio_handle_t rtc_ds1742_init_auto(rvvm_machine_t* machine)
{
    rvvm_addr_t addr = rvvm_mmio_zone_auto(machine, RTC_DS1742_DEFAULT_MMIO, DS1742_MMIO_SIZE);
    return rtc_ds1742_init(machine, addr);
}
