/*
ns16550a.h - NS16550A UART
Copyright (C) 2021  LekKit <github.com/LekKit>
                    Mr0maks <mr.maks0443@gmail.com>

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

#ifndef RVVM_NS16550A_H
#define RVVM_NS16550A_H

#include "rvvmlib.h"
#include "plic.h"
#include "chardev.h"

#define NS16550A_DEFAULT_MMIO 0x10000000

PUBLIC rvvm_mmio_handle_t ns16550a_init(rvvm_machine_t* machine, chardev_t* chardev,
                                        rvvm_addr_t base_addr, plic_ctx_t* plic, uint32_t irq);
PUBLIC rvvm_mmio_handle_t ns16550a_init_auto(rvvm_machine_t* machine, chardev_t* chardev);
PUBLIC rvvm_mmio_handle_t ns16550a_init_term_auto(rvvm_machine_t* machine);

#endif // NS16550A_H
