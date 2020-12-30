#pragma once

#include "riscv.h"

enum
{
    REGISTER_ZERO,
    REGISTER_X0 = REGISTER_ZERO,
    REGISTER_X1,
    REGISTER_X2,
    REGISTER_X3,
    REGISTER_X4,
    REGISTER_X5,
    REGISTER_X6,
    REGISTER_X7,
    REGISTER_X8,
    REGISTER_X9,
    REGISTER_X10,
    REGISTER_X11,
    REGISTER_X12,
    REGISTER_X13,
    REGISTER_X14,
    REGISTER_X15,
    REGISTER_X16,
    REGISTER_X17,
    REGISTER_X18,
    REGISTER_X19,
    REGISTER_X20,
    REGISTER_X21,
    REGISTER_X22,
    REGISTER_X23,
    REGISTER_X24,
    REGISTER_X25,
    REGISTER_X26,
    REGISTER_X27,
    REGISTER_X28,
    REGISTER_X29,
    REGISTER_X30,
    REGISTER_X31,
    REGISTER_PC,
    REGISTERS_MAX
};

typedef struct risc32_vm_state_s risc32_vm_state_t;

typedef struct {
    uint32_t start;
    uint32_t end;
    const char *name;
    void (*function)(risc32_vm_state_t *vm, uint32_t operation, uint32_t addr, uint32_t data);
} memory_map_t;

struct risc32_vm_state_s
{
    uint32_t cpu_instruction_flags;
    uint32_t cpu_flags;
    jmp_buf jump_buff;
    uint8_t *code;
    uint32_t code_len;
    uint32_t code_pointer;
    uint32_t registers[REGISTERS_MAX];
    memory_map_t *memory_map;
};

#define RISCV32I_OPCODE_MASK 0x3

#define RISCV_HAVE_16BIT_OPCODES (1u << 0) // allow execute riscv16 instructions?
#define RISCV_ALIGN_32 4 // 4 byte align
#define RISCV_ALIGN_16 2 // 2 byte align
#define RISCV_ILEN 4 // 4 byte opcode len

#define RISCV32_BIG_ENDIAN (1u << 0)
#define RISCV32_LITTLE_ENDIAN (1u << 1)

enum
{
    RISCV32_TRAP_CONTAINED, // The trap is visible to, and handled by, software running inside the executionenvironment.
    RISCV32_TRAP_REQUESTED, // The trap is a synchronous exception that is an explicit call to the executionenvironment requesting an action on behalf of software inside the execution environment.
    RISCV32_TRAP_INVISIBLE, // The trap is handled transparently by the execution environment and executionresumes normally after the trap is handled.
    RISCV32_TRAP_FATAL // The trap represents a fatal failure and causes the execution environment to terminateexecution.
};

#define RISCV32_HAVE_NONSTANDART_EXT (1u << 0) // mark cpu with custom opcodes to enable hacks
#define RISCV32I (1u << 1) // base and minimal
#define RISCV32_HAVE_C (1u << 2) // base compressed extension

#define RISCV32_UNPRIVILAGED_REG_COUNT 32 // x0 - x31 + pc

risc32_vm_state_t *riscv32_create_vm();
void riscv32_run(risc32_vm_state_t *vm);
void riscv32_destroy_vm(risc32_vm_state_t *vm);
void riscv32c_init();
