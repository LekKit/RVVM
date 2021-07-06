/*
riscv_f.c - RISC-V F/D extension decoder and interpreter
Copyright (C) 2021  cerg2010cerg2010 <github.com/cerg2010cerg2010>

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

/*
 * NOTE: This file is compiled twice.
 * RVD macro is defined for D extension, uses F otherwise
 */

#define RISCV_CPU_SOURCE

#include "compiler.h"
#include "bit_ops.h"
#include "riscv_cpu.h"
#include "riscv32_mmu.h"

#include <fenv.h>
#include <float.h>
/* well... */
#include <tgmath.h>

#ifdef RVD
    #define SIGNIFICAND_SIZE DBL_MANT_DIG /* used for sNaN/qNaN classification */
    /* current native float type, might be replaced with softfloat later */
    typedef double fnative_t;
    /* bit 26 of opcode instruction */
    #define BIT26 0

    #define read_fpu(x) read_double(x)
    #define write_fpu(p, x) write_double(p, x)
    #define fpu_read_register fpu_read_register64
    #define fpu_write_register fpu_write_register64
#else
    #define SIGNIFICAND_SIZE FLT_MANT_DIG /* used for sNaN/qNaN classification */
    /* current native float type, might be replaced with softfloat later */
    typedef float fnative_t;
    /* bit 26 of opcode instruction */
    #define BIT26 0

    #define read_fpu(x) read_float(x)
    #define write_fpu(p, x) write_float(p, x)
    #define fpu_read_register fpu_read_register32
    #define fpu_write_register fpu_write_register32
#endif

/* handlers for FPU operations, to be replaced with JIT or softfpu? */
#define fpu_add(x, y)      canonize_nan((fnative_t)(x) + (fnative_t)(y))
#define fpu_sub(x, y)      canonize_nan((fnative_t)(x) - (fnative_t)(y))
#define fpu_mul(x, y)      canonize_nan((fnative_t)(x) * (fnative_t)(y))
#define fpu_div(x, y)      canonize_nan((fnative_t)(x) / (fnative_t)(y))
#define fpu_neg(x)         canonize_nan(-(fnative_t)(x))
#define fpu_sqrt(x)        canonize_nan(sqrt((fnative_t)(x)))
#define fpu_min(x, y)      fpu_min_impl(x, y)
#define fpu_max(x, y)      fpu_max_impl(x, y)
#define fpu_eq(x, y)                  ((fnative_t)(x) == (fnative_t)(y))
#define fpu_lt(x, y)                  ((fnative_t)(x) <  (fnative_t)(y))
#define fpu_le(x, y)                  ((fnative_t)(x) <= (fnative_t)(y))
#define fpu_sign_check(x)             (signbit(x))
#define fpu_sign_set(x, s) (fnative_t)((s) ? copysign((fnative_t)(x), (fnative_t)(-1.0)) : copysign((fnative_t)(x), (fnative_t)(1.0)))
#define fpu_fp2int(t, x)   fpu_fp2int_##t(x)
#define fpu_int2fp(t, i)   canonize_nan((t) (i))
#define fpu_fp2fp(t, x)    canonize_nan((t)(x))
#define fpu_fclass(x)                 fpu_fclass_impl(x)

enum {
    FCL_NEG_INF,
    FCL_NEG_NORMAL,
    FCL_NEG_SUBNORMAL,
    FCL_NEG_ZERO,
    FCL_POS_ZERO,
    FCL_POS_SUBNORMAL,
    FCL_POS_NORMAL,
    FCL_POS_INF,
    FCL_NAN_SIG,
    FCL_NAN_QUIET
};

static inline uint8_t fpu_fclass_impl(fnative_t x)
{
    switch (fpclassify(x)) {
        case FP_INFINITE: return fpu_sign_check(x) ? FCL_NEG_INF : FCL_POS_INF;
        case FP_NORMAL: return fpu_sign_check(x) ? FCL_NEG_NORMAL : FCL_POS_NORMAL;
        case FP_SUBNORMAL: return fpu_sign_check(x) ? FCL_NEG_SUBNORMAL : FCL_POS_SUBNORMAL;
        case FP_ZERO: return fpu_sign_check(x) ? FCL_NEG_ZERO : FCL_POS_ZERO;
        case FP_NAN: {
                         /* Bitwise fuckery to check for sNaN/qNaN because C API is so awesome */
                         uint8_t tmp[sizeof(fnative_t)];
                         write_fpu(tmp, x);
                         if (bit_check(tmp[(SIGNIFICAND_SIZE-2) / 8], (SIGNIFICAND_SIZE-2) % 8)) {
                             return FCL_NAN_QUIET;
                         } else {
                             return FCL_NAN_SIG;
                         }
                     }
    }

    /* never reached */
    return FCL_NAN_SIG;
}

