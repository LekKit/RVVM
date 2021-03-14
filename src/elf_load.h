#pragma once

#include "riscv32.h"
#include <elf.h>
#include <stddef.h>

bool riscv32_elf_load_by_path(riscv32_vm_state_t *vm, const char *path, bool use_mmu, ptrdiff_t offset);

