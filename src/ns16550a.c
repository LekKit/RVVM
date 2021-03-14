/*
ns16550a.c - NS16550A UART emulator code
Copyright (C) 2021  Mr0maks <mr.maks0443@gmail.com>
                    LekKit <github.com/LekKit>

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

#include "riscv32.h"
#include "riscv32_mmu.h"

#define NS16550A_REG_SIZE 0x100

struct ns16550a_data {
    uint8_t regs[8];
    uint8_t regs_dlab[2];
};

// Read DLAB = 0
#define NS16550A_REG_RBR 0
// Write DLAB = 0
#define NS16550A_REG_THR 0
// RW DLAB = 0
#define NS16550A_REG_IER 1
// RW DLAB = 1
#define NS16550A_REG_DLL 0
#define NS16550A_REG_DLM 1
// Read DLAB = 0/1
#define NS16550A_REG_IIR 2
// Write DLAB = 0/1
#define NS16550A_REG_FCR 2
// RW DLAB = 0/1
#define NS16550A_REG_LCR 3
#define NS16550A_REG_MCR 4
#define NS16550A_REG_LSR 5
#define NS16550A_REG_MSR 6
#define NS16550A_REG_SCR 7

static bool ns16550a_mmio_read(riscv32_vm_state_t* vm, riscv32_mmio_device_t* device, physaddr_t offset, uint8_t *value)
{
    UNUSED(vm);
    struct ns16550a_data *regs = (struct ns16550a_data *)device->data;
    if (regs->regs[NS16550A_REG_LCR] & 0x80) {
        riscv32_debug(vm, "NS16550A: DLAB = 1\n");

        switch (offset) {
            case NS16550A_REG_DLL:
            case NS16550A_REG_DLM: {
                *value = regs->regs_dlab[offset];
                break;
            }
            case NS16550A_REG_IIR:
            case NS16550A_REG_LCR:
            case NS16550A_REG_MCR:
            case NS16550A_REG_LSR:
            case NS16550A_REG_MSR:
            case NS16550A_REG_SCR: {
                *value = regs->regs[offset];
                break;
            }
            default: {
                riscv32_debug_always(vm, "NS16550A: Unimplemented offset %h\n", offset);
                break;
            }
        }
    }
    else {
        riscv32_debug(vm, "NS16550A: DLAB = 0\n");

        switch (offset) {
            case NS16550A_REG_RBR: {
                *value = 0;
                break;
            }
            case NS16550A_REG_IER:
            case NS16550A_REG_IIR:
            case NS16550A_REG_LCR:
            case NS16550A_REG_MCR:
            case NS16550A_REG_LSR:
            case NS16550A_REG_MSR:
            case NS16550A_REG_SCR: {
                *value = regs->regs[offset];
                break;
            }
            default: {
                riscv32_debug_always(vm, "NS16550A: Unimplemented offset %h\n", offset);
                break;
            }
        }
    }

    return true;
}

static bool ns16550a_mmio_write(riscv32_vm_state_t* vm, riscv32_mmio_device_t* device, physaddr_t offset, uint8_t value)
{
    UNUSED(vm);
    struct ns16550a_data *regs = (struct ns16550a_data *)device->data;

    if (regs->regs[NS16550A_REG_LCR] & 0x80) {
        switch (offset) {
            case NS16550A_REG_DLL:
            case NS16550A_REG_DLM: {
                regs->regs_dlab[offset] = value;
                break;
            }
            case NS16550A_REG_FCR:
            case NS16550A_REG_LCR:
            case NS16550A_REG_MCR:
            case NS16550A_REG_SCR: {
                regs->regs[offset] = value;
                break;
            }
            case NS16550A_REG_LSR:
            case NS16550A_REG_MSR: {
                break;
            }
            default: {
                riscv32_debug_always(vm, "NS16550A: Unimplemented offset %h\n", offset);
                break;
            }
        }

    } else {
        switch (offset) {
            case NS16550A_REG_THR: {
                printf("%c", value);
                fflush(stdout);
                break;
            }
            case NS16550A_REG_IER:
            case NS16550A_REG_FCR:
            case NS16550A_REG_LCR:
            case NS16550A_REG_MCR:
            case NS16550A_REG_SCR: {
                regs->regs[offset] = value;
                break;
            }
            case NS16550A_REG_LSR:
            case NS16550A_REG_MSR: {
                break;
            }
            default: {
                riscv32_debug_always(vm, "NS16550A: Unimplemented offset %h\n", offset);
                break;
            }
        }
    }
    return true;
}

static bool ns16550a_mmio_handler(riscv32_vm_state_t* vm, riscv32_mmio_device_t* device, physaddr_t offset, void* memory_data, uint32_t size, uint8_t access)
{
    UNUSED(size);
    if (access == MMU_READ) {
        return ns16550a_mmio_read(vm, device, offset, memory_data);
    } else if (access == MMU_WRITE) {
        return ns16550a_mmio_write(vm, device, offset, *(uint8_t*)memory_data);
    }
    return false;
}

void ns16550a_init(riscv32_vm_state_t *vm, physaddr_t base_addr)
{
    struct ns16550a_data *ptr = calloc(1, sizeof (struct ns16550a_data));

    ptr->regs[NS16550A_REG_LSR] = 0x60;

    riscv32_debug_always(vm, "NS16550A UART ON %h", base_addr);
    riscv32_mmio_add_device(vm, base_addr, base_addr + NS16550A_REG_SIZE - 1, ns16550a_mmio_handler, ptr);
}
