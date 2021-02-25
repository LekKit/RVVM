/*
riscv32.c - Very stupid and slow RISC-V emulator code
Copyright (C) 2021  Mr0maks <mr.maks0443@gmail.com>
                    LekKit <github.com/LekKit>

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

#include <stdarg.h>
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <err.h>

#include "riscv.h"
#include "riscv32.h"
#include "riscv32_mmu.h"
#include "riscv32_csr.h"
#include "riscv32i.h"
#include "riscv32c.h"
#include "mem_ops.h"

void (*riscv32_opcodes[512])(riscv32_vm_state_t *vm, const uint32_t instruction);

// This should redirect the VM to the trap handlers when they are implemented
void riscv32c_illegal_insn(riscv32_vm_state_t *vm, const uint16_t instruction)
{
    riscv32_debug_always(vm, "RV32C: illegal instruction %h", instruction);
    riscv32_trap(vm, TRAP_ILL_INSTR, instruction);
}

void riscv32_illegal_insn(riscv32_vm_state_t *vm, const uint32_t instruction)
{
    riscv32_debug_always(vm, "RV32I: illegal instruction %h", instruction);
    riscv32_trap(vm, TRAP_ILL_INSTR, instruction);
}

void smudge_opcode_UJ(uint32_t opcode, void (*func)(riscv32_vm_state_t*, const uint32_t))
{
    for (uint32_t f3=0; f3<0x10; ++f3)
        riscv32_opcodes[opcode | (f3 << 5)] = func;
}

void smudge_opcode_ISB(uint32_t opcode, void (*func)(riscv32_vm_state_t*, const uint32_t))
{
    riscv32_opcodes[opcode] = func;
    riscv32_opcodes[opcode | 0x100] = func;
}

static bool mmio_usart_handler(struct riscv32_vm_state_t* vm, riscv32_mmio_device_t* device, uint32_t addr, void* dest, uint32_t size, uint8_t access)
{
    UNUSED(vm);
    UNUSED(device);
    UNUSED(addr);
    UNUSED(size);
    UNUSED(access);
#ifdef RV_DEBUG
    printf("USART: %c\n", *(char*)dest);
#else
    printf("%c", *(char*)dest);
#endif
    return true;
}

riscv32_vm_state_t *riscv32_create_vm()
{
    static bool global_init = false;
    if (!global_init) {
        for (uint32_t i=0; i<512; ++i) riscv32_opcodes[i] = riscv32_illegal_insn;
        riscv32i_init();
        riscv32m_init();
        riscv32c_init();
        riscv32a_init();
        riscv32_priv_init();
        global_init = true;
    }

    riscv32_vm_state_t *vm = (riscv32_vm_state_t*)malloc(sizeof(riscv32_vm_state_t));
    memset(vm, 0, sizeof(riscv32_vm_state_t));

    // 0x10000 pages = 256M
    if (!riscv32_init_phys_mem(&vm->mem, 0x80000000, 0x10000)) {
        printf("Failed to allocate VM physical RAM!\n");
        free(vm);
        return NULL;
    }
    riscv32_tlb_flush(vm);
    riscv32_mmio_add_device(vm, 0x10000000, 0x100000FF, mmio_usart_handler, NULL);
    vm->mmu_virtual = false;
    vm->priv_mode = PRIVILEGE_MACHINE;
    vm->registers[REGISTER_PC] = vm->mem.begin;

    for (uint32_t i=0; i<4096; ++i)
        riscv32_csr_init(vm, i, "illegal", 0, riscv32_csr_illegal);
    riscv32_csr_m_init(vm);
    riscv32_csr_s_init(vm);
    riscv32_csr_u_init(vm);

    return vm;
}

void riscv32_destroy_vm(riscv32_vm_state_t *vm)
{
    riscv32_destroy_phys_mem(&vm->mem);
    free(vm);
}

void riscv32_interrupt(riscv32_vm_state_t *vm, uint32_t cause)
{
    vm->mcause = cause | INTERRUPT_MASK;
    vm->wait_event = 0;
}

void riscv32_trap(riscv32_vm_state_t *vm, uint32_t cause, uint32_t tval)
{
    /*
    * This should set the PC to trap vector, save current execution context
    * when we have CSRs, etc
    * Currently works the same as interrupt
    */
    UNUSED(tval);
    vm->mcause = cause & (~INTERRUPT_MASK);
    vm->mtval = tval;
    vm->wait_event = 0;
}

