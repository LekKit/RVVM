#pragma once

#include "riscv.h"
#include "riscv32.h"

#define RISCV32C_VERSION 20 // 2.0

void riscv32m_emulate(risc32_vm_state_t *vm, const uint32_t instruction);
