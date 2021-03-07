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

#include "riscv32.h"
#include "riscv32_csr.h"
#include "riscv32_mmu.h"

#define CSR_SSTATUS_MASK 0x800DE133

static bool riscv32_csr_sstatus(riscv32_vm_state_t *vm, uint32_t csr_id, uint32_t* dest, uint8_t op)
{
    UNUSED(csr_id);
    csr_helper_masked(&vm->csr.status, dest, op, CSR_SSTATUS_MASK);
    return true;
}

static bool riscv32_csr_sie(riscv32_vm_state_t *vm, uint32_t csr_id, uint32_t* dest, uint8_t op)
{
    UNUSED(csr_id);
    csr_helper(&vm->csr.ie[PRIVILEGE_SUPERVISOR], dest, op);
    return true;
}

static bool riscv32_csr_stvec(riscv32_vm_state_t *vm, uint32_t csr_id, uint32_t* dest, uint8_t op)
{
    UNUSED(csr_id);
    csr_helper(&vm->csr.tvec[PRIVILEGE_SUPERVISOR], dest, op);
    return true;
}

static bool riscv32_csr_sscratch(riscv32_vm_state_t *vm, uint32_t csr_id, uint32_t* dest, uint8_t op)
{
    UNUSED(csr_id);
    csr_helper(&vm->csr.scratch[PRIVILEGE_SUPERVISOR], dest, op);
    return true;
}

static bool riscv32_csr_sepc(riscv32_vm_state_t *vm, uint32_t csr_id, uint32_t* dest, uint8_t op)
{
    UNUSED(csr_id);
    csr_helper(&vm->csr.epc[PRIVILEGE_SUPERVISOR], dest, op);
    return true;
}

static bool riscv32_csr_scause(riscv32_vm_state_t *vm, uint32_t csr_id, uint32_t* dest, uint8_t op)
{
    UNUSED(csr_id);
    csr_helper(&vm->csr.cause[PRIVILEGE_SUPERVISOR], dest, op);
    return true;
}

static bool riscv32_csr_stval(riscv32_vm_state_t *vm, uint32_t csr_id, uint32_t* dest, uint8_t op)
{
    UNUSED(csr_id);
    csr_helper(&vm->csr.tval[PRIVILEGE_SUPERVISOR], dest, op);
    return true;
}

static bool riscv32_csr_sip(riscv32_vm_state_t *vm, uint32_t csr_id, uint32_t* dest, uint8_t op)
{
    UNUSED(csr_id);
    csr_helper(&vm->csr.ip[PRIVILEGE_SUPERVISOR], dest, op);
    return true;
}
void riscv32_print_sv32_pagetable(riscv32_vm_state_t* vm, uint32_t pagetable);
static bool riscv32_csr_satp(riscv32_vm_state_t *vm, uint32_t csr_id, uint32_t* dest, uint8_t op)
{
    UNUSED(csr_id);
    uint32_t satp = (vm->mmu_virtual ? 0x80000000 : 0) | (vm->root_page_table >> 12);
    csr_helper(&satp, dest, op);
    bool mmu_enable = satp & 0x80000000;
    /*
    * We currently cache physical addresses in TLB as well, so switching
    * between bare/virtual modes will pollute the address space with illegal entries
    * Hence, a TLB flush is required on switch
    */
    if (vm->mmu_virtual != mmu_enable) riscv32_tlb_flush(vm);
    vm->mmu_virtual = mmu_enable;
    vm->root_page_table = satp << 12;
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
