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
#include "riscv32_mmu.h"
#include "rvtimer.h"
#include "mem_ops.h"
#include "bit_ops.h"

bool clint_mmio_handler(rvvm_hart_t* vm, riscv32_mmio_device_t* device, uint32_t offset, void* data, uint32_t size, uint8_t access)
{
    UNUSED(device);
    uint8_t tmp[8];
    // MSIP register, bit 0 drives MSIP interrupt bit of the hart
    if (offset == 0) {
        if (access == MMU_WRITE) {
            uint8_t msip = ((*(uint8_t*)data) & 1);
            vm->csr.ip = bit_replace(vm->csr.ip, 3, 1, msip);
        } else {
            memset(data, 0, size);
            *(uint8_t*)data = bit_cut(vm->csr.ip, 3, 1);
        }
        return true;
    }
    rvtimer_update(&vm->timer);
    // MTIMECMP register, 64-bit compare register for timer interrupts
    if (offset >= 0x4000 && (offset + size) <= 0x4008) {
        write_uint64_le(tmp, vm->timer.timecmp);
        offset -= 0x4000;
        if (access == MMU_WRITE) {
            memcpy(tmp + offset, data, size);
            vm->timer.timecmp = read_uint64_le(tmp);
        } else {
            memcpy(data, tmp + offset, size);
        }
        return true;
    }
    // MTIME register, 64-bit timer value
    if (offset >= 0xBFF8 && (offset + size) <= 0xC000) {
        write_uint64_le(tmp, vm->timer.time);
        offset -= 0xBFF8;
        if (access == MMU_WRITE) {
            memcpy(tmp + offset, data, size);
            vm->timer.time = read_uint64_le(tmp);
            rvtimer_rebase(&vm->timer);
        } else {
            memcpy(data, tmp + offset, size);
        }
        return true;
    }
    return false;
}
