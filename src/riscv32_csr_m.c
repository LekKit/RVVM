/*
riscv32_csr_m.c - RISC-V Machine Level Control and Status Registers
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

#define CSR_MARCHID 0x5256564D // 'RVVM'

#define CSR_MEIP_MASK    0xAAA

#define CSR_MISA_RV32  0x40000000
#define CSR_MISA_RV64  0x80000000
#define CSR_MISA_RV128 0xC0000000

// no N extension, U_x bits are hardwired to 0
static inline reg_t csr_mstatus_mask(const riscv32_vm_state_t *vm) 
{
	return (1 << CSR_STATUS_SIE)
	     | (1 << CSR_STATUS_MIE)
	     | (1 << CSR_STATUS_SPIE)
	     | (1 << CSR_STATUS_MPIE)
	     | (1 << CSR_STATUS_SPP)
	     | (gen_mask(CSR_STATUS_MPP_SIZE) << CSR_STATUS_MPP_START)
	     | (gen_mask(CSR_STATUS_FS_SIZE) << CSR_STATUS_FS_START)
	     | (gen_mask(CSR_STATUS_XS_SIZE) << CSR_STATUS_XS_START)
	     | (1 << CSR_STATUS_MPRV)
	     | (1 << CSR_STATUS_SUM)
	     | (1 << CSR_STATUS_MXR)
	     | (1 << CSR_STATUS_TVM)
	     | (1 << CSR_STATUS_TW)
	     | (1 << CSR_STATUS_TSR)
	     | (gen_mask(CSR_STATUS_UXL_SIZE) << CSR_STATUS_UXL_START)
	     | (gen_mask(CSR_STATUS_SXL_SIZE) << CSR_STATUS_SXL_START)
	     | (1 << CSR_STATUS_SD(vm));
}

static uint32_t riscv32_mkmisa(const char* str)
{
    uint32_t ret = 0;
    while (*str) {
        ret |= (1 << (*str - 'A'));
        str++;
    }
    return ret;
}

static bool riscv32_csr_mhartid(riscv32_vm_state_t *vm, uint32_t csr_id, reg_t* dest, uint8_t op)
{
    UNUSED(vm);
    UNUSED(csr_id);
    UNUSED(op);
    *dest = 0;
    return true;
}

static bool riscv32_csr_mstatus(riscv32_vm_state_t *vm, uint32_t csr_id, reg_t* dest, uint8_t op)
{
    UNUSED(csr_id);
    csr_helper_masked(&vm->csr.status, dest, op, csr_mstatus_mask(vm));
    riscv32_csr_isa_change(vm, PRIVILEGE_SUPERVISOR, cut_bits(vm->csr.status, CSR_STATUS_SXL_START, CSR_STATUS_SXL_SIZE));
    riscv32_csr_isa_change(vm, PRIVILEGE_USER, cut_bits(vm->csr.status, CSR_STATUS_UXL_START, CSR_STATUS_UXL_SIZE));
    return true;
}

static bool riscv32_csr_misa(riscv32_vm_state_t *vm, uint32_t csr_id, reg_t* dest, uint8_t op)
{
    UNUSED(csr_id);

    uint32_t extensions = riscv32_mkmisa("IMACSU");
    reg_t isa_pos = XLEN(vm) - 2;
    reg_t misa = ((reg_t)vm->isa[PRIVILEGE_MACHINE] << isa_pos) | extensions;

    // Do not permit changing extensions
    csr_helper_masked(&misa, dest, op, gen_mask(2) << isa_pos);

    uint8_t new_isa = cut_bits(misa, isa_pos, 2);
    riscv32_csr_isa_change(vm, PRIVILEGE_MACHINE, new_isa);
    *dest = (new_isa << ((reg_t)XLEN(vm) - 2)) | extensions;
    return true;
}

static bool riscv32_csr_medeleg(riscv32_vm_state_t *vm, uint32_t csr_id, reg_t* dest, uint8_t op)
{
    UNUSED(csr_id);
    csr_helper(&vm->csr.edeleg[PRIVILEGE_MACHINE], dest, op);
    return true;
}

static bool riscv32_csr_mideleg(riscv32_vm_state_t *vm, uint32_t csr_id, reg_t* dest, uint8_t op)
{
    UNUSED(csr_id);
    csr_helper(&vm->csr.ideleg[PRIVILEGE_MACHINE], dest, op);
    return true;
}

static bool riscv32_csr_mie(riscv32_vm_state_t *vm, uint32_t csr_id, reg_t* dest, uint8_t op)
{
    UNUSED(csr_id);
    csr_helper_masked(&vm->csr.ie, dest, op, CSR_MEIP_MASK);
    return true;
}

static bool riscv32_csr_mtvec(riscv32_vm_state_t *vm, uint32_t csr_id, reg_t* dest, uint8_t op)
{
    UNUSED(csr_id);
    csr_helper(&vm->csr.tvec[PRIVILEGE_MACHINE], dest, op);
    return true;
}

static bool riscv32_csr_mscratch(riscv32_vm_state_t *vm, uint32_t csr_id, reg_t* dest, uint8_t op)
{
    UNUSED(csr_id);
    csr_helper(&vm->csr.scratch[PRIVILEGE_MACHINE], dest, op);
    return true;
}

static bool riscv32_csr_mepc(riscv32_vm_state_t *vm, uint32_t csr_id, reg_t* dest, uint8_t op)
{
    UNUSED(csr_id);
    csr_helper(&vm->csr.epc[PRIVILEGE_MACHINE], dest, op);
    return true;
}

static bool riscv32_csr_mcause(riscv32_vm_state_t *vm, uint32_t csr_id, reg_t* dest, uint8_t op)
{
    UNUSED(csr_id);
    csr_helper(&vm->csr.cause[PRIVILEGE_MACHINE], dest, op);
    return true;
}

static bool riscv32_csr_mtval(riscv32_vm_state_t *vm, uint32_t csr_id, reg_t* dest, uint8_t op)
{
    UNUSED(csr_id);
    csr_helper(&vm->csr.tval[PRIVILEGE_MACHINE], dest, op);
    return true;
}

static bool riscv32_csr_mip(riscv32_vm_state_t *vm, uint32_t csr_id, reg_t* dest, uint8_t op)
{
    UNUSED(csr_id);
    csr_helper_masked(&vm->csr.ip, dest, op, CSR_MEIP_MASK);
    return true;
}

void riscv32_csr_m_init()
{
    // Machine Information Registers
    riscv32_csr_init(0xF11, "mvendorid", riscv32_csr_unimp);
    riscv32_csr_init(0xF12, "marchid", riscv32_csr_unimp);
    riscv32_csr_init(0xF13, "mimpid", riscv32_csr_unimp);
    riscv32_csr_init(0xF14, "mhartid", riscv32_csr_mhartid);

    // Machine Trap Setup
    riscv32_csr_init(0x300, "mstatus", riscv32_csr_mstatus);
    riscv32_csr_init(0x301, "misa", riscv32_csr_misa);
    riscv32_csr_init(0x302, "medeleg", riscv32_csr_medeleg);
    riscv32_csr_init(0x303, "mideleg", riscv32_csr_mideleg);
    riscv32_csr_init(0x304, "mie", riscv32_csr_mie);
    riscv32_csr_init(0x305, "mtvec", riscv32_csr_mtvec);
    riscv32_csr_init(0x306, "mcounteren", riscv32_csr_unimp);

    // Machine Trap Handling
    riscv32_csr_init(0x340, "mscratch", riscv32_csr_mscratch);
    riscv32_csr_init(0x341, "mepc", riscv32_csr_mepc);
    riscv32_csr_init(0x342, "mcause", riscv32_csr_mcause);
    riscv32_csr_init(0x343, "mtval", riscv32_csr_mtval);
    riscv32_csr_init(0x344, "mip", riscv32_csr_mip);

    // Machine Memory Protection
    for (uint32_t i=0; i<4; ++i)
        riscv32_csr_init(0x3A0+i, "pmpcfg", riscv32_csr_unimp);
    for (uint32_t i=0; i<16; ++i)
        riscv32_csr_init(0x3B0+i, "pmpaddr", riscv32_csr_unimp);

    // Machine Counter/Timers
    riscv32_csr_init(0xB00, "mcycle", riscv32_csr_unimp);
    riscv32_csr_init(0xB02, "minstret", riscv32_csr_unimp);
    riscv32_csr_init(0xB80, "mcycleh", riscv32_csr_unimp);
    riscv32_csr_init(0xB82, "minstreth", riscv32_csr_unimp);
    for (uint32_t i=3; i<32; ++i) {
        riscv32_csr_init(0xB00+i, "mhpmcounter", riscv32_csr_unimp);
        riscv32_csr_init(0xB80+i, "mhpmcounterh", riscv32_csr_unimp);
    }

    // Machine Counter Setup
    riscv32_csr_init(0x320, "mcountinhibit", riscv32_csr_unimp);
    for (uint32_t i=3; i<32; ++i)
        riscv32_csr_init(0x320+i, "mhpmevent", riscv32_csr_unimp);
}
