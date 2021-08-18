/*
clint.h - Core-local Interrupt
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

#include "clint.h"
#include "compiler.h"
#include "rvtimer.h"
#include "mem_ops.h"
#include "bit_ops.h"
#include "riscv_hart.h"

#define CLINT_MEM_SIZE 0x10000

static bool clint_mmio_read_handler(rvvm_mmio_dev_t* device, void* data, paddr_t offset, uint8_t size)
{
    rvvm_hart_t *vm = (rvvm_hart_t*) device->data;
    uint8_t tmp[8];

    // MSIP register, bit 0 drives MSIP interrupt bit of the hart
    if (offset == 0) {
        memset(data, 0, size);
        *(uint8_t*)data = bit_cut(vm->csr.ip, 3, 1);
        return true;
    }

    rvtimer_update(&vm->timer);

    // MTIMECMP register, 64-bit compare register for timer interrupts
    if (offset >= 0x4000 && (offset + size) <= 0x4008) {
        write_uint64_le(tmp, vm->timer.timecmp);
        offset -= 0x4000;
        memcpy(data, tmp + offset, size);
        return true;
    }

    // MTIME register, 64-bit timer value
    if (offset >= 0xBFF8 && (offset + size) <= 0xC000) {
        write_uint64_le(tmp, vm->timer.time);
        offset -= 0xBFF8;
        memcpy(data, tmp + offset, size);
        return true;
    }
    return false;
}

static bool clint_mmio_write_handler(rvvm_mmio_dev_t* device, void* data, paddr_t offset, uint8_t size)
{
    rvvm_hart_t *vm = (rvvm_hart_t*) device->data;
    uint8_t tmp[8];

    // MSIP register, bit 0 drives MSIP interrupt bit of the hart
    if (offset == 0) {
        uint8_t msip = ((*(uint8_t*)data) & 1);
        if (msip) {
            riscv_interrupt(vm, INTERRUPT_MSOFTWARE);
        } else {
            riscv_interrupt_clear(vm, INTERRUPT_MSOFTWARE);
        }
        return true;
    }

    rvtimer_update(&vm->timer);

    // MTIMECMP register, 64-bit compare register for timer interrupts
    if (offset >= 0x4000 && (offset + size) <= 0x4008) {
        write_uint64_le(tmp, vm->timer.timecmp);
        offset -= 0x4000;
        memcpy(tmp + offset, data, size);
        vm->timer.timecmp = read_uint64_le(tmp);
        return true;
    }

    // MTIME register, 64-bit timer value
    if (offset >= 0xBFF8 && (offset + size) <= 0xC000) {
        write_uint64_le(tmp, vm->timer.time);
        offset -= 0xBFF8;
        memcpy(tmp + offset, data, size);
        vm->timer.time = read_uint64_le(tmp);
        rvtimer_rebase(&vm->timer);
        return true;
    }

    return false;
}

static rvvm_mmio_type_t clint_dev_type = {
    .name = "clint",
};

void clint_init(rvvm_machine_t* machine, paddr_t addr)
{
    rvvm_mmio_dev_t clint;
    clint.min_op_size = 1;
    clint.max_op_size = 8;
    clint.read = clint_mmio_read_handler;
    clint.write = clint_mmio_write_handler;
    clint.type = &clint_dev_type;

    vector_foreach(machine->harts, i) {
        clint.begin = addr;
        clint.end = addr + CLINT_MEM_SIZE;
        clint.data = &vector_at(machine->harts, i);
        rvvm_attach_mmio(machine, &clint);

        addr += CLINT_MEM_SIZE;
    }
}

