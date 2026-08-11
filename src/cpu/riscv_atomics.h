/*
riscv_atomics.h - RISC-V Atomics interpreter
Copyright (C) 2024  LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#ifndef RVVM_RISCV_ATOMICS_H
#define RVVM_RISCV_ATOMICS_H

#include "riscv_common.h"

/*
 * AMO 5-bit operation code in insn[31:27]
 */

#define RISCV_AMO_LR   0x02
#define RISCV_AMO_SC   0x03
#define RISCV_AMO_CAS  0x05
#define RISCV_AMO_SWAP 0x01
#define RISCV_AMO_ADD  0x00
#define RISCV_AMO_AND  0x0C
#define RISCV_AMO_OR   0x08
#define RISCV_AMO_XOR  0x04
#define RISCV_AMO_MAX  0x14
#define RISCV_AMO_MIN  0x10
#define RISCV_AMO_MAXU 0x1C
#define RISCV_AMO_MINU 0x18

#define RISCV_AMO_I8   0x00
#define RISCV_AMO_I16  0x01
#define RISCV_AMO_I32  0x02
#define RISCV_AMO_I64  0x03
#define RISCV_AMO_I128 0x04

static forceinline void riscv_emulate_atomic_b(rvvm_hart_t* vm, const uint32_t insn)
{
    const uint32_t op    = insn >> 27;
    const size_t   rds   = bit_ext_u32(insn, 7, 5);
    const size_t   rs1   = bit_ext_u32(insn, 15, 5);
    const size_t   rs2   = bit_ext_u32(insn, 20, 5);
    const xaddr_t  vaddr = riscv_read_reg(vm, rs1);
    const uint8_t  val   = riscv_read_reg(vm, rs2);
    uint8_t        buf   = 0;
    void*          ptr   = NULL;

    if (unlikely(!((1U << op) & 0x11111133))) {
        // Illegal instruction
        riscv_illegal_insn(vm, insn);
        return;
    }
    ptr = riscv_rmw_translate(vm, vaddr, &buf, sizeof(buf));
    if (unlikely(!ptr)) {
        // Access fault
        return;
    }

    switch (op) {
        case RISCV_AMO_CAS: { // Zacas
            uint8_t exp = riscv_read_reg(vm, rds);
            atomic_cas_uint8_ex(ptr, &exp, val, false, ATOMIC_ACQ_REL, ATOMIC_ACQUIRE);
            riscv_write_reg(vm, rds, (int8_t)read_uint8(&exp));
            break;
        }
        case RISCV_AMO_SWAP:
            riscv_write_reg(vm, rds, (int8_t)atomic_swap_uint8(ptr, val));
            break;
        case RISCV_AMO_ADD:
            riscv_write_reg(vm, rds, (int8_t)atomic_add_uint8(ptr, val));
            break;
        case RISCV_AMO_AND:
            riscv_write_reg(vm, rds, (int8_t)atomic_and_uint8(ptr, val));
            break;
        case RISCV_AMO_OR:
            riscv_write_reg(vm, rds, (int8_t)atomic_or_uint8(ptr, val));
            break;
        case RISCV_AMO_XOR:
            riscv_write_reg(vm, rds, (int8_t)atomic_xor_uint8(ptr, val));
            break;
        case RISCV_AMO_MAX:
            riscv_write_reg(vm, rds, (int8_t)atomic_max_int8(ptr, val));
            break;
        case RISCV_AMO_MIN:
            riscv_write_reg(vm, rds, (int8_t)atomic_min_int8(ptr, val));
            break;
        case RISCV_AMO_MAXU:
            riscv_write_reg(vm, rds, (int8_t)atomic_maxu_uint8(ptr, val));
            break;
        case RISCV_AMO_MINU:
            riscv_write_reg(vm, rds, (int8_t)atomic_minu_uint8(ptr, val));
            break;
    }

    if (unlikely(ptr == &buf)) {
        // Commit MMIO atomic
        riscv_rmw_mmio_commit(vm, vaddr, &buf, sizeof(buf));
    }
}

