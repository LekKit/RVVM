<div align="center">

![RVVM Logo](https://github.com/user-attachments/assets/a8d241eb-ebe9-4ceb-a31f-8fd452db75e6 "The “RISC-V” trade name is a registered trade mark of RISC-V International. AmazDooM font is licensed under CC BY-NC 3.0. If you're a designer and have a better logo idea, please open an issue!")

[![version](https://img.shields.io/badge/version-0.7--git-brightgreen?style=for-the-badge)](#-installing) [![Build](https://img.shields.io/github/actions/workflow/status/LekKit/RVVM/build.yml?branch=staging&style=for-the-badge)](https://github.com/LekKit/RVVM/actions/workflows/build.yml) [![Codacy grade](https://img.shields.io/codacy/grade/c77cc7499a784cd293fde58641ce3e46?logo=codacy&style=for-the-badge)](https://app.codacy.com/gh/LekKit/RVVM/dashboard)

[![Demo](https://img.shields.io/badge/Check%20it%20out-WASM%20Demo-red?style=for-the-badge)](https://lekkit.github.io/test/index.html) [![Wiki](https://img.shields.io/badge/Wiki-brightgreen?style=for-the-badge)](https://github.com/LekKit/RVVM/wiki)

</div>

# RVVM - The RISC-V Virtual Machine
RVVM is a virtual machine / emulator for RISC-V guests, which emphasizes on performance, security, lean code and portability. It already runs a lot of guest operating systems, including Linux, Haiku, FreeBSD, OpenBSD, etc. It also aims to run RISC-V applications on a foreign-arch host without full OS guest & isolation (Userland emulation).

## Main features
- Fully spec-compliant **rv64imafdcb** instruction set, Zkr/Zicbom/Zicboz/Sstc extensions
- Tracing JIT with x86_64, ARM64, RISC-V backends - Faster than QEMU TCG
- Working OpenSBI & U-Boot, Linux, FreeBSD, OpenBSD, Haiku guests
- Framebuffer display, HID mouse & keyboard, UART terminal
- NVMe storage drives, TRIM support (Deallocate space on host), fast multi-threaded IO
- Networking userland stack (Works on any host OS)
- VFIO PCIe passthrough (For GPUs, etc)
- Kernel-level isolation to prevent and contain vulnerability exploitation
- Library API (**librvvm**) for machine/userland emulation, implementing new devices
- Userland emulation (WIP)
- Shadow pagetable acceleration (WIP)
- See [wiki page](https://github.com/LekKit/RVVM/wiki) for full list of features

## 📦 Installing
[![Artifacts](https://img.shields.io/badge/BIN-Artifacts-brightgreen?style=for-the-badge)](https://nightly.link/LekKit/RVVM/workflows/build/staging) [![AUR](https://img.shields.io/badge/Arch%20Linux-AUR-blue?style=for-the-badge&logo=archlinux)](https://aur.archlinux.org/packages/rvvm-git) [![Build](https://img.shields.io/badge/Build-Make-red?style=for-the-badge)](#-building)

## 🛠 Building
Currently builds using GNU Make (recommended) or CMake and is extremely portable.
```sh
git clone https://github.com/LekKit/RVVM
cd RVVM
make
cd release.linux.x86_64
./rvvm_x86_64 -h
```

Alternatively, you can use CMake:
```sh
git clone https://github.com/LekKit/RVVM
cd RVVM
cmake -S. -Bbuild
cmake --build build --target all
cd build
./rvvm -h
```

See the [wiki page](https://github.com/LekKit/RVVM/wiki/Building-&-Installing) for advanced build manual like cross-compilation.

## 🚀 Running
Example: Launches a dual-core VM with 2 GiB of RAM, 1280x720 display.

Runs OpenSBI + U-Boot firmware, EFI guest from `drive.img`. Forwards host `127.0.0.1:2022` into guest SSH port.
```sh
rvvm fw_payload.bin -i drive.img -m 2G -smp 2 -res 1280x720 -portfwd tcp/127.0.0.1:2022=22
```

Argument explanation:
```
[fw_payload.bin]    Initial M-mode firmware, OpenSBI + U-Boot in this case
-i  drive.img       Attach preferred storage image (Currently as NVMe)
-m 2G               Memory amount (may be suffixed by k/M/G), default 256M
-smp 2              Amount of cores, single-core machine by default
-res 1280x720       Set display(s) resolution
-portfwd 8080=80    Port forwarding (Extended: tcp/127.0.0.1:8080=80)
 . . .
-rv32               Enable 32-bit RISC-V, 64-bit by default
-v                  Verbose mode
-h                  Extended help
```

See [wiki page](https://github.com/LekKit/RVVM/wiki/Running) for recommended guest firmware/images and full argument explanation. 

## ⚖️ License
The **librvvm** library is licensed under non-viral [**MPL 2.0**](https://github.com/LekKit/RVVM/blob/staging/LICENSE-MPL) license.

If you wish to use **librvvm** as a component of a larger, non-GPL compliant project (permissive, etc), you are free<br>
to do so in any form (Static linkage, binary distribution, modules) as long as you comply with the MPL 2.0 license.

The RVVM Manager and Linux userland emulator (**rvvm** and **rvvm-user**) binaries are licensed under the [**GPL 3.0**](https://github.com/LekKit/RVVM/blob/staging/LICENSE-GPL)<br>
license, since they are intended for end-users. All the heavy lifting is done by **librvvm** anyways.

Source file headers should be gradually fixed to reflect this.

## 🎉 Contributions
[![PRs are welcome](https://img.shields.io/badge/Pull%20requests-welcome-8957e5?style=for-the-badge&logo=github)](https://github.com/LekKit/RVVM/pulls?q=is%3Apr+is%3Aclosed)
|                      | Achievements | Working on |
|----------------------|-------------|------------|
| [**LekKit**](https://github.com/LekKit)                     | RVVM API & infrastructure <br> RV64IMAFDC interpreter, MMU/IRQs/Priv/etc <br> RVJIT Compiler, X86/RISC-V backends <br> NVMe, RTL8169, VFIO, many tiny devices <br> Userspace network <br> Rework of PCIe, PLIC, etc | Networking, Userspace emulation <br> COW blk-dedup image format <br> New CPU features & JIT optimizations |
| [**cerg2010cerg2010**](https://github.com/cerg2010cerg2010) | Important fixes, RV64 groundwork, FPU <br> Initial PLIC & PCI, PS2 HID, ATA, OC Ethernet <br> ARM/ARM64 RVJIT backends | Testing, Assistance |
| [**Mr0maks**](https://github.com/Mr0maks)                   | Initial C/M/Zicsr extensions, initial UART, VM debugger <br> ARM32 mul/div JIT intrinsics | - |
| [**0xCatPKG**](https://github.com/0xCatPKG)                 | Userspace network & API improvements <br> Extended testing & portability fixes | HD Audio |
| [**X547**](https://github.com/X547)                         | Haiku GUI, I2C HID, Userland API assistance | Guest Haiku support, UserlandVM |
| [**iyzsong**](https://github.com/iyzsong)                   | OpenBSD & PLIC fixes, Chardev API | |
| [**nebulka1**](https://github.com/nebulka1)                 | Relative input mode | |

## 🔍 TODO
- Implement Svpbmt, Svnapot extensions
- Sparse block image format with compression/deduplication
- Suspend/resume to file, VM migration
- Linux userspace binary emulation (WIP)
- USB3.0 XHCI, USB passthrough
- Sound (HD Audio or else)
- More RVJIT optimizations, shared caches
- FPU JIT (Complicated AF to make a conformant one)
- Vector extensions
- Other peripherals from real boards (VisionFive 2: GPIO, SPI, flash...)
- RISC-V APLIC, PCIe MSI Interrupts
- Virtio devices (For better QEMU interoperability; VirGL via Virtio-GPU)
- Free page reporting via virtio-balloon
- *A lot more...*
- KVM hypervisor? Alternative CPU engines?


The RISC-V trade name is a registered trade mark of RISC-V International.
