/*
riscv_atomics.h - RISC-V Atomics interpreter
Copyright (C) 2024  LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.

Alternatively, the contents of this file may be used under the terms
of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or any later version.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef RISCV_ATOMICS_H
#define RISCV_ATOMICS_H

#define RISCV_AMO_LR   0x2
#define RISCV_AMO_SC   0x3
#define RISCV_AMO_SWAP 0x1
#define RISCV_AMO_ADD  0x0
#define RISCV_AMO_XOR  0x4
#define RISCV_AMO_AND  0xC
#define RISCV_AMO_OR   0x8
#define RISCV_AMO_MIN  0x10
#define RISCV_AMO_MAX  0x14
#define RISCV_AMO_MINU 0x18
#define RISCV_AMO_MAXU 0x1C

static void riscv_invalidate_lrsc(rvvm_hart_t *vm)
{
    vector_foreach(vm->machine->harts, i) {
        rvvm_hart_t* hart = vector_at(vm->machine->harts, i);
        if (hart != vm) {
            atomic_store_uint32(&hart->lrsc, false);
        }
    }
}

static forceinline void riscv_emulate_atomic_w(rvvm_hart_t *vm, const uint32_t insn)
{
    const uint32_t op = insn >> 27;
    const regid_t rds = bit_cut(insn, 7, 5);
    const regid_t rs1 = bit_cut(insn, 15, 5);
    const regid_t rs2 = bit_cut(insn, 20, 5);
    const xaddr_t addr = riscv_read_reg(vm, rs1);
    const uint32_t val = riscv_read_reg(vm, rs2);
    uint8_t buff[4]; // MMIO atomics bounce buffer

    if (unlikely(addr & 3)) {
        riscv_trap(vm, TRAP_STORE_MISALIGN, 0);
        return;
    }

    void* ptr = riscv_vma_translate_w(vm, addr, buff, sizeof(buff));
    if (unlikely(ptr == NULL)) return;

    switch (op) {
        case RISCV_AMO_LR:
            // Mark our reservation
            atomic_store_uint32(&vm->lrsc, true);
            vm->lrsc_cas = atomic_load_uint32_le(ptr);
            riscv_write_reg(vm, rds, (int32_t)vm->lrsc_cas);
            break;
        case RISCV_AMO_SC:
            // Invalidate all other reservations
            riscv_invalidate_lrsc(vm);

            // If our reservation is valid, perform a CAS
            if (atomic_load_uint32(&vm->lrsc) && atomic_cas_uint32_le(ptr, vm->lrsc_cas, val)) {
                riscv_write_reg(vm, rds, 0);
            } else {
                riscv_write_reg(vm, rds, 1);
            }

            // Invalidate this hart reservation
            atomic_store_uint32(&vm->lrsc, false);
            break;
        case RISCV_AMO_SWAP:
            riscv_write_reg(vm, rds, (int32_t)atomic_swap_uint32_le(ptr, val));
            break;
        case RISCV_AMO_ADD:
            riscv_write_reg(vm, rds, (int32_t)atomic_add_uint32_le(ptr, val));
            break;
        case RISCV_AMO_XOR:
            riscv_write_reg(vm, rds, (int32_t)atomic_xor_uint32_le(ptr, val));
            break;
        case RISCV_AMO_AND:
            riscv_write_reg(vm, rds, (int32_t)atomic_and_uint32_le(ptr, val));
            break;
        case RISCV_AMO_OR:
            riscv_write_reg(vm, rds, (int32_t)atomic_or_uint32_le(ptr, val));
            break;
        case RISCV_AMO_MIN:
            riscv_write_reg(vm, rds, (int32_t)atomic_min_int32_le(ptr, val));
            break;
        case RISCV_AMO_MAX:
            riscv_write_reg(vm, rds, (int32_t)atomic_max_int32_le(ptr, val));
            break;
        case RISCV_AMO_MINU:
            riscv_write_reg(vm, rds, (int32_t)atomic_minu_uint32_le(ptr, val));
            break;
        case RISCV_AMO_MAXU:
            riscv_write_reg(vm, rds, (int32_t)atomic_maxu_uint32_le(ptr, val));
            break;
        default:
            riscv_illegal_insn(vm, insn);
            break;
    }

    if (unlikely(ptr == buff)) {
        riscv_mmu_vma_mmio_write(vm, addr, buff, sizeof(buff));
    }
}

#ifdef RV64

static forceinline void riscv_emulate_atomic_d(rvvm_hart_t *vm, const uint32_t insn)
{
    const uint32_t op = insn >> 27;
    const regid_t rds = bit_cut(insn, 7, 5);
    const regid_t rs1 = bit_cut(insn, 15, 5);
    const regid_t rs2 = bit_cut(insn, 20, 5);
    const xaddr_t addr = riscv_read_reg(vm, rs1);
    const uint64_t val = riscv_read_reg(vm, rs2);
    uint8_t buff[8]; // MMIO atomics bounce buffer

    if (unlikely(addr & 7)) {
        riscv_trap(vm, TRAP_STORE_MISALIGN, 0);
        return;
    }

    void* ptr = riscv_vma_translate_w(vm, addr, buff, sizeof(buff));
    if (unlikely(ptr == NULL)) return;

    switch (op) {
        case RISCV_AMO_LR:
            // Mark our reservation
            atomic_store_uint32(&vm->lrsc, true);
            vm->lrsc_cas = atomic_load_uint64_le(ptr);
            vm->registers[rds] = vm->lrsc_cas;
            break;
        case RISCV_AMO_SC:
            // Invalidate all other reservations
            riscv_invalidate_lrsc(vm);

            // If our reservation is valid, perform a CAS
            if (atomic_load_uint32(&vm->lrsc) && atomic_cas_uint64_le(ptr, vm->lrsc_cas, val)) {
                riscv_write_reg(vm, rds, 0);
            } else {
                riscv_write_reg(vm, rds, 1);
            }

            // Invalidate this hart reservation
            atomic_store_uint32(&vm->lrsc, false);
            break;
        case RISCV_AMO_SWAP:
            vm->registers[rds] = atomic_swap_uint64_le(ptr, val);
            break;
        case RISCV_AMO_ADD:
            vm->registers[rds] = atomic_add_uint64_le(ptr, val);
            break;
        case RISCV_AMO_XOR:
            vm->registers[rds] = atomic_xor_uint64_le(ptr, val);
            break;
        case RISCV_AMO_AND:
            vm->registers[rds] = atomic_and_uint64_le(ptr, val);
            break;
        case RISCV_AMO_OR:
            vm->registers[rds] = atomic_or_uint64_le(ptr, val);
            break;
        case RISCV_AMO_MIN:
            vm->registers[rds] = atomic_min_int64_le(ptr, val);
            break;
        case RISCV_AMO_MAX:
            vm->registers[rds] = atomic_max_int64_le(ptr, val);
            break;
        case RISCV_AMO_MINU:
            vm->registers[rds] = atomic_minu_uint64_le(ptr, val);
            break;
        case RISCV_AMO_MAXU:
            vm->registers[rds] = atomic_maxu_uint64_le(ptr, val);
            break;
        default:
            riscv_illegal_insn(vm, insn);
            break;
    }

    if (unlikely(ptr == buff)) {
        riscv_mmu_vma_mmio_write(vm, addr, buff, sizeof(buff));
    }
}

#endif

static inline void riscv_emulate_a_opc_amo(rvvm_hart_t* vm, const uint32_t insn)
{
    const uint32_t funct3 = bit_cut(insn, 12, 3);
    switch (funct3) {
        case 0x2:
            riscv_emulate_atomic_w(vm, insn);
            return;
#ifdef RV64
        case 0x3:
            riscv_emulate_atomic_d(vm, insn);
            return;
#endif
    }
    riscv_illegal_insn(vm, insn);
}

#endif