static inline fnative_t canonize_nan(fnative_t x)
{
    if (!isnan(x)) {
        return x;
    }

#ifndef RVD
    uint32_t nan = 0x7fc00000;
#else
    uint64_t nan = 0x7ff8000000000000;
#endif

    memcpy(&x, &nan, sizeof(fnative_t));
    return x;
}

static inline xlen_t fpu_fp2int_xlen_t(fnative_t x)
{
    long long ret = llrint(x);
    bool isinvalid = fetestexcept(FE_INVALID);
    if (!isinvalid) {
        isinvalid = ret < 0 || ret > (~(xlen_t)0);
        if (isinvalid) feraiseexcept(FE_INVALID);
    }

    if (isinvalid) {
        if (isinf(x) && fpu_sign_check(x)) {
            return 0;
        } else if (isnan(x) || (isinf(x) && !fpu_sign_check(x))) {
            return ~(xlen_t)0;
        } else if (!fpu_sign_check(x)) {
            return ~(xlen_t)0;
        } else {
            return 0;
        }
    }

    return ret;
}

static inline sxlen_t fpu_fp2int_sxlen_t(fnative_t x)
{
    long long ret = llrint(x);
    bool isinvalid = fetestexcept(FE_INVALID);
    if (!isinvalid) {
        isinvalid = ret < (sxlen_t)~(~(xlen_t)0 >> 1)
                 || ret > (sxlen_t)(~(xlen_t)0 >> 1);
        if (isinvalid) feraiseexcept(FE_INVALID);
    }

    if (isinvalid) {
        if (isinf(x) && fpu_sign_check(x)) {
            return ~(~(xlen_t)0 >> 1);
        } else if (isnan(x) || (isinf(x) && !fpu_sign_check(x))) {
            return ~(xlen_t)0 >> 1;
        } else if (!fpu_sign_check(x)) {
            return ~(xlen_t)0 >> 1;
        } else {
            return ~(~(xlen_t)0 >> 1);
        }
    }

    return ret;
}

static inline fnative_t fpu_min_impl(fnative_t x, fnative_t y)
{
    if (fabs(x) == 0.0 || fabs(y) == 0.0) {
        return fpu_sign_check(x) ? x : y;
    }

    fnative_t res = fmin(x, y);

    if (isnan(res)) {
        if (!isnan(x)) {
            return x;
        } else if (!isnan(y)) {
            return y;
        } else {
            return canonize_nan(res);
        }
    }

    return res;
}

static inline fnative_t fpu_max_impl(fnative_t x, fnative_t y)
{
    if (fabs(x) == 0.0 || fabs(y) == 0.0) {
        return fpu_sign_check(x) ? y : x;
    }

    fnative_t res = fmax(x, y);

    if (isnan(res)) {
        if (!isnan(x)) {
            return x;
        } else if (!isnan(y)) {
            return y;
        } else {
            return canonize_nan(res);
        }
    }

    return res;
}

/* type for rounding mode */
typedef uint8_t rm_t;

/* funct7 opcodes, shifted right by 2 bits */
#define FT7_FADD 0x0
#define FT7_FSUB 0x1
#define FT7_FMUL 0x2
#define FT7_FDIV 0x3
#define FT7_FSQRT 0xb /* rs2 == 0 */
#define FT7_FSGN 0x4 /* multiple variants, selected by funct3 */
#define FT7_FMINMAX 0x5 /* distinguished by funct3 */
#define FT7_FCMP 0x14 /* distinguished by funct3 */
#define FT7_FCVT_W_S 0x18 /* rs2 == 0 (signed) or 1 (unsigned) */
#define FT7_FCVT_S_W 0x1a /* rs2 == 0 (signed) or 1 (unsigned) */
#ifndef RVD
#define FT7_FMV_X_W 0x1c /* rs2 == 0, funct3 == 0 (fmv) or 1 (fclass) */
#define FT7_FMV_W_X 0x1e /* rs2 == 0, funct3 == 0 */

/* this is encoded as F instruction */
#define FT7_FCVT_S_D 0x8 /* rs2 == 1 */
#else
#define FT7_FCVT_D_S 0x8 /* rs2 == 0 */
#define FT7_FCLASS 0x1c /* rs2 == 0, funct3 == 0 (fmv) or 1 (fclass) */
#endif

#if !defined(RVD) && !defined(RV64)
/* Sets rounding mode, returns previous value */
rm_t fpu_set_rm(rvvm_hart_t *vm, rm_t newrm)
{
    if (likely(newrm == RM_DYN)) {
        /* do nothing - rounding mode should be already set with csr */
        return RM_DYN;
    }

    switch (newrm) {
        case RM_RNE:
            fesetround(FE_TONEAREST);
            break;
        case RM_RTZ:
            fesetround(FE_TOWARDZERO);
            break;
        case RM_RDN:
            fesetround(FE_DOWNWARD);
            break;
        case RM_RUP:
            fesetround(FE_UPWARD);
            break;
        case RM_RMM:
            /* TODO: handle this somehow? */
            fesetround(FE_TONEAREST);
            break;
        default:
            return RM_INVALID;
    }

    rm_t oldrm = bit_cut(vm->csr.fcsr, 5, 3);
    if (unlikely(oldrm > RM_RMM)) {
        return RM_INVALID;
    }
    return oldrm;
}

