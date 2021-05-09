#ifndef ATA_H
#define ATA_H

#include "riscv32.h"
#include "rvvm_types.h"
void ata_init(riscv32_vm_state_t *vm, paddr_t data_base_addr, paddr_t ctl_base_addr, FILE *fp0, FILE *fp1);

#endif
