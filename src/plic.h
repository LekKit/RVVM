#include "riscv32.h"

void* plic_init(riscv32_vm_state_t *vm, uint32_t base_addr);
bool plic_send_irq(riscv32_vm_state_t *vm, void *data, uint32_t id);