static forceinline void riscv_emulate_atomic_h(rvvm_hart_t* vm, const uint32_t insn)
{
    const uint32_t op    = insn >> 27;
    const size_t   rds   = bit_ext_u32(insn, 7, 5);
    const size_t   rs1   = bit_ext_u32(insn, 15, 5);
    const size_t   rs2   = bit_ext_u32(insn, 20, 5);
    const xaddr_t  vaddr = riscv_read_reg(vm, rs1);
    const uint16_t val   = riscv_read_reg(vm, rs2);
    uint16_t       buf   = 0;
    void*          ptr   = NULL;

    if (unlikely(!((1U << op) & 0x11111133))) {
        // Illegal instruction
        riscv_illegal_insn(vm, insn);
        return;
    }
    if (unlikely(vaddr & 1)) {
        // Misaligned atomic
        riscv_trap(vm, RISCV_TRAP_STORE_MISALIGN, vaddr);
        return;
    }
    ptr = riscv_rmw_translate(vm, vaddr, &buf, sizeof(buf));
    if (unlikely(!ptr)) {
        // Access fault
        return;
    }

    switch (op) {
        case RISCV_AMO_CAS: { // Zacas
            uint16_t exp = 0, chg = 0;
            write_uint16_le(&exp, riscv_read_reg(vm, rds));
            write_uint16_le(&chg, val);
            atomic_cas_uint16_ex(ptr, &exp, chg, false, ATOMIC_ACQ_REL, ATOMIC_ACQUIRE);
            riscv_write_reg(vm, rds, (int16_t)read_uint16_le(&exp));
            break;
        }
        case RISCV_AMO_SWAP:
            riscv_write_reg(vm, rds, (int16_t)atomic_swap_uint16_le(ptr, val));
            break;
        case RISCV_AMO_ADD:
            riscv_write_reg(vm, rds, (int16_t)atomic_add_uint16_le(ptr, val));
            break;
        case RISCV_AMO_AND:
            riscv_write_reg(vm, rds, (int16_t)atomic_and_uint16_le(ptr, val));
            break;
        case RISCV_AMO_OR:
            riscv_write_reg(vm, rds, (int16_t)atomic_or_uint16_le(ptr, val));
            break;
        case RISCV_AMO_XOR:
            riscv_write_reg(vm, rds, (int16_t)atomic_xor_uint16_le(ptr, val));
            break;
        case RISCV_AMO_MAX:
            riscv_write_reg(vm, rds, (int16_t)atomic_max_int16_le(ptr, val));
            break;
        case RISCV_AMO_MIN:
            riscv_write_reg(vm, rds, (int16_t)atomic_min_int16_le(ptr, val));
            break;
        case RISCV_AMO_MAXU:
            riscv_write_reg(vm, rds, (int16_t)atomic_maxu_uint16_le(ptr, val));
            break;
        case RISCV_AMO_MINU:
            riscv_write_reg(vm, rds, (int16_t)atomic_minu_uint16_le(ptr, val));
            break;
    }

    if (unlikely(ptr == &buf)) {
        // Commit MMIO atomic
        riscv_rmw_mmio_commit(vm, vaddr, &buf, sizeof(buf));
    }
}

static forceinline void riscv_emulate_atomic_w(rvvm_hart_t* vm, const uint32_t insn)
{
    const uint32_t op    = insn >> 27;
    const size_t   rds   = bit_ext_u32(insn, 7, 5);
    const size_t   rs1   = bit_ext_u32(insn, 15, 5);
    const size_t   rs2   = bit_ext_u32(insn, 20, 5);
    const xaddr_t  vaddr = riscv_read_reg(vm, rs1);
    const uint32_t val   = riscv_read_reg(vm, rs2);
    uint32_t       buf   = 0;
    void*          ptr   = NULL;

    if (unlikely(!((1U << op) & 0x1111113F))) {
        // Illegal instruction
        riscv_illegal_insn(vm, insn);
        return;
    }
    if (unlikely(vaddr & 3)) {
        // Misaligned atomic
        uint32_t cause = (op == RISCV_AMO_LR) ? RISCV_TRAP_LOAD_MISALIGN : RISCV_TRAP_STORE_MISALIGN;
        riscv_trap(vm, cause, vaddr);
        return;
    }
    if (op == RISCV_AMO_LR) {
        ptr = riscv_ro_translate(vm, vaddr, &buf, sizeof(buf));
    } else {
        ptr = riscv_rmw_translate(vm, vaddr, &buf, sizeof(buf));
    }
    if (unlikely(!ptr)) {
        // Access fault
        return;
    }

    switch (op) {
        case RISCV_AMO_LR:
            // Mark our reservation
            vm->lrsc      = true;
            vm->lrsc_addr = vaddr;
            vm->lrsc_cas  = atomic_load_uint32_le(ptr);
            riscv_write_reg(vm, rds, (int32_t)vm->lrsc_cas);

            // Return to skip RMW MMIO commit
            return;
        case RISCV_AMO_SC:
            // If our reservation is valid, perform a CAS
            if (vm->lrsc && vm->lrsc_addr == vaddr && atomic_cas_uint32_le(ptr, vm->lrsc_cas, val)) {
                // SC success
                riscv_write_reg(vm, rds, 0);
                vm->lrsc = false;
            } else {
                // SC failed, skip RMW MMIO commit
                riscv_write_reg(vm, rds, 1);
                vm->lrsc = false;
                return;
            }
            break;
        case RISCV_AMO_CAS: { // Zacas
            uint32_t exp = 0, chg = 0;
            write_uint32_le(&exp, riscv_read_reg(vm, rds));
            write_uint32_le(&chg, val);
            atomic_cas_uint32_ex(ptr, &exp, chg, false, ATOMIC_ACQ_REL, ATOMIC_ACQUIRE);
            riscv_write_reg(vm, rds, (int32_t)read_uint32_le(&exp));
            break;
        }
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
    }

    if (unlikely(ptr == &buf)) {
        // Commit MMIO atomic
        riscv_rmw_mmio_commit(vm, vaddr, &buf, sizeof(buf));
    }
}