void fpu_set_fs(rvvm_hart_t *vm, uint8_t value)
{
    vm->csr.status = bit_replace(vm->csr.status, 13, 2, value);

    /* Also update the SD bit */
    vm->csr.status = bit_replace(vm->csr.status, (1 << SHAMT_BITS) - 1, 1,
            value == S_DIRTY || bit_cut(vm->csr.status, 15, 2) == S_DIRTY);
}

bool fpu_is_enabled(rvvm_hart_t *vm)
{
    return bit_cut(vm->csr.status, 13, 2) != S_OFF;
}
#endif

static void riscv_f_flw(rvvm_hart_t *vm, const uint32_t insn)
{
    regid_t rds = bit_cut(insn, 7, 5);
    regid_t rs1 = bit_cut(insn, 15, 5);
    sxlen_t offset = sign_extend(bit_cut(insn, 20, 12), 12);

    xaddr_t addr = riscv_read_register(vm, rs1) + offset;
    uint8_t val[sizeof(fnative_t)];

    if (likely(riscv_mem_op(vm, addr, val, sizeof(val), MMU_READ))) {
        fpu_write_register(vm, rds, read_fpu(val));
    }
}

static void riscv_f_fsw(rvvm_hart_t *vm, const uint32_t insn)
{
    regid_t rs1 = bit_cut(insn, 15, 5);
    regid_t rs2 = bit_cut(insn, 20, 5);
    sxlen_t offset = sign_extend(bit_cut(insn, 7, 5) |
                               (bit_cut(insn, 25, 7) << 5), 12);

    xaddr_t addr = riscv_read_register(vm, rs1) + offset;
    uint8_t val[sizeof(fnative_t)];
    write_fpu(val, fpu_read_register(vm, rs2));

    riscv_mem_op(vm, addr, val, sizeof(val), MMU_WRITE);
}

static void riscv_f_fmadd(rvvm_hart_t *vm, const uint32_t insn)
{
    if (unlikely(bit_check(insn, 26) != BIT26)) {
        riscv_illegal_insn(vm, insn);
        return;
    }

    regid_t rs1 = bit_cut(insn, 15, 5);
    regid_t rs2 = bit_cut(insn, 20, 5);
    rm_t rm = fpu_set_rm(vm, bit_cut(insn, 12, 3));
    if (unlikely(rm == RM_INVALID)) {
        riscv_illegal_insn(vm, insn);
        return;
    }
    regid_t rd = bit_cut(insn, 7, 5);
    regid_t rs3 = bit_cut(insn, 27, 5);

    fnative_t res = fpu_add(fpu_mul(fpu_read_register(vm, rs1), fpu_read_register(vm, rs2)), fpu_read_register(vm, rs3));
    fpu_write_register(vm, rd, res);
    fpu_set_rm(vm, rm);
}

static void riscv_f_fmsub(rvvm_hart_t *vm, const uint32_t insn)
{
    if (unlikely(bit_check(insn, 26) != BIT26)) {
        riscv_illegal_insn(vm, insn);
        return;
    }

    regid_t rs1 = bit_cut(insn, 15, 5);
    regid_t rs2 = bit_cut(insn, 20, 5);
    rm_t rm = fpu_set_rm(vm, bit_cut(insn, 12, 3));
    if (unlikely(rm == RM_INVALID)) {
        riscv_illegal_insn(vm, insn);
        return;
    }
    regid_t rd = bit_cut(insn, 7, 5);
    regid_t rs3 = bit_cut(insn, 27, 5);

    fnative_t res = fpu_sub(fpu_mul(fpu_read_register(vm, rs1), fpu_read_register(vm, rs2)), fpu_read_register(vm, rs3));
    fpu_write_register(vm, rd, res);
    fpu_set_rm(vm, rm);
}

static void riscv_f_fnmadd(rvvm_hart_t *vm, const uint32_t insn)
{
    if (unlikely(bit_check(insn, 26) != BIT26)) {
        riscv_illegal_insn(vm, insn);
        return;
    }

    regid_t rs1 = bit_cut(insn, 15, 5);
    regid_t rs2 = bit_cut(insn, 20, 5);
    rm_t rm = fpu_set_rm(vm, bit_cut(insn, 12, 3));
    if (unlikely(rm == RM_INVALID)) {
        riscv_illegal_insn(vm, insn);
        return;
    }
    regid_t rd = bit_cut(insn, 7, 5);
    regid_t rs3 = bit_cut(insn, 27, 5);

    fnative_t res = fpu_sub(fpu_neg(fpu_mul(fpu_read_register(vm, rs1), fpu_read_register(vm, rs2))), fpu_read_register(vm, rs3));
    fpu_write_register(vm, rd, res);
    fpu_set_rm(vm, rm);
}

