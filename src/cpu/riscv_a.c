/*
riscv_a.c - RISC-V A Decoder, Interpreter
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

#define RISCV_CPU_SOURCE

#include "bit_ops.h"
#include "riscv_cpu.h"
#include "riscv32_mmu.h"
#include "spinlock.h"

#define AMO_LR           0x2
#define AMO_SC           0x3
#define AMO_SWAP         0x1
#define AMO_ADD          0x0
#define AMO_XOR          0x4
#define AMO_AND          0xC
#define AMO_OR           0x8
#define AMO_MIN          0x10
#define AMO_MAX          0x14
#define AMO_MINU         0x18
#define AMO_MAXU         0x1C

/*
* Currently, atomics are not handled directly, instead a global lock is used
* This is still a conforming implementation (and fast enough as for now)
* There is no multi-core support, but will eventually come with proper atomics
*/
// This may break in some ways when changing ISA, needs testing
static spinlock_t global_amo_lock;
static bool lr_reserved = false;

static void riscv_a_atomic_w(rvvm_hart_t *vm, const uint32_t instruction)
{
    regid_t rds = bit_cut(instruction, 7, 5);
    regid_t rs1 = bit_cut(instruction, 15, 5);
    regid_t rs2 = bit_cut(instruction, 20, 5);
    uint8_t op = bit_cut(instruction, 27, 5);
    xaddr_t addr = riscv_read_register(vm, rs1);
    xlen_t val = riscv_read_register(vm, rs2);
    xlen_t mval;
    uint8_t tmp[sizeof(uint32_t)];

    spin_lock(&global_amo_lock);

    if (!riscv_mem_op(vm, addr, tmp, sizeof(uint32_t), MMU_READ))
    {
        spin_unlock(&global_amo_lock);
        return;
    }

    mval = sign_extend(read_uint32_le(tmp), 32);
    riscv_write_register(vm, rds, mval);

    switch (op) {
    case AMO_LR:
        lr_reserved = true;
        break;
    case AMO_SC:
        if (lr_reserved) {
            lr_reserved = false;
            write_uint32_le(tmp, val);
            riscv_mem_op(vm, addr, tmp, sizeof(uint32_t), MMU_WRITE);
            riscv_write_register(vm, rds, 0);
        } else {
            riscv_write_register(vm, rds, 1);
        }
        break;
    case AMO_SWAP:
        write_uint32_le(tmp, val);
        riscv_mem_op(vm, addr, tmp, sizeof(uint32_t), MMU_WRITE);
        break;
    case AMO_ADD:
        write_uint32_le(tmp, mval + val);
        riscv_mem_op(vm, addr, tmp, sizeof(uint32_t), MMU_WRITE);
        break;
    case AMO_XOR:
        write_uint32_le(tmp, mval ^ val);
        riscv_mem_op(vm, addr, tmp, sizeof(uint32_t), MMU_WRITE);
        break;
    case AMO_AND:
        write_uint32_le(tmp, mval & val);
        riscv_mem_op(vm, addr, tmp, sizeof(uint32_t), MMU_WRITE);
        break;
    case AMO_OR:
        write_uint32_le(tmp, mval | val);
        riscv_mem_op(vm, addr, tmp, sizeof(uint32_t), MMU_WRITE);
        break;
    case AMO_MIN:
        write_uint32_le(tmp, ((sxlen_t)mval < (sxlen_t)val) ? mval : val);
        riscv_mem_op(vm, addr, tmp, sizeof(uint32_t), MMU_WRITE);
        break;
    case AMO_MAX:
        write_uint32_le(tmp, ((sxlen_t)mval > (sxlen_t)val) ? mval : val);
        riscv_mem_op(vm, addr, tmp, sizeof(uint32_t), MMU_WRITE);
        break;
    case AMO_MINU:
        write_uint32_le(tmp, (mval < val) ? mval : val);
        riscv_mem_op(vm, addr, tmp, sizeof(uint32_t), MMU_WRITE);
        break;
    case AMO_MAXU:
        write_uint32_le(tmp, (mval > val) ? mval : val);
        riscv_mem_op(vm, addr, tmp, sizeof(uint32_t), MMU_WRITE);
        break;
    default:
        riscv_illegal_insn(vm, instruction);
        break;
    }
    spin_unlock(&global_amo_lock);
}

