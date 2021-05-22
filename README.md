
# RVVM - The RISC-V Virtual Machine
[![Language grade: C/C++](https://img.shields.io/lgtm/grade/cpp/g/LekKit/RVVM.svg?logo=lgtm&logoWidth=18)](https://lgtm.com/projects/g/LekKit/RVVM/context:cpp)
![RISC-V Logo](https://riscv.org/wp-content/uploads/2018/09/riscv-logo-1.png "The “RISC-V” trade name is a registered trade mark of RISC-V International.")


RISC-V CPU & System software implementation written in С

## What's working
- OpenSBI, custom firmwares boot and execute properly
- Linux kernel boots!
- Linux userspace works, interactive shell through UART
- Framebuffer graphics, working Xorg with mouse & keyboard
- Raw image mounted as rootfs

## What's done so far
- Feature-complete RV32I instruction set
- C, M, A instruction extensions
- Extendable and fast instruction decoder
- Physical memory
- Memory mapping unit (MMU) with SV32 virtual addressing
- TLB address caching (greatly speeds up memory operations)
- MMIO handlers
- CSR operations
- UART 16550a-compatible text console
- Bootrom loading
- DTB loading, passing to firmware/kernel
- ELF kernel loading
- Interrupts
- PLIC/CLIC, timers
- PS2 Altera Controller, PS2 keyboard & mouse
- Graphical framebuffer
- ATA PIO hard drive
- OpenCores Ethernet
- [Somewhat WIP] Flash, RV64 CPU, JIT prototype

## Usage
Currently builds using GNU Make and tested on Linux, Windows and MacOS systems. More build targets are going to be supported.
```
git clone https://github.com/LekKit/RVVM
cd RVVM
make
cd release.linux.x86_64
```
To cross-compile, pass CC=target-gcc and OS=target-os if the target OS differs from your host to make. You can configure the build with use flags

Examples:
```
make CC=x86_64-w64-mingw32-gcc OS=windows
make CC=aarch64-linux-gnu-gcc OS=linux USE_FB=0 USE_NET=1
```

Running:
```
./rvvm_x86_64 bootrom.bin -dtb=device.dtb -image=rootfs.img
```
The bootrom.bin file is a user-provided raw binary, loaded at 0x80000000 address where it starts execution, and device.dtb is a DTB file containing description of the machine.
You can pass -image=rootfs.img to mount a raw partition image as a flash drive.


## Our team
- **LekKit**:  Instruction decoding, RAM/MMU/TLB implementation, RV32/64ICMA ISA, interrupts & timer, privileged ISA, JIT, lots of fixes
- **Mr0maks**: Initial ideas, C/M extensions, VM debugger, CSR work, NS16550A UART
- **cerg2010cerg2010**: ELF loading, important fixes, initial RV64 work, PLIC, PS2, ATA, Ethernet, XCB window backend
- *Hoping to see more contributors here*

## TODO
- Debug the available functionality and make sure it's conforming to the specs
- Proper MMU atomicity, fence, native AMO
- Multicore support (already works but the kernel races and crashes sometimes)
- Improve MMU & TLB, allow their usage from JIT'ed code
- Floating-point extensions
- RV64-only instructions & MMU
- Integrate JIT into the VM
- Networking, sound?
- Other peripherals
- DTB generation
- *A lot more...*
- Userspace emulation?