static void riscv_f_fnmsub(rvvm_hart_t *vm, const uint32_t insn)
{
    if (unlikely(bit_check(insn, 26) != BIT26)) {
        riscv_illegal_insn(vm, insn);
        return;
    }

    regid_t rs1 = bit_cut(insn, 15, 5);
    regid_t rs2 = bit_cut(insn, 20, 5);
    rm_t rm = fpu_set_rm(vm, bit_cut(insn, 12, 3));
    if (unlikely(rm == RM_INVALID)) {
        riscv_illegal_insn(vm, insn);
        return;
    }
    regid_t rd = bit_cut(insn, 7, 5);
    regid_t rs3 = bit_cut(insn, 27, 5);

    fnative_t res = fpu_add(fpu_neg(fpu_mul(fpu_read_register(vm, rs1), fpu_read_register(vm, rs2))), fpu_read_register(vm, rs3));
    fpu_write_register(vm, rd, res);
    fpu_set_rm(vm, rm);
}

static inline void riscv_f_fadd(rvvm_hart_t *vm, regid_t rs1, regid_t rs2, regid_t rd)
{
    fpu_write_register(vm, rd, fpu_add(fpu_read_register(vm, rs1), fpu_read_register(vm, rs2)));
}

static inline void riscv_f_fsub(rvvm_hart_t *vm, regid_t rs1, regid_t rs2, regid_t rd)
{
    fpu_write_register(vm, rd, fpu_sub(fpu_read_register(vm, rs1), fpu_read_register(vm, rs2)));
}

static inline void riscv_f_fmul(rvvm_hart_t *vm, regid_t rs1, regid_t rs2, regid_t rd)
{
    fpu_write_register(vm, rd, fpu_mul(fpu_read_register(vm, rs1), fpu_read_register(vm, rs2)));
}

static inline void riscv_f_fdiv(rvvm_hart_t *vm, regid_t rs1, regid_t rs2, regid_t rd)
{
    fpu_write_register(vm, rd, fpu_div(fpu_read_register(vm, rs1), fpu_read_register(vm, rs2)));
}

static inline void riscv_f_fsqrt(rvvm_hart_t *vm, regid_t rs1, regid_t rd)
{
    fpu_write_register(vm, rd, fpu_sqrt(fpu_read_register(vm, rs1)));
}

static inline void riscv_f_fsgnj(rvvm_hart_t *vm, regid_t rs1, regid_t rs2, regid_t rd)
{
    fpu_write_register(vm, rd, fpu_sign_set(fpu_read_register(vm, rs1), !!fpu_sign_check(fpu_read_register(vm, rs2))));
}

static inline void riscv_f_fsgnjn(rvvm_hart_t *vm, regid_t rs1, regid_t rs2, regid_t rd)
{
    fpu_write_register(vm, rd, fpu_sign_set(fpu_read_register(vm, rs1), !fpu_sign_check(fpu_read_register(vm, rs2))));
}

static inline void riscv_f_fsgnjx(rvvm_hart_t *vm, regid_t rs1, regid_t rs2, regid_t rd)
{
    fnative_t rs1val = fpu_read_register(vm, rs1);
    fpu_write_register(vm, rd, fpu_sign_set(rs1val, !!fpu_sign_check(rs1val) ^ !!fpu_sign_check(fpu_read_register(vm, rs2))));
}

static inline void riscv_f_fmin(rvvm_hart_t *vm, regid_t rs1, regid_t rs2, regid_t rd)
{
    fpu_write_register(vm, rd, fpu_min(fpu_read_register(vm, rs1), fpu_read_register(vm, rs2)));
}

static inline void riscv_f_fmax(rvvm_hart_t *vm, regid_t rs1, regid_t rs2, regid_t rd)
{
    fpu_write_register(vm, rd, fpu_max(fpu_read_register(vm, rs1), fpu_read_register(vm, rs2)));
}

static inline void riscv_f_fcvt_w_s(rvvm_hart_t *vm, regid_t rs1, regid_t rd, bool is_unsigned)
{
    if (is_unsigned) {
        riscv_write_register(vm, rd, fpu_fp2int(xlen_t, fpu_read_register(vm, rs1)));
    } else {
        riscv_write_register(vm, rd, fpu_fp2int(sxlen_t, fpu_read_register(vm, rs1)));
    }
}

