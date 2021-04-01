/*
riscv_cpu.c - RISC-V CPU Emulation
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

#include "mem_ops.h"
#include "riscv32_mmu.h"
#include "riscv_cpu.h"

static inline uint32_t riscv_funcid(const uint32_t instr)
{
    return (((instr >> 17) & 0x100) | ((instr >> 7) & 0xE0) | ((instr >> 2) & 0x1F));
}

static inline uint32_t riscv_c_funcid(const uint16_t instr)
{
    return (((instr >> 13) << 2) | (instr & 3));
}

#define RV_OPCODE_MASK 0x3

/*
 * We build the entire interpreter twice for RV32/64, allow disabling RV64 at compile time.
 * It removes unnecessary runtime checks in actual instruction implementations,
 * makes code cleaner. Eventually, this may allow to implement RV128 easily.
 * This is very similar to C++ templates, lol
 */

static void (*riscv_opcodes[512])(rvvm_hart_state_t *vm, const uint32_t instruction);
static void (*riscv_c_opcodes[32])(rvvm_hart_state_t *vm, const uint16_t instruction);


// Sanity check that our installed instructions do not overlap
static void check_opcode(uint32_t opcode)
{
    if (riscv_opcodes[opcode] != riscv_illegal_insn) {
        printf("ERROR: RV opcode %x overlaps at CPU init!\n", opcode);
        exit(-1);
    }
}

static void check_opcode_c(uint32_t opcode)
{
    if (riscv_c_opcodes[opcode] != riscv_c_illegal_insn) {
        printf("ERROR: RVC opcode %x overlaps at CPU init!\n", opcode);
        exit(-1);
    }
}

// Install instruction implementations to the jumptable
void riscv_install_opcode_R(uint32_t opcode, void (*func)(rvvm_hart_state_t*, const uint32_t))
{
    check_opcode(opcode);
    riscv_opcodes[opcode] = func;
}

void riscv_install_opcode_UJ(uint32_t opcode, void (*func)(rvvm_hart_state_t*, const uint32_t))
{
    for (uint32_t f3=0; f3<0x10; ++f3) {
        check_opcode(opcode | (f3 << 5));
        riscv_opcodes[opcode | (f3 << 5)] = func;
    }
}

void riscv_install_opcode_ISB(uint32_t opcode, void (*func)(rvvm_hart_state_t*, const uint32_t))
{
    check_opcode(opcode);
    check_opcode(opcode | 0x100);
    riscv_opcodes[opcode] = func;
    riscv_opcodes[opcode | 0x100] = func;
}

void riscv_install_opcode_C(uint32_t opcode, void (*func)(rvvm_hart_state_t*, const uint16_t))
{
    check_opcode_c(opcode);
    riscv_c_opcodes[opcode] = func;
}

static inline void riscv_emulate(rvvm_hart_state_t *vm, uint32_t instruction)
{
    if ((instruction & RV_OPCODE_MASK) != RV_OPCODE_MASK) {
        // 16-bit opcode
        riscv_c_opcodes[riscv_c_funcid(instruction)](vm, instruction);
        // FYI: Any jump instruction implementation should take care of PC increment
        vm->registers[REGISTER_PC] += 2;
    } else {
        riscv_opcodes[riscv_funcid(instruction)](vm, instruction);
        vm->registers[REGISTER_PC] += 4;
    }
}

void riscv_cpu_init()
{
    for (size_t i=0; i<512; ++i)
        riscv_opcodes[i] = riscv_illegal_insn;
    for (size_t i=0; i<32; ++i)
        riscv_c_opcodes[i] = riscv_c_illegal_insn;
    riscv_i_init();
    riscv_c_init();
    riscv_m_init();
    riscv_a_init();
}

void riscv_run_till_event(rvvm_hart_state_t *vm)
{
    uint8_t instruction[4];
    xlen_t tlb_key, inst_addr;
    // Execute instructions loop until some event occurs (interrupt, trap)
    while (vm->wait_event) {
        vm->registers[REGISTER_ZERO] = 0;
        inst_addr = vm->registers[REGISTER_PC];
        tlb_key = tlb_hash(inst_addr);
        if (tlb_check(vm->tlb[tlb_key], inst_addr, MMU_EXEC) && block_inside_page(inst_addr, 4)) {
            riscv_emulate(vm, read_uint32_le(vm->tlb[tlb_key].ptr + (inst_addr & 0xFFF)));
        } else {
            if (riscv_mmu_op(vm, inst_addr, instruction, 4, MMU_EXEC)) {
                riscv_emulate(vm, read_uint32_le(instruction));
            }
        }
    }
}
