# RVVM - The RISC-V Virtual Machine
[![version](https://img.shields.io/badge/version-0.6--git-brightgreen?style=for-the-badge)](#-installing) [![](https://img.shields.io/tokei/lines/github.com/LekKit/RVVM?style=for-the-badge)](https://github.com/LekKit/RVVM/graphs/contributors) [![Build](https://img.shields.io/github/actions/workflow/status/LekKit/RVVM/build.yml?branch=staging&style=for-the-badge)](https://github.com/LekKit/RVVM/actions/workflows/build.yml) [![Codacy grade](https://img.shields.io/codacy/grade/c77cc7499a784cd293fde58641ce3e46?logo=codacy&style=for-the-badge)](https://app.codacy.com/gh/LekKit/RVVM/dashboard)
![RISC-V Logo](https://riscv.org/wp-content/uploads/2018/09/riscv-logo-1.png "The “RISC-V” trade name is a registered trade mark of RISC-V International.")

RISC-V CPU & System software implementation written in С

## What's working
[![Demo](https://img.shields.io/badge/Check%20it%20out-WASM%20Demo-red?style=for-the-badge)](https://lekkit.github.io/test/index.html)
- Passes RISC-V compliance/torture tests for both RV64 & RV32
- OpenSBI, U-Boot, custom firmwares boot and execute properly
- Working Linux, FreeBSD, OpenBSD, Haiku OS & other cool OSes
- Tracing JIT, multicore support
- Framebuffer display, mouse & keyboard, UART shell
- NVMe storage drives
- Networking (WIP)

## 💡 Tell me more...
- Feature-complete RV64IMAFDC instruction set
- Multicore support (SMP), SV32/SV39/SV48/SV57 MMU
- Tracing RVJIT with x86_64, ARM64, RISC-V, i386, ARM backends
  (faster than QEMU, yay!)
- Bootrom, Kernel Image loading, ELF support
- Device Tree auto-generation, passing to firmware/kernel
- RVVM Public API for VM integration
- UART 16550a-compatible text console
- PLIC/ACLINT, Timers, Poweroff/reset
- Generic PCI Express Bus
- NVMe storage, image TRIM support, fast IO
- Framebuffer display, GUI for X11/WinAPI/Haiku/SDL
- I2C HID keyboard & mouse, OpenCores I2C controller
- Realtek RTL8169, OpenCores Ethernet NICs
- Linux TAP / WIP socket-based networking
- Deprecated: PS2 keyboard & mouse, ATA (IDE) drive

## 📦 Installing
[![Artifacts](https://img.shields.io/badge/BIN-Artifacts-orange?style=for-the-badge)](https://nightly.link/LekKit/RVVM/workflows/build/staging) [![AUR](https://img.shields.io/badge/Arch%20Linux-AUR-blue?style=for-the-badge&logo=archlinux)](https://aur.archlinux.org/packages/rvvm-git) [![Build](https://img.shields.io/badge/Build-Make-red?style=for-the-badge)](#-building)

## 🛠 Building
Currently builds using GNU Make (recommended) or CMake and is extremely portable.
```
git clone https://github.com/LekKit/RVVM
cd RVVM
make
cd release.linux.x86_64
./rvvm_x86_64 -h
```
You can configure the build with USE flags. To cross-compile, pass CC=target-gcc to make. If it fails to detect features, pass ARCH/OS variables explicitly.

Examples:
```
make lib CC=aarch64-linux-android21-clang USE_FB=0
make lib all CC=x86_64-w64-mingw32-gcc USE_NET=1
make CFLAGS=-m32 ARCH=i386 USE_RV64=0 BUILDDIR=build BINARY=rvvm
make CC=mipseb-linux-gnu-gcc USE_JIT=0
```
Alternatively, you can use CMake:
```
git clone https://github.com/LekKit/RVVM
cd RVVM
mkdir build
cmake -S. -Bbuild
cmake --build build --target all
cd build
./rvvm -h
```

## 🚀 Running
```
./rvvm fw_jump.bin -k u-boot.bin -i drive.img -m 2G -smp 2 -res 1280x720
```
Argument explanation:
```
[fw_jump.bin]          Initial M-mode firmware, OpenSBI in this case
-k, -kernel u-boot.bin S-mode kernel payload (Linux Image, U-Boot, etc)
-i, -image drive.img   Attach NVMe storage image (Raw format as of now)
-m, -mem 2G            Memory amount (may be suffixed by k/M/G), default 256M
-s, -smp 2             Amount of cores, single-core machine by default
-res 1280x720          Changes framebuffer & VM window resolution
-jitcache 64M          Raise JIT cache limit (64M recommended for complex guests)
 . . .
-rv32                  Enable 32-bit RISC-V, 64-bit by default
-cmdline, -append ...  Override/append default kernel command line
-nogui, -nojit         Disable GUI (Use only UART), Disable JIT (for debugging)
```
Invoke "./rvvm -h" to see extended help.

## Tested environments (Likely works elsewhere as well)
| OS         | JIT                        | GUI          |
|------------|----------------------------|--------------|
| Linux      | Works                      | X11, SDL     |
| Windows    | Works                      | Win32, SDL   |
| MacOS X    | Works, need signing for M1 | SDL, XQuartz |
| FreeBSD    | Works                      | X11, SDL     |
| Haiku OS   | Works                      | Haiku, SDL   |
| Serenity   | Works                      | SDL          |
| Windows CE | Broken (?)                 | Win32        |
| Emscripten | -                          | SDL          |

## ⚖️ License
This work is dual-licensed under **GPL 3.0** OR **MPL 2.0**. You can choose between one of them if you use this work.

If you wish to use this work as a component of a larger, non-GPL compliant project, you are free to do so in any form
(Static linkage, binary distribution, modules) as long as you comply with the MPL 2.0 license.

For any form of software distribution, changes made precisely to this work should be made public to comply with either licenses.

Source file headers should be gradually transitioned to reflect their reusability.

## 🎉 Contributions
[![PRs are welcome](https://img.shields.io/badge/Pull%20requests-welcome-8957e5?style=for-the-badge&logo=github)](https://github.com/LekKit/RVVM/pulls?q=is%3Apr+is%3Aclosed)
|                      | Achievments | Working on |
|----------------------|-------------|------------|
| [**LekKit**](https://github.com/LekKit)                     | RVVM API & infrastructure <br> RV64IMAFDC interpreter, MMU/IRQs/Priv/etc <br> RVJIT Compiler, X86/RISC-V backends <br> NVMe, RTL8169, Framebuffer, many tiny devices <br> Rework of PCIe, PLIC, etc | Networking, Userspace emulation <br> COW blk-dedup image format |
| [**cerg2010cerg2010**](https://github.com/cerg2010cerg2010) | Important fixes, RV64 groundwork, FPU <br> Initial PLIC & PCI, PS2 HID, ATA, OC Ethernet <br> ARM/ARM64 RVJIT backends | Testing, Assistance |
| [**Mr0maks**](https://github.com/Mr0maks)                   | Initial C/M/Zicsr extensions, initial UART, VM debugger <br> ARM32 mul/div JIT intrinsics | - |
| [**0xCatPKG**](https://github.com/0xCatPKG)                 | Userspace network, API improvements <br> Extended testing & portability fixes | HD Audio |
| [**X547**](https://github.com/X547)                         | Haiku GUI, I2C HID, Userland API assistance | Guest Haiku support, UserlandVM |
| [**iyzsong**](https://github.com/iyzsong)                   | OpenBSD & PLIC fixes, Chardev API | |
| [**nebulka1**](https://github.com/nebulka1)                 | Relative input mode | |

## 🔍 TODO
- TCP buffering & NAT in userspace networking
- Suspend/resume to file, VM migration
- Sparse block image format, compression/deduplication
- Sound (HD Audio or else)
- Linux userspace syscalls emulation, ELF loader
- VFIO for GPU passthrough
- More RVJIT optimizations, shared caches
- FPU JIT, Vector/Bitmanip extensions (Waiting for GCC V ext support)
- Other peripherals from real boards (SiFive GPIO, flash)
- *Maybe* virtio devices (For better QEMU interoperability, current devices are plenty fast)
- *A lot more...*
- KVM hypervisor? Alternative CPU engines?

The RISC-V trade name is a registered trade mark of RISC-V International.