#if defined(RISCV64)

static forceinline void riscv_emulate_atomic_d(rvvm_hart_t* vm, const uint32_t insn)
{
    const uint32_t op    = insn >> 27;
    const size_t   rds   = bit_ext_u32(insn, 7, 5);
    const size_t   rs1   = bit_ext_u32(insn, 15, 5);
    const size_t   rs2   = bit_ext_u32(insn, 20, 5);
    const xaddr_t  vaddr = riscv_read_reg(vm, rs1);
    const uint64_t val   = riscv_read_reg(vm, rs2);
    uint64_t       buf;
    void*          ptr = NULL;

    if (unlikely(!((1U << op) & 0x1111113F))) {
        // Illegal instruction
        riscv_illegal_insn(vm, insn);
        return;
    }
    if (unlikely(vaddr & 7)) {
        // Misaligned atomic
        uint32_t cause = (op == RISCV_AMO_LR) ? RISCV_TRAP_LOAD_MISALIGN : RISCV_TRAP_STORE_MISALIGN;
        riscv_trap(vm, cause, vaddr);
        return;
    }
    if (op == RISCV_AMO_LR) {
        ptr = riscv_ro_translate(vm, vaddr, &buf, sizeof(buf));
    } else {
        ptr = riscv_rmw_translate(vm, vaddr, &buf, sizeof(buf));
    }
    if (unlikely(!ptr)) {
        // Access fault
        return;
    }

    switch (op) {
        case RISCV_AMO_LR:
            // Load-reserve, mark our reservation
            vm->lrsc      = true;
            vm->lrsc_addr = vaddr;
            vm->lrsc_cas  = atomic_load_uint64_le(ptr);
            riscv_write_reg(vm, rds, vm->lrsc_cas);

            // Return to skip RMW MMIO commit
            return;
        case RISCV_AMO_SC:
            // Store-conditional, perform CAS on reservation
            if (vm->lrsc && vm->lrsc_addr == vaddr && atomic_cas_uint64_le(ptr, vm->lrsc_cas, val)) {
                // SC success
                riscv_write_reg(vm, rds, 0);
                vm->lrsc = false;
            } else {
                // SC failed, skip RMW MMIO commit
                riscv_write_reg(vm, rds, 1);
                vm->lrsc = false;
                return;
            }
            break;
        case RISCV_AMO_CAS: { // Zacas
            uint64_t exp = 0, chg = 0;
            write_uint64_le(&exp, riscv_read_reg(vm, rds));
            write_uint64_le(&chg, val);
            atomic_cas_uint64_ex(ptr, &exp, chg, false, ATOMIC_ACQ_REL, ATOMIC_ACQUIRE);
            riscv_write_reg(vm, rds, read_uint64_le(&exp));
            break;
        }
        case RISCV_AMO_SWAP:
            riscv_write_reg(vm, rds, atomic_swap_uint64_le(ptr, val));
            break;
        case RISCV_AMO_ADD:
            riscv_write_reg(vm, rds, atomic_add_uint64_le(ptr, val));
            break;
        case RISCV_AMO_XOR:
            riscv_write_reg(vm, rds, atomic_xor_uint64_le(ptr, val));
            break;
        case RISCV_AMO_AND:
            riscv_write_reg(vm, rds, atomic_and_uint64_le(ptr, val));
            break;
        case RISCV_AMO_OR:
            riscv_write_reg(vm, rds, atomic_or_uint64_le(ptr, val));
            break;
        case RISCV_AMO_MIN:
            riscv_write_reg(vm, rds, atomic_min_int64_le(ptr, val));
            break;
        case RISCV_AMO_MAX:
            riscv_write_reg(vm, rds, atomic_max_int64_le(ptr, val));
            break;
        case RISCV_AMO_MINU:
            riscv_write_reg(vm, rds, atomic_minu_uint64_le(ptr, val));
            break;
        case RISCV_AMO_MAXU:
            riscv_write_reg(vm, rds, atomic_maxu_uint64_le(ptr, val));
            break;
    }

    if (unlikely(ptr == &buf)) {
        // Commit MMIO atomic
        riscv_rmw_mmio_commit(vm, vaddr, &buf, sizeof(buf));
    }
}