static inline void riscv_f_fcvt_s_w(rvvm_hart_t *vm, regid_t rs1, regid_t rd, bool is_unsigned)
{
    if (is_unsigned) {
        fpu_write_register(vm, rd, fpu_int2fp(xlen_t, riscv_read_register(vm, rs1)));
    } else {
        fpu_write_register(vm, rd, fpu_int2fp(sxlen_t, riscv_read_register(vm, rs1)));
    }
}

#ifndef RVD
static inline void riscv_f_fmv_x_w(rvvm_hart_t *vm, regid_t rs1, regid_t rd)
{
    fnative_t val = fpu_read_register(vm, rs1);
    riscv_write_register(vm, rd, read_uint32_le(&val));
}

static inline void riscv_f_fmv_w_x(rvvm_hart_t *vm, regid_t rs1, regid_t rd)
{
    fnative_t val;
    write_uint32_le(&val, riscv_read_register(vm, rs1));
    fpu_write_register(vm, rd, val);
}

static inline void riscv_f_fcvt_s_d(rvvm_hart_t *vm, regid_t rs1, regid_t rd)
{
    double val = read_double(&vm->fpu_registers[rs1]);
    fpu_write_register(vm, rd, fpu_fp2fp(fnative_t, val));
}
#else
static inline void riscv_f_fcvt_d_s(rvvm_hart_t *vm, regid_t rs1, regid_t rd)
{
    float val = read_float(&vm->fpu_registers[rs1]);
    fpu_write_register(vm, rd, fpu_fp2fp(fnative_t, val));
}
#endif
static inline void riscv_f_fclass(rvvm_hart_t *vm, regid_t rs1, regid_t rd)
{
    riscv_write_register(vm, rd, 1 << fpu_fclass(fpu_read_register(vm, rs1)));
}

static inline void riscv_f_feq(rvvm_hart_t *vm, regid_t rs1, regid_t rs2, regid_t rd)
{
    riscv_write_register(vm, rd, !!fpu_eq(fpu_read_register(vm, rs1), fpu_read_register(vm, rs2)));
}

static inline void riscv_f_flt(rvvm_hart_t *vm, regid_t rs1, regid_t rs2, regid_t rd)
{
    riscv_write_register(vm, rd, !!fpu_lt(fpu_read_register(vm, rs1), fpu_read_register(vm, rs2)));
}

static inline void riscv_f_fle(rvvm_hart_t *vm, regid_t rs1, regid_t rs2, regid_t rd)
{
    riscv_write_register(vm, rd, !!fpu_le(fpu_read_register(vm, rs1), fpu_read_register(vm, rs2)));
}

