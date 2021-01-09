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
    bool error;
    char error_string[4096];
    uint8_t *code;
    uint32_t code_len;
    uint32_t registers[REGISTERS_MAX];
    memory_map_t *memory_map;
};

#define RISCV32I_OPCODE_MASK 0x3

#define RISCV_ALIGN_32 4 // 4 byte align
#define RISCV_ALIGN_16 2 // 2 byte align
#define RISCV_ILEN 4 // 4 byte opcode len

#define RISCV32_LITTLE_ENDIAN (1u << 0)
#define RISCV32_IIS_I (1u << 1) // base and minimal ISA with 32 registers
#define RISCV32_IIS_E (1u << 2) // base and minimal ISA with 16 registers

#define RISCV32_HAVE_NONSTANDART_EXTENSION (1u << 0) // mark cpu with custom opcodes to enable hacks
#define RISCV32_HAVE_M_EXTENSION (1u << 1) // multiplication and division for intergers
#define RISCV32_HAVE_C_EXTENSION (1u << 2) // compressed instructions extension

/*
* Concatenate func3[14:12] and opcode[6:0] into 10-bit id to simplify decoding.
* This won't work for U-type and J-type instructions since there's no func3,
* so we will simply smudge function pointers for those all over the jumptable.
*/
#define RISCV32_GET_FUNCID(x) (((x >> 5) & 0x380) | (x & 0x7F))

extern void (*riscv32_opcodes[1024])(risc32_vm_state_t *vm, uint32_t instruction);

// This is the trick mentioned earlier, to decode U/J-type operations properly
void smudge_opcode_func3(uint32_t opcode, void (*func)(risc32_vm_state_t*, uint32_t));

risc32_vm_state_t *riscv32_create_vm();
void riscv32_run(risc32_vm_state_t *vm);
void riscv32_destroy_vm(risc32_vm_state_t *vm);
void riscv32_dump_registers(risc32_vm_state_t *vm);
void riscv32_error(risc32_vm_state_t *vm, const char *fmt, ...) __attribute__ ((format (printf, 2, 3)));
void riscv32_illegal_insn(risc32_vm_state_t *vm, uint32_t instruction);
void riscv32m_init();
void riscv32c_init();
void riscv32i_init();
