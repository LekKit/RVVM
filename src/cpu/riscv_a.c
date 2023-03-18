/*
riscv_a.c - RISC-V A Decoder, Interpreter
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

#define RISCV_CPU_SOURCE

#include "bit_ops.h"
#include "riscv_cpu.h"
#include "riscv_mmu.h"
#include "atomics.h"

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

static void riscv_a_atomic_w(rvvm_hart_t *vm, const uint32_t instruction)
{
    regid_t rds = bit_cut(instruction, 7, 5);
    regid_t rs1 = bit_cut(instruction, 15, 5);
    regid_t rs2 = bit_cut(instruction, 20, 5);
    uint8_t op = bit_cut(instruction, 27, 5);
    xaddr_t addr = riscv_read_register(vm, rs1);
    uint32_t val = riscv_read_register(vm, rs2);
    uint8_t buff[4]; // MMIO atomics bounce buffer

    if (unlikely(addr & 3)) {
        riscv_trap(vm, TRAP_STORE_MISALIGN, 0);
        return;
    }

    void* ptr = riscv_vma_translate_w(vm, addr, buff, sizeof(buff));
    if (unlikely(ptr == NULL)) {
        return;
    }

    switch (op) {
        case AMO_LR:
            vm->lrsc = true;
            vm->lrsc_cas = atomic_load_uint32_le(ptr);
            riscv_write_register(vm, rds, (int32_t)vm->lrsc_cas);
            break;
        case AMO_SC:
            if (vm->lrsc && atomic_cas_uint32_le(ptr, vm->lrsc_cas, val)) {
                vm->lrsc = false;
                riscv_write_register(vm, rds, 0);
            } else {
                riscv_write_register(vm, rds, 1);
            }
            break;
        case AMO_SWAP:
            riscv_write_register(vm, rds, (int32_t)atomic_swap_uint32_le(ptr, val));
            break;
        case AMO_ADD:
            riscv_write_register(vm, rds, (int32_t)atomic_add_uint32_le(ptr, val));
            break;
        case AMO_XOR:
            riscv_write_register(vm, rds, (int32_t)atomic_xor_uint32_le(ptr, val));
            break;
        case AMO_AND:
            riscv_write_register(vm, rds, (int32_t)atomic_and_uint32_le(ptr, val));
            break;
        case AMO_OR:
            riscv_write_register(vm, rds, (int32_t)atomic_or_uint32_le(ptr, val));
            break;
        case AMO_MIN:
            riscv_write_register(vm, rds, (int32_t)atomic_min_int32_le(ptr, val));
            break;
        case AMO_MAX:
            riscv_write_register(vm, rds, (int32_t)atomic_max_int32_le(ptr, val));
            break;
        case AMO_MINU:
            riscv_write_register(vm, rds, (int32_t)atomic_minu_uint32_le(ptr, val));
            break;
        case AMO_MAXU:
            riscv_write_register(vm, rds, (int32_t)atomic_maxu_uint32_le(ptr, val));
            break;
        default:
            riscv_trap(vm, TRAP_ILL_INSTR, instruction);
            break;
    }

    if (unlikely(ptr == buff)) {
        riscv_mmu_vma_mmio_write(vm, addr, buff, sizeof(buff));
    }
}

#ifdef RV64
static void riscv_a_atomic_d(rvvm_hart_t *vm, const uint32_t instruction)
{
    regid_t rds = bit_cut(instruction, 7, 5);
    regid_t rs1 = bit_cut(instruction, 15, 5);
    regid_t rs2 = bit_cut(instruction, 20, 5);
    uint8_t op = bit_cut(instruction, 27, 5);
    xaddr_t addr = riscv_read_register(vm, rs1);
    uint64_t val = riscv_read_register(vm, rs2);
    uint8_t buff[8]; // MMIO atomics bounce buffer

    if (unlikely(addr & 7)) {
        riscv_trap(vm, TRAP_STORE_MISALIGN, 0);
        return;
    }

    void* ptr = riscv_vma_translate_w(vm, addr, buff, sizeof(buff));
    if (unlikely(ptr == NULL)) {
        return;
    }

    switch (op) {
        case AMO_LR:
            vm->lrsc = true;
            vm->lrsc_cas = atomic_load_uint64_le(ptr);
            vm->registers[rds] = vm->lrsc_cas;
            break;
        case AMO_SC:
            if (vm->lrsc && atomic_cas_uint64_le(ptr, vm->lrsc_cas, val)) {
                vm->lrsc = false;
                riscv_write_register(vm, rds, 0);
            } else {
                riscv_write_register(vm, rds, 1);
            }
            break;
        case AMO_SWAP:
            vm->registers[rds] = atomic_swap_uint64_le(ptr, val);
            break;
        case AMO_ADD:
            vm->registers[rds] = atomic_add_uint64_le(ptr, val);
            break;
        case AMO_XOR:
            vm->registers[rds] = atomic_xor_uint64_le(ptr, val);
            break;
        case AMO_AND:
            vm->registers[rds] = atomic_and_uint64_le(ptr, val);
            break;
        case AMO_OR:
            vm->registers[rds] = atomic_or_uint64_le(ptr, val);
            break;
        case AMO_MIN:
            vm->registers[rds] = atomic_min_int64_le(ptr, val);
            break;
        case AMO_MAX:
            vm->registers[rds] = atomic_max_int64_le(ptr, val);
            break;
        case AMO_MINU:
            vm->registers[rds] = atomic_minu_uint64_le(ptr, val);
            break;
        case AMO_MAXU:
            vm->registers[rds] = atomic_maxu_uint64_le(ptr, val);
            break;
        default:
            riscv_trap(vm, TRAP_ILL_INSTR, instruction);
            break;
    }

    if (unlikely(ptr == buff)) {
        riscv_mmu_vma_mmio_write(vm, addr, buff, sizeof(buff));
    }
}
#endif

void riscv_a_init(rvvm_hart_t* vm)
{
    riscv_install_opcode_ISB(vm, RVA_ATOMIC_W, riscv_a_atomic_w);
#ifdef RV64
    riscv_install_opcode_ISB(vm, RV64A_ATOMIC_D, riscv_a_atomic_d);
#else
    // Remove RV64A-only instructions from decoder
    riscv_install_opcode_ISB(vm, RV64A_ATOMIC_D, riscv_illegal_insn);
#endif
}