static void riscv_f_other(rvvm_hart_t *vm, const uint32_t insn)
{
    if (unlikely(bit_check(insn, 26) != BIT26)) {
        riscv_illegal_insn(vm, insn);
        return;
    }

    regid_t rs1 = bit_cut(insn, 15, 5);
    regid_t rs2 = bit_cut(insn, 20, 5);
    rm_t rm = bit_cut(insn, 12, 3);
    regid_t rd = bit_cut(insn, 7, 5);
    regid_t rs3 = bit_cut(insn, 27, 5);

    switch (rs3) {
        case FT7_FADD:
            rm = fpu_set_rm(vm, rm);
            if (unlikely(rm == RM_INVALID)) {
                riscv_illegal_insn(vm, insn);
                return;
            }
            riscv_f_fadd(vm, rs1, rs2, rd);
            fpu_set_rm(vm, rm);
            break;
        case FT7_FSUB:
            rm = fpu_set_rm(vm, rm);
            if (unlikely(rm == RM_INVALID)) {
                riscv_illegal_insn(vm, insn);
                return;
            }
            riscv_f_fsub(vm, rs1, rs2, rd);
            fpu_set_rm(vm, rm);
            break;
        case FT7_FMUL:
            rm = fpu_set_rm(vm, rm);
            if (unlikely(rm == RM_INVALID)) {
                riscv_illegal_insn(vm, insn);
                return;
            }
            riscv_f_fmul(vm, rs1, rs2, rd);
            fpu_set_rm(vm, rm);
            break;
        case FT7_FDIV:
            rm = fpu_set_rm(vm, rm);
            if (unlikely(rm == RM_INVALID)) {
                riscv_illegal_insn(vm, insn);
                return;
            }
            riscv_f_fdiv(vm, rs1, rs2, rd);
            fpu_set_rm(vm, rm);
            break;
        case FT7_FSQRT:
            if (unlikely(rs2 != 0)) {
                riscv_illegal_insn(vm, insn);
                return;
            }

            rm = fpu_set_rm(vm, rm);
            if (unlikely(rm == RM_INVALID)) {
                riscv_illegal_insn(vm, insn);
                return;
            }
            riscv_f_fsqrt(vm, rs1, rd);
            fpu_set_rm(vm, rm);
            break;
        case FT7_FSGN:
            switch (rm) {
                case 0:
                    riscv_f_fsgnj(vm, rs1, rs2, rd);
                    break;
                case 1:
                    riscv_f_fsgnjn(vm, rs1, rs2, rd);
                    break;
                case 2:
                    riscv_f_fsgnjx(vm, rs1, rs2, rd);
                    break;
                default:
                    riscv_illegal_insn(vm, insn);
                    return;
            }
            break;
        case FT7_FMINMAX:
            switch (rm) {
                case 0:
                    riscv_f_fmin(vm, rs1, rs2, rd);
                    break;
                case 1:
                    riscv_f_fmax(vm, rs1, rs2, rd);
                    break;
                default:
                    riscv_illegal_insn(vm, insn);
                    return;
            }
            break;
        case FT7_FCVT_W_S:
            rm = fpu_set_rm(vm, rm);
            if (unlikely(rm == RM_INVALID)) {
                riscv_illegal_insn(vm, insn);
                return;
            }

            if (unlikely(rs2 > 1)) {
                riscv_illegal_insn(vm, insn);
            } else {
                riscv_f_fcvt_w_s(vm, rs1, rd, rs2 == 1);
            }
            fpu_set_rm(vm, rm);
            break;
#ifndef RVD
        case FT7_FMV_X_W:
#else
        case FT7_FCLASS:
#endif
            if (unlikely(rs2 != 0)) {
                riscv_illegal_insn(vm, insn);
                return;
            }

            if (rm == 0) {
#ifndef RVD
                riscv_f_fmv_x_w(vm, rs1, rd);
#else
                riscv_illegal_insn(vm, insn);
                return;
#endif
            } else if (likely(rm == 1)) {
                riscv_f_fclass(vm, rs1, rd);
            } else {
                riscv_illegal_insn(vm, insn);
                return;
            }
            break;
        case FT7_FCMP:
            switch (rm) {
                case 0:
                    riscv_f_fle(vm, rs1, rs2, rd);
                    break;
                case 1:
                    riscv_f_flt(vm, rs1, rs2, rd);
                    break;
                case 2:
                    riscv_f_feq(vm, rs1, rs2, rd);
                    break;
                default:
                    riscv_illegal_insn(vm, insn);
                    return;
            }
            break;
        case FT7_FCVT_S_W:
            rm = fpu_set_rm(vm, rm);
            if (unlikely(rm == RM_INVALID)) {
                riscv_illegal_insn(vm, insn);
                return;
            }

            if (unlikely(rs2 > 1)) {
                riscv_illegal_insn(vm, insn);
            } else {
                riscv_f_fcvt_s_w(vm, rs1, rd, rs2 == 1);
            }
            fpu_set_rm(vm, rm);
            break;
#ifndef RVD
        case FT7_FMV_W_X:
            if (unlikely(rs2 != 0 || rm != 0)) {
                riscv_illegal_insn(vm, insn);
                return;
            }

            riscv_f_fmv_w_x(vm, rs1, rd);
            break;
        case FT7_FCVT_S_D:
            rm = fpu_set_rm(vm, rm);
            if (unlikely(rm == RM_INVALID)) {
                riscv_illegal_insn(vm, insn);
                return;
            }

            if (unlikely(rs2 != 1)) {
                riscv_illegal_insn(vm, insn);
                return;
            }
            riscv_f_fcvt_s_d(vm, rs1, rd);
            break;
#else
        case FT7_FCVT_D_S:
            rm = fpu_set_rm(vm, rm);
            if (unlikely(rm == RM_INVALID)) {
                riscv_illegal_insn(vm, insn);
                return;
            }

            if (unlikely(rs2 != 0)) {
                riscv_illegal_insn(vm, insn);
                return;
            }
            riscv_f_fcvt_d_s(vm, rs1, rd);
            fpu_set_rm(vm, rm);
            break;
#endif
        default:
            riscv_illegal_insn(vm, insn);
            return;
    }
}

#ifdef RVD
static void riscv_c_fld(rvvm_hart_t *vm, const uint16_t instruction)
{
    // Read double-precision floating point value from address rs1+offset to rds
    regid_t rds = riscv_c_reg(bit_cut(instruction, 2, 3));
    regid_t rs1 = riscv_c_reg(bit_cut(instruction, 7, 3));
    uint32_t offset = (bit_cut(instruction, 10, 3) << 3)
                    | (bit_cut(instruction, 5, 2)  << 6);

    xaddr_t addr = riscv_read_register(vm, rs1) + offset;
    uint8_t val[sizeof(double)];

    if (likely(riscv_mem_op(vm, addr, val, sizeof(val), MMU_READ))) {
        fpu_write_register64(vm, rds, read_double(val));
    }
}

