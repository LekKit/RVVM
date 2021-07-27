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
#include "rvvm_types.h"

#define CSR_MARCHID 0x5256564D // 'RVVM'

// no N extension, U_x bits are hardwired to 0
#define CSR_MSTATUS_MASK 0x807FF9CC
#define CSR_MEIP_MASK    0xAAA

#define CSR_MISA_RV32  0x40000000
#define CSR_MISA_RV64  0x80000000
#define CSR_MISA_RV128 0xC0000000

static uint32_t riscv32_mkmisa(const char* str)
{
    uint32_t ret = CSR_MISA_RV32;
    while (*str) {
        ret |= (1 << (*str - 'A'));
        str++;
    }
    return ret;
}

static bool riscv32_csr_mhartid(rvvm_hart_t *vm, uint32_t csr_id, uint32_t* dest, uint8_t op)
{
    UNUSED(vm);
    UNUSED(csr_id);
    UNUSED(op);
    *dest = 0;
    return true;
}

static bool riscv32_csr_mstatus(rvvm_hart_t *vm, uint32_t csr_id, uint32_t* dest, uint8_t op)
{
    UNUSED(csr_id);
#ifdef USE_FPU
    bool was_enabled = bit_cut(vm->csr.status, 13, 2) != S_OFF;
#endif
    csr_helper_masked(&vm->csr.status, dest, op, CSR_MSTATUS_MASK);
#ifdef USE_FPU
    uint8_t new_fs = bit_cut(vm->csr.status, 13, 2);
    bool enabled = new_fs != S_OFF;
    if (was_enabled != enabled)
    {
        riscv32f_enable(enabled);
        riscv32d_enable(enabled);
    }
    bool sdbit = new_fs == S_DIRTY
              || bit_cut(vm->csr.status, 15, 2) == S_DIRTY;
    vm->csr.status = bit_replace(vm->csr.status, ((1 << MAX_SHAMT_BITS) - 1), 1, sdbit);
#endif
    return true;
}

static bool riscv32_csr_misa(rvvm_hart_t *vm, uint32_t csr_id, uint32_t* dest, uint8_t op)
{
    UNUSED(vm);
    UNUSED(csr_id);
    UNUSED(op);
#ifdef USE_FPU
    *dest = riscv32_mkmisa("IMAFDCSU");
#else
    *dest = riscv32_mkmisa("IMACSU");
#endif
    return true;
}

static bool riscv32_csr_medeleg(rvvm_hart_t *vm, uint32_t csr_id, uint32_t* dest, uint8_t op)
{
    UNUSED(csr_id);
    csr_helper(&vm->csr.edeleg[PRIVILEGE_MACHINE], dest, op);
    return true;
}

static bool riscv32_csr_mideleg(rvvm_hart_t *vm, uint32_t csr_id, uint32_t* dest, uint8_t op)
{
    UNUSED(csr_id);
    csr_helper(&vm->csr.ideleg[PRIVILEGE_MACHINE], dest, op);
    return true;
}

static bool riscv32_csr_mie(rvvm_hart_t *vm, uint32_t csr_id, uint32_t* dest, uint8_t op)
{
    UNUSED(csr_id);
    csr_helper_masked(&vm->csr.ie, dest, op, CSR_MEIP_MASK);
    vm->ev_int = true;
    vm->wait_event = 0;
    return true;
}

static bool riscv32_csr_mtvec(rvvm_hart_t *vm, uint32_t csr_id, uint32_t* dest, uint8_t op)
{
    UNUSED(csr_id);
    csr_helper(&vm->csr.tvec[PRIVILEGE_MACHINE], dest, op);
    return true;
}

static bool riscv32_csr_mscratch(rvvm_hart_t *vm, uint32_t csr_id, uint32_t* dest, uint8_t op)
{
    UNUSED(csr_id);
    csr_helper(&vm->csr.scratch[PRIVILEGE_MACHINE], dest, op);
    return true;
}

static bool riscv32_csr_mepc(rvvm_hart_t *vm, uint32_t csr_id, uint32_t* dest, uint8_t op)
{
    UNUSED(csr_id);
    csr_helper(&vm->csr.epc[PRIVILEGE_MACHINE], dest, op);
    return true;
}

static bool riscv32_csr_mcause(rvvm_hart_t *vm, uint32_t csr_id, uint32_t* dest, uint8_t op)
{
    UNUSED(csr_id);
    csr_helper(&vm->csr.cause[PRIVILEGE_MACHINE], dest, op);
    return true;
}

static bool riscv32_csr_mtval(rvvm_hart_t *vm, uint32_t csr_id, uint32_t* dest, uint8_t op)
{
    UNUSED(csr_id);
    csr_helper(&vm->csr.tval[PRIVILEGE_MACHINE], dest, op);
    return true;
}

static bool riscv32_csr_mip(rvvm_hart_t *vm, uint32_t csr_id, uint32_t* dest, uint8_t op)
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
