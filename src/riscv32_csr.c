/*
riscv32_csr.c - RISC-V Control and Status Register
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
#include "riscv32i.h"
#include "riscv32_csr.h"
#include "bit_ops.h"

riscv32_csr_t riscv32_csr_list[4096];

// Perform CSR width modulation as described in 2.4 of priv. spec.
void riscv32_csr_isa_change(riscv32_vm_state_t *vm, uint8_t priv, uint8_t target_isa)
{
	uint8_t source_isa = vm->isa[priv];
	if (source_isa == target_isa
		// if target_isa is 0, that means that we're coming from 32-bit SXL/UXL
		// write, where SXL/UXL fields are not available.
		|| target_isa == 0)
	{
		// nothing to do
		return;
	}

	size_t source_sd_pos = CSR_STATUS_SD(vm);
	vm->isa[priv] = target_isa;

	size_t source_isa_xlen = 1 << (source_isa + 4);
	size_t target_isa_xlen = 1 << (target_isa + 4);
	reg_t mask = gen_mask(target_isa_xlen);

	bool sd = is_bit_set(vm->csr.status, source_sd_pos);
	vm->csr.status = replace_bits(vm->csr.status, source_sd_pos, 1, 0);
	vm->csr.status &= mask;
	vm->csr.status = replace_bits(vm->csr.status, CSR_STATUS_SD(vm), 1, sd);

	if (priv == PRIVILEGE_MACHINE
			&& source_isa_xlen == 32 && target_isa_xlen > source_isa_xlen)
	{
		vm->csr.status = replace_bits(vm->csr.status, CSR_STATUS_SXL_START, CSR_STATUS_SXL_SIZE, ISA_MAX);
		vm->csr.status = replace_bits(vm->csr.status, CSR_STATUS_UXL_START, CSR_STATUS_UXL_SIZE, ISA_MAX);
		// We don't need to run csr_isa_change for other privilege levels,
		// since ISA_MAX is set and no mask should be applied. However,
		// we still need to change ISA in our VM state:
		vm->isa[PRIVILEGE_USER] = ISA_MAX;
		vm->isa[PRIVILEGE_SUPERVISOR] = ISA_MAX;
	}

	vm->csr.edeleg[priv] &= mask;
	vm->csr.ideleg[priv] &= mask;
	vm->csr.ie &= mask;
	vm->csr.tvec[priv] &= mask;
	vm->csr.counteren[priv] &= mask;
	vm->csr.scratch[priv] &= mask;
	vm->csr.epc[priv] &= mask;
	vm->csr.cause[priv] &= mask;
	vm->csr.tval[priv] &= mask;
	vm->csr.ip &= mask;
}

void riscv32_csr_init(uint32_t csr_id, const char *name, riscv32_csr_handler_t handler)
{
    riscv32_csr_list[csr_id].name = name;
    riscv32_csr_list[csr_id].handler = handler;
}

bool riscv32_csr_unimp(riscv32_vm_state_t *vm, uint32_t csr_id, reg_t* dest, uint8_t op)
{
    UNUSED(vm);
    UNUSED(dest);
    UNUSED(op);
    UNUSED(csr_id);
    riscv32_debug_always(vm, "unimplemented csr %c!!!", csr_id);
    return false;
}

bool riscv32_csr_illegal(riscv32_vm_state_t *vm, uint32_t csr_id, reg_t* dest, uint8_t op)
{
    UNUSED(vm);
    UNUSED(csr_id);
    UNUSED(dest);
    UNUSED(op);
    return false;
}