#ifdef RV64
static void riscv_a_atomic_d(rvvm_hart_t *vm, const uint32_t instruction)
{
    regid_t rds = bit_cut(instruction, 7, 5);
    regid_t rs1 = bit_cut(instruction, 15, 5);
    regid_t rs2 = bit_cut(instruction, 20, 5);
    uint8_t op = bit_cut(instruction, 27, 5);
    xaddr_t addr = riscv_read_register(vm, rs1);
    xlen_t val = riscv_read_register(vm, rs2);
    xlen_t mval;
    uint8_t tmp[sizeof(uint64_t)];

    spin_lock(&global_amo_lock);

    if (!riscv_mem_op(vm, addr, tmp, sizeof(uint64_t), MMU_READ))
    {
        spin_unlock(&global_amo_lock);
        return;
    }

    mval = read_uint64_le(tmp);
    riscv_write_register(vm, rds, mval);

    switch (op) {
    case AMO_LR:
        lr_reserved = true;
        break;
    case AMO_SC:
        if (lr_reserved) {
            lr_reserved = false;
            write_uint64_le(tmp, val);
            riscv_mem_op(vm, addr, tmp, sizeof(uint64_t), MMU_WRITE);
            riscv_write_register(vm, rds, 0);
        } else {
            riscv_write_register(vm, rds, 1);
        }
        break;
    case AMO_SWAP:
        write_uint64_le(tmp, val);
        riscv_mem_op(vm, addr, tmp, sizeof(uint64_t), MMU_WRITE);
        break;
    case AMO_ADD:
        write_uint64_le(tmp, mval + val);
        riscv_mem_op(vm, addr, tmp, sizeof(uint64_t), MMU_WRITE);
        break;
    case AMO_XOR:
        write_uint64_le(tmp, mval ^ val);
        riscv_mem_op(vm, addr, tmp, sizeof(uint64_t), MMU_WRITE);
        break;
    case AMO_AND:
        write_uint64_le(tmp, mval & val);
        riscv_mem_op(vm, addr, tmp, sizeof(uint64_t), MMU_WRITE);
        break;
    case AMO_OR:
        write_uint64_le(tmp, mval | val);
        riscv_mem_op(vm, addr, tmp, sizeof(uint64_t), MMU_WRITE);
        break;
    case AMO_MIN:
        write_uint64_le(tmp, ((sxlen_t)mval < (sxlen_t)val) ? mval : val);
        riscv_mem_op(vm, addr, tmp, sizeof(uint64_t), MMU_WRITE);
        break;
    case AMO_MAX:
        write_uint64_le(tmp, ((sxlen_t)mval > (sxlen_t)val) ? mval : val);
        riscv_mem_op(vm, addr, tmp, sizeof(uint64_t), MMU_WRITE);
        break;
    case AMO_MINU:
        write_uint64_le(tmp, (mval < val) ? mval : val);
        riscv_mem_op(vm, addr, tmp, sizeof(uint64_t), MMU_WRITE);
        break;
    case AMO_MAXU:
        write_uint64_le(tmp, (mval > val) ? mval : val);
        riscv_mem_op(vm, addr, tmp, sizeof(uint64_t), MMU_WRITE);
        break;
    default:
        riscv_illegal_insn(vm, instruction);
        break;
    }
    spin_unlock(&global_amo_lock);
}
#endif

void riscv_a_init()
{
    spin_init(&global_amo_lock);
    riscv_install_opcode_ISB(RVA_ATOMIC_W, riscv_a_atomic_w);
#ifdef RV64
    riscv_install_opcode_ISB(RV64A_ATOMIC_D, riscv_a_atomic_d);
#endif
}
