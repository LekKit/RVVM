#ifndef ETH_OC_H
#define ETH_OC_H

#include "riscv32.h"
#include "rvvm_types.h"

#ifdef USE_NET
void ethoc_init(riscv32_vm_state_t *vm, const char *tap_name, paddr_t regs_base_addr, void *intc_data, uint32_t irq);
#else
static inline void ethoc_init(riscv32_vm_state_t *vm, const char *tap_name, paddr_t regs_base_addr, void *intc_data, uint32_t irq)
{
    UNUSED(vm);
    UNUSED(tap_name);
    UNUSED(regs_base_addr);
    UNUSED(intc_data);
    UNUSED(irq);
}
#endif

#endif
