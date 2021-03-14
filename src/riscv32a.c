/*
riscv32a.c - RISC-V Atomic operations extension
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

#include <assert.h>

#include "riscv32.h"
#include "riscv32_mmu.h"
#include "riscv32i.h"
#include "bit_ops.h"
#include "spinlock.h"

#define RISCV32A_VERSION 20

#define AMO_LR           0x2
#define AMO_SC           0x3
#define AMOSWAP          0x1
#define AMOADD           0x0
#define AMOXOR           0x4
#define AMOAND           0xC
#define AMOOR            0x8
#define AMOMIN           0x10
#define AMOMAX           0x14
#define AMOMINU          0x18
#define AMOMAXU          0x1C

#define RISCV32A_ATOMIC  0x4B

/*
* Currently, atomics are not handled directly, instead a global lock is used
* This is still a conforming implementation (and fast enough as for now)
* There is no multi-core support, but will eventually come with proper atomics
*/
static spinlock_t global_amo_lock;
static virtaddr_t lr_addr;

static inline int32_t amo_min(int32_t a, int32_t b)
{
    return (a < b) ? a : b;
}

static inline int32_t amo_max(int32_t a, int32_t b)
{
    return (a > b) ? a : b;
}

static inline uint32_t amo_minu(uint32_t a, uint32_t b)
{
    return (a < b) ? a : b;
}

static inline uint32_t amo_maxu(uint32_t a, uint32_t b)
{
    return (a > b) ? a : b;
}

static void riscv32a_atomic(riscv32_vm_state_t *vm, const uint32_t instruction)
{
    uint32_t rds = cut_bits(instruction, 7, 5);
    uint32_t rs1 = cut_bits(instruction, 15, 5);
    uint32_t rs2 = cut_bits(instruction, 20, 5);
    uint32_t op = cut_bits(instruction, 27, 5);
    virtaddr_t address = riscv32i_read_register_u(vm, rs1);
    reg_t val = riscv32i_read_register_u(vm, rs2);
    uint8_t tmp[4];

    spin_lock(&global_amo_lock);
    switch (op) {
    case AMO_LR:
        lr_addr = address;
        riscv32_mem_op(vm, address, tmp, 4, MMU_READ);
        riscv32i_write_register_u(vm, rds, read_uint32_le(tmp));
        riscv32_debug(vm, "RV32A: lr.w %r, %r, %r", rds, rs2, rs1);
        break;
    case AMO_SC:
        if (lr_addr == address) {
            write_uint32_le(tmp, val);
            riscv32_mem_op(vm, address, tmp, 4, MMU_WRITE);
            riscv32i_write_register_u(vm, rds, 0);
        } else {
            riscv32i_write_register_u(vm, rds, 1);
        }
        riscv32_debug(vm, "RV32A: sc.w %r, %r, %r", rds, rs2, rs1);
        break;
    case AMOSWAP:
        riscv32_mem_op(vm, address, tmp, 4, MMU_READ);
        riscv32i_write_register_u(vm, rds, read_uint32_le(tmp));
        write_uint32_le(tmp, val);
        riscv32_mem_op(vm, address, tmp, 4, MMU_WRITE);
        riscv32_debug(vm, "RV32A: amoswap.w %r, %r, %r", rds, rs2, rs1);
        break;
    case AMOADD:
        riscv32_mem_op(vm, address, tmp, 4, MMU_READ);
        riscv32i_write_register_u(vm, rds, read_uint32_le(tmp));
        write_uint32_le(tmp, read_uint32_le(tmp) + val);
        riscv32_mem_op(vm, address, tmp, 4, MMU_WRITE);
        riscv32_debug(vm, "RV32A: amoadd.w %r, %r, %r", rds, rs2, rs1);
        break;
    case AMOXOR:
        riscv32_mem_op(vm, address, tmp, 4, MMU_READ);
        riscv32i_write_register_u(vm, rds, read_uint32_le(tmp));
        write_uint32_le(tmp, read_uint32_le(tmp) ^ val);
        riscv32_mem_op(vm, address, tmp, 4, MMU_WRITE);
        riscv32_debug(vm, "RV32A: amoxor.w %r, %r, %r", rds, rs2, rs1);
        break;
    case AMOAND:
        riscv32_mem_op(vm, address, tmp, 4, MMU_READ);
        riscv32i_write_register_u(vm, rds, read_uint32_le(tmp));
        write_uint32_le(tmp, read_uint32_le(tmp) & val);
        riscv32_mem_op(vm, address, tmp, 4, MMU_WRITE);
        riscv32_debug(vm, "RV32A: amoand.w %r, %r, %r", rds, rs2, rs1);
        break;
    case AMOOR:
        riscv32_mem_op(vm, address, tmp, 4, MMU_READ);
        riscv32i_write_register_u(vm, rds, read_uint32_le(tmp));
        write_uint32_le(tmp, read_uint32_le(tmp) | val);
        riscv32_mem_op(vm, address, tmp, 4, MMU_WRITE);
        riscv32_debug(vm, "RV32A: amoor.w %r, %r, %r", rds, rs2, rs1);
        break;
    case AMOMIN:
        riscv32_mem_op(vm, address, tmp, 4, MMU_READ);
        riscv32i_write_register_u(vm, rds, read_uint32_le(tmp));
        write_uint32_le(tmp, amo_min(read_uint32_le(tmp), val));
        riscv32_mem_op(vm, address, tmp, 4, MMU_WRITE);
        riscv32_debug(vm, "RV32A: amomin.w %r, %r, %r", rds, rs2, rs1);
        break;
    case AMOMAX:
        riscv32_mem_op(vm, address, tmp, 4, MMU_READ);
        riscv32i_write_register_u(vm, rds, read_uint32_le(tmp));
        write_uint32_le(tmp, amo_max(read_uint32_le(tmp), val));
        riscv32_mem_op(vm, address, tmp, 4, MMU_WRITE);
        riscv32_debug(vm, "RV32A: amomax.w %r, %r, %r", rds, rs2, rs1);
        break;
    case AMOMINU:
        riscv32_mem_op(vm, address, tmp, 4, MMU_READ);
        riscv32i_write_register_u(vm, rds, read_uint32_le(tmp));
        write_uint32_le(tmp, amo_minu(read_uint32_le(tmp), val));
        riscv32_mem_op(vm, address, tmp, 4, MMU_WRITE);
        riscv32_debug(vm, "RV32A: amominu.w %r, %r, %r", rds, rs2, rs1);
        break;
    case AMOMAXU:
        riscv32_mem_op(vm, address, tmp, 4, MMU_READ);
        riscv32i_write_register_u(vm, rds, read_uint32_le(tmp));
        write_uint32_le(tmp, amo_maxu(read_uint32_le(tmp), val));
        riscv32_mem_op(vm, address, tmp, 4, MMU_WRITE);
        riscv32_debug(vm, "RV32A: amomaxu.w %r, %r, %r", rds, rs2, rs1);
        break;
    default:
        riscv32_debug_always(vm, "RV32A: illegal instruction %h", instruction);
        riscv32_trap(vm, TRAP_ILL_INSTR, instruction);
        break;
    }
    spin_unlock(&global_amo_lock);
}

void riscv32a_init()
{
    spin_init(&global_amo_lock);
    smudge_opcode_ISB(RISCV32A_ATOMIC, riscv32a_atomic);
}
