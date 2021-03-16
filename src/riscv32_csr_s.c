/*
riscv32_csr_s.c - RISC-V Supervisor Level Control and Status Registers
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

#include "bit_ops.h"
#include "riscv32.h"
#include "riscv32_csr.h"
#include "riscv32_mmu.h"

#define CSR_SEIP_MASK    0x222

// no N extension, U_x bits are hardwired to 0
static inline reg_t csr_sstatus_mask(const riscv32_vm_state_t *vm)
{
	return (1 << CSR_STATUS_SIE)
	     | (1 << CSR_STATUS_SPIE)
	     | (1 << CSR_STATUS_SPP)
	     | (gen_mask(CSR_STATUS_FS_SIZE) << CSR_STATUS_FS_START)
	     | (gen_mask(CSR_STATUS_XS_SIZE) << CSR_STATUS_XS_START)
	     | (1 << CSR_STATUS_SUM)
	     | (1 << CSR_STATUS_MXR)
	     | (gen_mask(CSR_STATUS_UXL_SIZE) << CSR_STATUS_UXL_START)
	     | (1 << CSR_STATUS_SD(vm));
}

static bool riscv32_csr_sstatus(riscv32_vm_state_t *vm, uint32_t csr_id, reg_t* dest, uint8_t op)
{
    UNUSED(csr_id);
    csr_helper_masked(&vm->csr.status, dest, op, csr_sstatus_mask(vm));
    riscv32_csr_isa_change(vm, PRIVILEGE_USER, cut_bits(vm->csr.status, CSR_STATUS_UXL_START, CSR_STATUS_UXL_SIZE));
    return true;
}

static bool riscv32_csr_sie(riscv32_vm_state_t *vm, uint32_t csr_id, reg_t* dest, uint8_t op)
{
    UNUSED(csr_id);
    csr_helper_masked(&vm->csr.ie, dest, op, CSR_SEIP_MASK);
    return true;
}

static bool riscv32_csr_stvec(riscv32_vm_state_t *vm, uint32_t csr_id, reg_t* dest, uint8_t op)
{
    UNUSED(csr_id);
    csr_helper(&vm->csr.tvec[PRIVILEGE_SUPERVISOR], dest, op);
    return true;
}

static bool riscv32_csr_sscratch(riscv32_vm_state_t *vm, uint32_t csr_id, reg_t* dest, uint8_t op)
{
    UNUSED(csr_id);
    csr_helper(&vm->csr.scratch[PRIVILEGE_SUPERVISOR], dest, op);
    return true;
}

static bool riscv32_csr_sepc(riscv32_vm_state_t *vm, uint32_t csr_id, reg_t* dest, uint8_t op)
{
    UNUSED(csr_id);
    csr_helper(&vm->csr.epc[PRIVILEGE_SUPERVISOR], dest, op);
    return true;
}

static bool riscv32_csr_scause(riscv32_vm_state_t *vm, uint32_t csr_id, reg_t* dest, uint8_t op)
{
    UNUSED(csr_id);
    csr_helper(&vm->csr.cause[PRIVILEGE_SUPERVISOR], dest, op);
    return true;
}

static bool riscv32_csr_stval(riscv32_vm_state_t *vm, uint32_t csr_id, reg_t* dest, uint8_t op)
{
    UNUSED(csr_id);
    csr_helper(&vm->csr.tval[PRIVILEGE_SUPERVISOR], dest, op);
    return true;
}

static bool riscv32_csr_sip(riscv32_vm_state_t *vm, uint32_t csr_id, reg_t* dest, uint8_t op)
{
    UNUSED(csr_id);
    csr_helper_masked(&vm->csr.ip, dest, op, CSR_SEIP_MASK);
    return true;
}

static bool riscv32_csr_satp(riscv32_vm_state_t *vm, uint32_t csr_id, reg_t* dest, uint8_t op)
{
    UNUSED(csr_id);
    reg_t satp;
    bool is32bit = vm->isa[PRIVILEGE_SUPERVISOR] == ISA_RV32;
    if (is32bit)
    {
	    satp = (!!vm->mmu_virtual << 31) | (vm->root_page_table >> 12);
    }
    else
    {
	    satp = ((reg_t)vm->mmu_virtual << 60) | ((vm->root_page_table >> 12) & gen_mask(44));
    }
    csr_helper(&satp, dest, op);
    uint8_t mmu_enable = is32bit ? satp >> 31 : satp >> 60;
    /*
    * We currently cache physical addresses in TLB as well, so switching
    * between bare/virtual modes will pollute the address space with illegal entries
    * Hence, a TLB flush is required on switch
    */
    if (vm->mmu_virtual != mmu_enable) riscv32_tlb_flush(vm);
    vm->mmu_virtual = mmu_enable;
    vm->root_page_table = (satp << 12) & gen_mask(XLEN(vm));
    if (!is32bit)
    {
	    vm->root_page_table = sign_extend(vm->root_page_table & gen_mask(56), 57);
    }
    return true;
}

void riscv32_csr_s_init()
{
    // Supervisor Trap Setup
    riscv32_csr_init(0x100, "sstatus", riscv32_csr_sstatus);
    riscv32_csr_init(0x102, "sedeleg", riscv32_csr_unimp);
    riscv32_csr_init(0x103, "sideleg", riscv32_csr_unimp);
    riscv32_csr_init(0x104, "sie", riscv32_csr_sie);
    riscv32_csr_init(0x105, "stvec", riscv32_csr_stvec);
    riscv32_csr_init(0x106, "scounteren", riscv32_csr_unimp);

    // Supervisor Trap Handling
    riscv32_csr_init(0x140, "sscratch", riscv32_csr_sscratch);
    riscv32_csr_init(0x141, "sepc", riscv32_csr_sepc);
    riscv32_csr_init(0x142, "scause", riscv32_csr_scause);
    riscv32_csr_init(0x143, "stval", riscv32_csr_stval);
    riscv32_csr_init(0x144, "sip", riscv32_csr_sip);

    // Supervisor Protection and Translation
    riscv32_csr_init(0x180, "satp", riscv32_csr_satp);
}