void riscv32_debug_always(const riscv32_vm_state_t *vm, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    char buffer[256];
    uint32_t size = sprintf(buffer, "[VM %p] ", vm);
    uint32_t begin = 0;
    uint32_t len = strlen(fmt);
    uint32_t i;
    for (i=0; i<len; ++i) {
        if (fmt[i] == '%') {
            memcpy(buffer+size, fmt+begin, i-begin);
            size += i-begin;
            begin = i+2;
            i++;
            switch (fmt[i]) {
            case 'r':
                size += sprintf(buffer+size, "%s", riscv32i_translate_register(va_arg(ap, uint32_t)));
                break;
            case 'd':
                size += sprintf(buffer+size, "%d", va_arg(ap, int32_t));
                break;
            case 'h':
                size += sprintf(buffer+size, "0x%x", va_arg(ap, uint32_t));
                break;
            case 'c':
                size += sprintf(buffer+size, "%s", vm->csr[va_arg(ap, uint32_t)].name);
                break;
            }
        }
    }
    memcpy(buffer+size, fmt+begin, i-begin);
    size += i-begin;
    buffer[size] = 0;
    printf("%s\n", buffer);
    va_end(ap);
}

void riscv32_dump_registers(riscv32_vm_state_t *vm)
{
    for ( int i = 0; i < REGISTERS_MAX - 1; i++ ) {
        printf("%-5s: 0x%08X  ", riscv32i_translate_register(i), riscv32i_read_register_u(vm, i));

        if (((i + 1) % 4) == 0)
            printf("\n");
    }
    printf("%-5s: 0x%08X\n", riscv32i_translate_register(32), riscv32i_read_register_u(vm, 32));
}

inline void riscv32_exec_instruction(riscv32_vm_state_t *vm, uint32_t instruction)
{
    if ((instruction & RISCV32I_OPCODE_MASK) != RISCV32I_OPCODE_MASK) {
        // 16-bit opcode
        riscv32c_emulate(vm, instruction);
        // FYI: Any jump instruction implementation should take care of PC increment
        vm->registers[REGISTER_PC] += 2;
    } else {
        riscv32i_emulate(vm, instruction);
        vm->registers[REGISTER_PC] += 4;
    }

#ifdef RV_DEBUG
    riscv32_dump_registers(vm);
#ifdef RV_DEBUG_SINGLESTEP
    getchar();
#endif
#endif
}

static void riscv32_run_till_event(riscv32_vm_state_t *vm)
{
    uint8_t instruction[4];
    uint32_t tlb_key, inst_addr;
    // Execute hot instructions loop until some event occurs (interrupt, etc)
    // This adds little to no overhead, and the loop can be forcefully unrolled
    while (vm->wait_event) {
        // Copying the riscv32_mem_op code here increases performance
        inst_addr = vm->registers[REGISTER_PC];
        tlb_key = tlb_hash(inst_addr);
        if (tlb_check(vm->tlb[tlb_key], inst_addr, MMU_EXEC) && block_inside_page(inst_addr, 4)) {
            memcpy(instruction, vm->tlb[tlb_key].ptr + (inst_addr & 0xFFF), 4);
            riscv32_exec_instruction(vm, read_uint32_le(instruction));
        } else {
            if (riscv32_mmu_op(vm, inst_addr, instruction, 4, MMU_EXEC))
                riscv32_exec_instruction(vm, read_uint32_le(instruction));
        }
    }
}

void riscv32_run(riscv32_vm_state_t *vm)
{
    assert(vm);

    vm->wait_event = 1;
    riscv32_run_till_event(vm);
    if (vm->mcause & INTERRUPT_MASK)
        riscv32_debug_always(vm, "Interrupted the VM, mcause: %h", vm->mcause);
    else
        riscv32_debug_always(vm, "Trapped the VM, mcause: %h, mtval: %h", vm->mcause, vm->mtval);
    riscv32_dump_registers(vm);
}