static void riscv_c_fsd(rvvm_hart_t *vm, const uint16_t instruction)
{
    // Write double-precision floating point value rs2 to address rs1+offset
    regid_t rs2 = riscv_c_reg(bit_cut(instruction, 2, 3));
    regid_t rs1 = riscv_c_reg(bit_cut(instruction, 7, 3));
    uint32_t offset = (bit_cut(instruction, 10, 3) << 3)
                    | (bit_cut(instruction, 5, 2)  << 6);

    xaddr_t addr = riscv_read_register(vm, rs1) + offset;
    uint8_t val[sizeof(double)];
    write_double(val, fpu_read_register64(vm, rs2));
    riscv_mem_op(vm, addr, val, sizeof(val), MMU_WRITE);
}

static void riscv_c_fldsp(rvvm_hart_t *vm, const uint16_t instruction)
{
    // Read double-precision floating point value from address sp+offset to rds
    regid_t rds = bit_cut(instruction, 7, 5);
    uint32_t offset = (bit_cut(instruction, 5, 2)  << 3)
                    | (bit_cut(instruction, 12, 1) << 5)
                    | (bit_cut(instruction, 2, 3)  << 6);

    xaddr_t addr = riscv_read_register(vm, REGISTER_X2) + offset;
    uint8_t val[sizeof(double)];

    if (likely(riscv_mem_op(vm, addr, val, sizeof(val), MMU_READ))) {
        fpu_write_register64(vm, rds, read_double(val));
    }
}

static void riscv_c_fsdsp(rvvm_hart_t *vm, const uint16_t instruction)
{
    // Write double-precision floating point value rs2 to address sp+offset
    regid_t rs2 = bit_cut(instruction, 2, 5);
    uint32_t offset = (bit_cut(instruction, 10, 3) << 3)
                    | (bit_cut(instruction, 7, 3) << 6);

    xaddr_t addr = riscv_read_register(vm, REGISTER_X2) + offset;
    uint8_t val[sizeof(double)];
    write_double(val, fpu_read_register64(vm, rs2));
    riscv_mem_op(vm, addr, val, sizeof(val), MMU_WRITE);
}

static void riscv_d_init()
{
    riscv_install_opcode_ISB(RVD_FLW, riscv_f_flw);
    riscv_install_opcode_ISB(RVD_FSW, riscv_f_fsw);
    for (uint8_t i = 0; i < 8; ++i) {
        riscv_install_opcode_R(RVD_FMADD | (i << 5), riscv_f_fmadd);
        riscv_install_opcode_R(RVD_FMSUB | (i << 5), riscv_f_fmsub);
        riscv_install_opcode_R(RVD_FNMSUB | (i << 5), riscv_f_fnmsub);
        riscv_install_opcode_R(RVD_FNMADD | (i << 5), riscv_f_fnmadd);
        riscv_install_opcode_R(RVD_OTHER | (i << 5), riscv_f_other);
    }

    riscv_install_opcode_C(RVC_FLD, riscv_c_fld);
    riscv_install_opcode_C(RVC_FSD, riscv_c_fsd);
    riscv_install_opcode_C(RVC_FLDSP, riscv_c_fldsp);
    riscv_install_opcode_C(RVC_FSDSP, riscv_c_fsdsp);
}

void riscv_d_enable(bool enable)
{
    if (enable) {
        riscv_d_init();
        return;
    }

    riscv_install_opcode_ISB(RVD_FLW, riscv_illegal_insn);
    riscv_install_opcode_ISB(RVD_FSW, riscv_illegal_insn);
    for (uint8_t i = 0; i < 8; ++i) {
        riscv_install_opcode_R(RVD_FMADD | (i << 5), riscv_illegal_insn);
        riscv_install_opcode_R(RVD_FMSUB | (i << 5), riscv_illegal_insn);
        riscv_install_opcode_R(RVD_FNMSUB | (i << 5), riscv_illegal_insn);
        riscv_install_opcode_R(RVD_FNMADD | (i << 5), riscv_illegal_insn);
        riscv_install_opcode_R(RVD_OTHER | (i << 5), riscv_illegal_insn);
    }

    riscv_install_opcode_C(RVC_FLD, riscv_c_illegal_insn);
    riscv_install_opcode_C(RVC_FSD, riscv_c_illegal_insn);
    riscv_install_opcode_C(RVC_FLDSP, riscv_c_illegal_insn);
    riscv_install_opcode_C(RVC_FSDSP, riscv_c_illegal_insn);
}
#else
static void riscv_c_flw(rvvm_hart_t *vm, const uint16_t instruction)
{
    // Read single-precision floating point value from address rs1+offset to rds
    regid_t rds = riscv_c_reg(bit_cut(instruction, 2, 3));
    regid_t rs1 = riscv_c_reg(bit_cut(instruction, 7, 3));
    uint32_t offset = (bit_cut(instruction, 6, 1)  << 2)
                    | (bit_cut(instruction, 10, 3) << 3)
                    | (bit_cut(instruction, 5, 1)  << 6);

    xaddr_t addr = riscv_read_register(vm, rs1) + offset;
    uint8_t val[sizeof(float)];

    if (likely(riscv_mem_op(vm, addr, val, sizeof(val), MMU_READ))) {
        fpu_write_register32(vm, rds, read_float(val));
    }
}