#endif

static forceinline void riscv_emulate_atomic_dwcas(rvvm_hart_t* vm, const uint32_t insn)
{
    const uint32_t op    = insn >> 27;
    const size_t   rds   = bit_ext_u32(insn, 7, 5);
    const size_t   rs1   = bit_ext_u32(insn, 15, 5);
    const size_t   rs2   = bit_ext_u32(insn, 20, 5);
    const xaddr_t  vaddr = riscv_read_reg(vm, rs1);

#if defined(RISCV64)
    uint64_t exp[2] = ZERO_INIT;
    uint64_t val[2] = ZERO_INIT;
#else
    uint64_t exp = 0, val = 0;
#endif

    xlen_t buf[2];
    void*  ptr = NULL;

    if (unlikely(op != RISCV_AMO_CAS || (rds & 1) || (rs2 & 1))) {
        // Illegal instruction
        riscv_illegal_insn(vm, insn);
        return;
    }
    if (unlikely(vaddr & (sizeof(val) - 1))) {
        // Misaligned dwcas
        riscv_trap(vm, RISCV_TRAP_STORE_MISALIGN, vaddr);
        return;
    }
    ptr = riscv_rmw_translate(vm, vaddr, buf, sizeof(buf));
    if (unlikely(!ptr)) {
        // Access fault
        return;
    }

#if defined(RISCV64)
    if (rds) {
        write_uint64_le(&exp[0], riscv_read_reg(vm, rds));
        write_uint64_le(&exp[1], riscv_read_reg(vm, rds + 1));
    }
    if (rs2) {
        write_uint64_le(&val[0], riscv_read_reg(vm, rs2));
        write_uint64_le(&val[1], riscv_read_reg(vm, rs2 + 1));
    }
    atomic_cas_uint128_ex(ptr, exp, val, false, ATOMIC_ACQ_REL, ATOMIC_ACQUIRE);
    if (rds) {
        riscv_write_reg(vm, rds, read_uint64_le(&exp[0]));
        riscv_write_reg(vm, rds + 1, read_uint64_le(&exp[1]));
    }
#else
    if (rds) {
        write_uint64_le(&exp, riscv_read_reg(vm, rds) | (((uint64_t)riscv_read_reg(vm, rds + 1)) << 32));
    }
    if (rs2) {
        write_uint64_le(&val, riscv_read_reg(vm, rs2) | (((uint64_t)riscv_read_reg(vm, rs2 + 1)) << 32));
    }
    atomic_cas_uint64_ex(ptr, &exp, val, false, ATOMIC_ACQ_REL, ATOMIC_ACQUIRE);
    if (rds) {
        riscv_write_reg(vm, rds, read_uint64_le(&exp));
        riscv_write_reg(vm, rds + 1, read_uint64_le(&exp) >> 32);
    }
#endif

    if (unlikely(ptr == &buf)) {
        // Commit MMIO atomic
        riscv_rmw_mmio_commit(vm, vaddr, buf, sizeof(buf));
    }
}

static slow_path func_opt_size void riscv_emulate_a_opc_amo(rvvm_hart_t* vm, const uint32_t insn)
{
    switch (bit_ext_u32(insn, 12, 3)) {
        case RISCV_AMO_I8: // Zabha
            riscv_emulate_atomic_b(vm, insn);
            return;
        case RISCV_AMO_I16: // Zabha
            riscv_emulate_atomic_h(vm, insn);
            return;
        case RISCV_AMO_I32:
            riscv_emulate_atomic_w(vm, insn);
            return;
#if defined(RISCV64)
        case RISCV_AMO_I64:
            riscv_emulate_atomic_d(vm, insn);
            return;
        case RISCV_AMO_I128: // Zacas
            riscv_emulate_atomic_dwcas(vm, insn);
            return;
#else
        case RISCV_AMO_I64: // Zacas
            riscv_emulate_atomic_dwcas(vm, insn);
            return;
#endif
    }

    riscv_illegal_insn(vm, insn);
}

#endif
