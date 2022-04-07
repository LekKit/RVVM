/*
ns16550a.h - NS16550A UART emulator code definitions
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

#ifndef NS16550A_H
#define NS16550A_H

#include "rvvm.h"
#include "plic.h"

#define NS16550A_DEFAULT_MMIO 0x10000000

void ns16550a_init(rvvm_machine_t* machine, paddr_t base_addr, plic_ctx_t plic, uint32_t irq);
void ns16550a_init_auto(rvvm_machine_t* machine, plic_ctx_t plic);

#endif // NS16550A_H
