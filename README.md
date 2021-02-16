
# RVVM - The RISC-V Virtual Machine

![RISC-V Logo](https://riscv.org/wp-content/uploads/2018/09/riscv-logo-1.png "The “RISC-V” trade name is a registered trade mark of RISC-V International.")


RISC-V CPU & System software implementation written in С

## What's done so far
- Feature-complete RV32I instruction set
- C, M instruction extensions
- Extendable and fast instruction decoder
- Physical memory
- Memory mapping unit (MMU) with SV32 virtual addressing
- TLB address caching (greatly speeds up memory operations)
- MMIO handlers
- Bootrom loading

## Usage
Currently builds on *nix systems using GNU Make. Actual code however is cross-platform and more build targets are going to be supported, including Windows, or even embedded systems.
The bootrom.bin file is a user-provided raw binary, loaded at 0x80000000 address where it starts execution.
```
git clone https://github.com/LekKit/RVVM
cd RVVM
make
cd release.linux.x86_64
./rvvm_x86_64 bootrom.bin
```

## Our team
- **LekKit**:  Instruction decoding, RAM/MMU/TLB implementation, parts of RV32I/C ISA
- **Mr0maks**: Initial ideas, C/M extensions, VM debugger, bootrom testing
- *Hoping to see more contributors here*

## TODO
- Debug the available functionality to make sure it's conforming to the specs
- Control and Status Registers (CSR)
- Privileged modes, operations
- Basic peripherals atleast
- Supervisor Binary Interface
- Successfully booting Linux kernel and userspace software
- *A lot more...*
- JIT? RV64?
