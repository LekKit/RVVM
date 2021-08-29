/*
syscon.c - Poweroff/reset syscon device
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

#include "syscon.h"
#include "riscv_hart.h"
#include "atomics.h"
#include "mem_ops.h"

#define SYSCON_POWEROFF 0x5555
#define SYSCON_RESET    0x7777

static bool syscon_mmio_read(rvvm_mmio_dev_t* dev, void* data, paddr_t offset, uint8_t size)
{
    UNUSED(dev);
    UNUSED(offset);
    memset(data, 0, size);
    return true;
}

static bool syscon_mmio_write(rvvm_mmio_dev_t* dev, void* data, paddr_t offset, uint8_t size)
{
    UNUSED(size);
    if (offset == 0) {
        switch(read_uint16_le_m(data)) {
            case SYSCON_POWEROFF:
                rvvm_info("Machine %p shutting down", dev->machine);
                dev->machine->needs_reset = false;
                break;
            case SYSCON_RESET:
                rvvm_info("Machine %p resetting", dev->machine);
                dev->machine->needs_reset = true;
                break;
            default:
                return true;
                break;
        }
        // Handled by eventloop
        atomic_store_uint32(&dev->machine->running, 0);
        // For singlethreaded VMs, returns from riscv_hart_run()
        if (vector_size(dev->machine->harts) == 1) {
            riscv_hart_queue_pause(&vector_at(dev->machine->harts, 0));
        }
    }
    return true;
}

static rvvm_mmio_type_t syscon_dev_type = {
    .name = "syscon",
};

void syscon_init(rvvm_machine_t* machine, paddr_t base_addr)
{
    rvvm_mmio_dev_t syscon = {0};
    syscon.min_op_size = 2;
    syscon.max_op_size = 8;
    syscon.read = syscon_mmio_read;
    syscon.write = syscon_mmio_write;
    syscon.type = &syscon_dev_type;
    syscon.begin = base_addr;
    syscon.end = base_addr + 0x1000;
    rvvm_attach_mmio(machine, &syscon);
}
