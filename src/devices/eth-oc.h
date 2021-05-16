#ifndef ETH_OC_H
#define ETH_OC_H

#include "riscv32.h"
#include "rvvm_types.h"

void ethoc_init(riscv32_vm_state_t *vm, const char *tap_name, paddr_t regs_base_addr, void *intc_data, uint32_t irq);

#endif
