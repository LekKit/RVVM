/*
rtc-goldfish.c - Goldfish Real-time Clock
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

#include "rtc-goldfish.h"
#include "mem_ops.h"
#include "utils.h"
#include "fdtlib.h"
#include <time.h>

#define RTC_TIME_LOW     0x0
#define RTC_TIME_HIGH    0x4
#define RTC_ALARM_LOW    0x8
#define RTC_ALARM_HIGH   0xC
#define RTC_IRQ_ENABLED  0x10
#define RTC_ALARM_CLEAR  0x14
#define RTC_ALARM_STATUS 0x18
#define RTC_IRQ_CLEAR    0x1C

#define RTC_REG_SIZE     0x20

struct rtc_goldfish_data {
    plic_ctx_t* plic;
    uint32_t irq;
    uint32_t alarm_low;
    uint32_t alarm_high;
    bool irq_enabled;
    bool alarm_enabled;
};

static bool rtc_goldfish_mmio_read(rvvm_mmio_dev_t* dev, void* data, size_t offset, uint8_t size)
{
    struct rtc_goldfish_data* rtc = dev->data;
    uint64_t timer64 = time(NULL) * 1000000000ULL;
    switch (offset) {
        case RTC_TIME_LOW:
            write_uint32_le(data, timer64);
            break;
        case RTC_TIME_HIGH:
            write_uint32_le(data, timer64 >> 32);
            break;
        case RTC_ALARM_LOW:
            write_uint32_le(data, rtc->alarm_low);
            break;
        case RTC_ALARM_HIGH:
            write_uint32_le(data, rtc->alarm_high);
            break;
        case RTC_IRQ_ENABLED:
            write_uint32_le(data, rtc->irq_enabled);
            break;
        case RTC_ALARM_STATUS:
            write_uint32_le(data, rtc->alarm_enabled);
            break;
        default:
            memset(data, 0, size);
            break;
    }
    return true;
}

static bool rtc_goldfish_mmio_write(rvvm_mmio_dev_t* dev, void* data, size_t offset, uint8_t size)
{
    struct rtc_goldfish_data* rtc = dev->data;
    uint64_t timer64 = time(NULL) * 1000000000ULL;
    UNUSED(size);
    switch (offset) {
        case RTC_ALARM_LOW:
            rtc->alarm_low = read_uint32_le(data);
            break;
        case RTC_ALARM_HIGH:
            rtc->alarm_high = read_uint32_le(data);
            break;
        case RTC_IRQ_ENABLED:
            rtc->irq_enabled = read_uint32_le(data);
            break;
        case RTC_ALARM_CLEAR:
            rtc->alarm_enabled = false;
            break;
        default:
            break;
    }
    uint64_t alarm64 = rtc->alarm_low | (((uint64_t)rtc->alarm_high) << 32);
    if (rtc->alarm_enabled && rtc->irq_enabled && timer64 <= alarm64) {
        if (rtc->plic) plic_send_irq(rtc->plic, rtc->irq);
        rtc->alarm_enabled = false;
    } else {
        rtc->alarm_enabled = true;
    }
    return true;
}

static rvvm_mmio_type_t rtc_goldfish_dev_type = {
    .name = "rtc_goldfish",
};

PUBLIC rvvm_mmio_handle_t rtc_goldfish_init(rvvm_machine_t* machine, rvvm_addr_t base_addr, plic_ctx_t* plic, uint32_t irq)
{
    struct rtc_goldfish_data* ptr = safe_calloc(sizeof(struct rtc_goldfish_data), 1);
    ptr->plic = plic;
    ptr->irq = irq;

    rvvm_mmio_dev_t rtc_goldfish = {
        .data = ptr,
        .addr = base_addr,
        .size = RTC_REG_SIZE,
        .read = rtc_goldfish_mmio_read,
        .write = rtc_goldfish_mmio_write,
        .min_op_size = 4,
        .max_op_size = 4,
        .type = &rtc_goldfish_dev_type,
    };
    rvvm_mmio_handle_t handle = rvvm_attach_mmio(machine, &rtc_goldfish);
    if (handle == RVVM_INVALID_MMIO) return handle;
#ifdef USE_FDT
    struct fdt_node* rtc = fdt_node_create_reg("rtc", base_addr);
    fdt_node_add_prop_reg(rtc, "reg", base_addr, RTC_REG_SIZE);
    fdt_node_add_prop_str(rtc, "compatible", "google,goldfish-rtc");
    fdt_node_add_prop_u32(rtc, "interrupt-parent", plic_get_phandle(plic));
    fdt_node_add_prop_u32(rtc, "interrupts", irq);
    fdt_node_add_child(rvvm_get_fdt_soc(machine), rtc);
#endif
    return handle;
}

PUBLIC rvvm_mmio_handle_t rtc_goldfish_init_auto(rvvm_machine_t* machine)
{
    plic_ctx_t* plic = rvvm_get_plic(machine);
    rvvm_addr_t addr = rvvm_mmio_zone_auto(machine, RTC_GOLDFISH_DEFAULT_MMIO, RTC_REG_SIZE);
    return rtc_goldfish_init(machine, addr, plic, plic_alloc_irq(plic));
}
