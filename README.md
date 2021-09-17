
# RVVM - The RISC-V Virtual Machine
[![Language grade: C/C++](https://img.shields.io/lgtm/grade/cpp/g/LekKit/RVVM.svg?logo=lgtm&logoWidth=18)](https://lgtm.com/projects/g/LekKit/RVVM/context:cpp)
![version](https://img.shields.io/badge/version-0.4-brightgreen)
![RISC-V Logo](https://riscv.org/wp-content/uploads/2018/09/riscv-logo-1.png "The “RISC-V” trade name is a registered trade mark of RISC-V International.")

RISC-V CPU & System software implementation written in С

## What's working
- Passes RISC-V compliance tests for both RV64 & RV32
- OpenSBI, U-Boot, custom firmwares boot and execute properly
- Linux kernel boots!
- Linux userspace works, interactive shell through UART
- Framebuffer graphics, working Xorg with mouse & keyboard
- Raw image mounted as rootfs

## What's done so far
- Feature-complete RV64IMAFDC instruction set
- Multicore support (SMP)
- Bootrom, kernel Image loading
- DTB auto-generation, passing to firmware/kernel
- RVVM Lib API for VM integration
- MMU with SV32/SV39/SV48/SV57 virtual addressing
- UART 16550a-compatible text console
- PLIC/CLIC, timers
- PS2 Altera Controller, PS2 keyboard & mouse
- Graphical framebuffer through X11/WinAPI
- Generic PCI Bus
- ATA PIO hard drive
- ATA BMDMA (IDE UDMA) over PCI
- OpenCores Ethernet through Linux TAP
- Poweroff/reset device

## Building
Currently builds using GNU Make or CMake and tested on Linux, Windows and MacOS systems. More build targets are going to be supported.
```
git clone https://github.com/LekKit/RVVM
cd RVVM
make
cd release.linux.x86_64
```
To cross-compile, pass CC=target-gcc and OS=target-os if the target OS differs from your host to make. You can configure the build with use flags

Examples:
```
make CC=x86_64-w64-mingw32-gcc OS=windows USE_JIT=1
make CC=aarch64-linux-gnu-gcc OS=linux USE_FB=0 USE_NET=1 USE_JIT=0
```
Alternatively, you can use CMake (hint, build binaries are not host arch suffixed):
```
git clone https://github.com/LekKit/RVVM
cd RVVM
mkdir build
cmake -S. -Bbuild
cmake --build build --target all
cd build
```

## Running
```
./rvvm_x86_64 fw_jump.bin -kernel linux_Image -image rootfs.img -rv64 -mem 2G -smp 2 -res 1280x720
```
Argument explanation:
```
fw_jump.bin            Initial firmware, OpenSBI in this case
-kernel linux_Image    Kernel Image to be booted by SBI, etc
-image rootfs.img      Hard drive image to be mounted as rootfs
-rv64                  Enable RV64, RV32 is used by default
-mem 2G                Memory amount (may be suffixed by k/M/G), default 256M
-smp 2                 Amount of cores, single-core machine by default
-res 1280x720          Changes framebuffer & VM window resolution
```
Invoke "./rvvm_x86_64 -h" to see extended help.

## Our team
- **LekKit**: RVVM API, interpreter, RV64IMAC ISA, RISC-V Hart/MMU/Privileged/CSR, codebase infrastructure, working on JIT
- **cerg2010cerg2010**: Important fixes, initial RV64 work, PLIC/PS2/ATA/Ethernet, XCB window backend, FPU extensions, FDT lib, working on PCI
- **Mr0maks**: Initial ideas, C/M extensions, VM debugger, CSR work, NS16550A UART
- *Hoping to see more contributors here*

## TODO
- Test FreeBSD, HaikuOS
  (FreeBSD generic kernel boots but i have no idea how to mount rootfs,
  too lazy to look at Haiku)
- Improve RVVM Lib public API
- Working JIT infrastructure (many headaches from x86 backend)
- SATA/SCSI
- Sparse HDD image format, compression
- Userspace networking, sound?
- Maybe virtio devices
- Other peripherals
- *A lot more...*
- Userspace emulation?