static void riscv_c_fsw(rvvm_hart_t *vm, const uint16_t instruction)
{
    // Write single-precision floating point value rs2 to address rs1+offset
    regid_t rs2 = riscv_c_reg(bit_cut(instruction, 2, 3));
    regid_t rs1 = riscv_c_reg(bit_cut(instruction, 7, 3));
    uint32_t offset = (bit_cut(instruction, 6, 1)  << 2)
                    | (bit_cut(instruction, 10, 3) << 3)
                    | (bit_cut(instruction, 5, 1)  << 6);

    xaddr_t addr = riscv_read_register(vm, rs1) + offset;
    uint8_t val[sizeof(float)];
    write_float(val, fpu_read_register32(vm, rs2));
    riscv_mem_op(vm, addr, val, sizeof(val), MMU_WRITE);
}

static void riscv_c_flwsp(rvvm_hart_t *vm, const uint16_t instruction)
{
    // Read single-precision floating point value from address sp+offset to rds
    regid_t rds = bit_cut(instruction, 7, 5);
    uint32_t offset = (bit_cut(instruction, 4, 3)  << 2)
                    | (bit_cut(instruction, 12, 1) << 5)
                    | (bit_cut(instruction, 2, 2)  << 6);

    xaddr_t addr = riscv_read_register(vm, REGISTER_X2) + offset;
    uint8_t val[sizeof(float)];

    if (likely(riscv_mem_op(vm, addr, val, sizeof(val), MMU_READ))) {
        fpu_write_register32(vm, rds, read_float(val));
    }
}

static void riscv_c_fswsp(rvvm_hart_t *vm, const uint16_t instruction)
{
    // Write single-precision floating point value rs2 to address sp+offset
    regid_t rs2 = bit_cut(instruction, 2, 5);
    uint32_t offset = (bit_cut(instruction, 9, 4) << 2)
                    | (bit_cut(instruction, 7, 2) << 6);

    xaddr_t addr = riscv_read_register(vm, REGISTER_X2) + offset;
    uint8_t val[sizeof(float)];
    write_float(val, fpu_read_register32(vm, rs2));
    riscv_mem_op(vm, addr, val, sizeof(val), MMU_WRITE);
}

static void riscv_f_init()
{
    riscv_install_opcode_ISB(RVF_FLW, riscv_f_flw);
    riscv_install_opcode_ISB(RVF_FSW, riscv_f_fsw);
    for (uint8_t i = 0; i < 8; ++i) {
        riscv_install_opcode_R(RVF_FMADD | (i << 5), riscv_f_fmadd);
        riscv_install_opcode_R(RVF_FMSUB | (i << 5), riscv_f_fmsub);
        riscv_install_opcode_R(RVF_FNMSUB | (i << 5), riscv_f_fnmsub);
        riscv_install_opcode_R(RVF_FNMADD | (i << 5), riscv_f_fnmadd);
        riscv_install_opcode_R(RVF_OTHER | (i << 5), riscv_f_other);
    }

#ifndef RV64
    riscv_install_opcode_C(RVC_FLW, riscv_c_flw);
    riscv_install_opcode_C(RVC_FSW, riscv_c_fsw);
    riscv_install_opcode_C(RVC_FLWSP, riscv_c_flwsp);
    riscv_install_opcode_C(RVC_FSWSP, riscv_c_fswsp);
#endif
}

void riscv_f_enable(bool enable)
{
    if (enable) {
        riscv_f_init();
        return;
    }

    riscv_install_opcode_ISB(RVF_FLW, riscv_illegal_insn);
    riscv_install_opcode_ISB(RVF_FSW, riscv_illegal_insn);
    for (uint8_t i = 0; i < 8; ++i) {
        riscv_install_opcode_R(RVF_FMADD | (i << 5), riscv_illegal_insn);
        riscv_install_opcode_R(RVF_FMSUB | (i << 5), riscv_illegal_insn);
        riscv_install_opcode_R(RVF_FNMSUB | (i << 5), riscv_illegal_insn);
        riscv_install_opcode_R(RVF_FNMADD | (i << 5), riscv_illegal_insn);
        riscv_install_opcode_R(RVF_OTHER | (i << 5), riscv_illegal_insn);
    }

#ifndef RV64
    riscv_install_opcode_C(RVC_FLW, riscv_c_illegal_insn);
    riscv_install_opcode_C(RVC_FSW, riscv_c_illegal_insn);
    riscv_install_opcode_C(RVC_FLWSP, riscv_c_illegal_insn);
    riscv_install_opcode_C(RVC_FSWSP, riscv_c_illegal_insn);
#endif
}
#endif
