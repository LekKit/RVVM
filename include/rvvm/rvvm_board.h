/*
<rvvm/rvvm_board.h> - RVVM Board devices and helpers
Copyright (C) 2020-2026 LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#ifndef _RVVM_BOARD_DEVICES_H
#define _RVVM_BOARD_DEVICES_H

#include <rvvm/rvvm_blk.h>

RVVM_EXTERN_C_BEGIN

/**
 * @defgroup rvvm_builtin_irq_dev Built-in interrupt controllers
 * @addtogroup rvvm_builtin_irq_dev
 * @{
 */

/**
 * Attach RISC-V Core-local Interrupt Controller to the machine (FDT-based)
 *
 * \param machine Machine handle
 * \param addr    Base MMIO address
 * \return        Region device handle or NULL
 * \return        Success
 */
RVVM_PUBLIC bool rvvm_riscv_clint_init(rvvm_machine_t* machine, rvvm_addr_t addr);

/**
 * Attach RISC-V Platform Interrupt Controller to the machine (FDT-based)
 *
 * If no interrupt controllers existed previously, sets this as the default wired
 * interrupt controller for machine, interrupt controllers are owned by machine
 *
 * \param machine Machine handle
 * \param addr    Base MMIO address
 * \return        Wired interrupt controller handle or NULL
 */
RVVM_PUBLIC rvvm_irq_dev_t* rvvm_riscv_plic_init(rvvm_machine_t* machine, rvvm_addr_t addr);

/**
 * Attach RISC-V Incoming Message-Signaled Interrupt Controller to the machine (FDT-based)
 *
 * \param machine Machine handle
 * \param maddr   Base MMIO address for M-mode controller
 * \param saddr   Base MMIO address for S-mode controller
 * \return        Success
 */
RVVM_PUBLIC bool rvvm_riscv_imsic_init(rvvm_machine_t* machine, /**/
                                       rvvm_addr_t     maddr,   /**/
                                       rvvm_addr_t     saddr);

/**
 * Attach RISC-V Advanced Platform Interrupt Controller to the machine (FDT-based)
 *
 * Together with RISC-V IMSIC, replaces RISC-V PLIC in a modern AIA system
 *
 * If no interrupt controllers existed previously, sets this as the default wired
 * interrupt controller for machine, interrupt controllers are owned by machine
 *
 * \param machine Machine handle
 * \param maddr   Base MMIO address for M-mode controller
 * \param saddr   Base MMIO address for S-mode controller
 * \return        Wired interrupt controller handle or NULL
 */
RVVM_PUBLIC rvvm_irq_dev_t* rvvm_riscv_aplic_init(rvvm_machine_t* machine, /**/
                                                  rvvm_addr_t     maddr,   /**/
                                                  rvvm_addr_t     saddr);

/**
 * Attach x86 PIC, LAPIC, IOAPIC interrupt controllers to the machine (ACPI-based)
 *
 * The guest decides whether to use IOAPIC or PIC, all interrupt lines are mapped 1:1
 *
 * IOAPIC is at address 0xFEC00000, LAPIC is at 0xFEE00000 (May be modified via MSR)
 *
 * Master PIC is at port 0x20, slave PIC is at port 0xA0
 *
 * Addresses are hardcoded to assist virtualization, and match most real HW
 *
 * If no interrupt controllers existed previously, sets this as the default wired
 * interrupt controller for machine, interrupt controllers are owned by machine
 *
 * \param machine Machine handle
 * \return        Wired interrupt controller handle or NULL
 */
RVVM_PUBLIC rvvm_irq_dev_t* rvvm_x86_apic_init(rvvm_machine_t* machine);

/**
 * @}
 * @defgroup rvvm_builtin_bus_dev Built-in bus controllers
 * @addtogroup rvvm_builtin_bus_dev
 * @{
 */

/**
 * Attach ECAM PCIe host controller (ACPI/FDT based)
 *
 * \param machine  Machine handle
 * \param domain   PCI domain
 * \param addr     Base address of ECAM space
 * \param irq_dev  Wired interrupt controller handle
 * \param irqs     Vector of 4 wired IRQs
 * \param io_addr  Start of PCI IO window
 * \param io_size  Length of PCI IO window
 * \param mem_addr Start of PCI MMIO window
 * \param mem_size Length of PCI MMIO window
 * \return         Success
 */
RVVM_PUBLIC bool rvvm_pci_ecam_init(rvvm_machine_t*   machine,   /**/
                                    uint32_t          domain,    /**/
                                    rvvm_addr_t       addr,      /**/
                                    rvvm_irq_dev_t*   irq_dev,   /**/
                                    const rvvm_irq_t* irqs,      /**/
                                    rvvm_addr_t       io_addr,   /**/
                                    rvvm_addr_t       io_size,   /**/
                                    rvvm_addr_t       mem_addr,  /**/
                                    rvvm_addr_t       mem_size); /**/

/**
 * Attach legacy x86 PCI controller via IO ports
 *
 * \param machine Machine handle
 * \param port    Base port of PCI controller, usually 0xCF8
 * \param irq_dev Wired interrupt controller handle
 * \param irqs    Vector of 4 wired IRQs, usually 11, 10, 9, 5
 * \return        Success
 */
RVVM_PUBLIC bool rvvm_pci_legacy_init(rvvm_machine_t*   machine, /**/
                                      rvvm_addr_t       port,    /**/
                                      rvvm_irq_dev_t*   irq_dev, /**/
                                      const rvvm_irq_t* irqs);

/**
 * @}
 * @defgroup rvvm_builtin_serial_dev Built-in serial devices
 * @addtogroup rvvm_builtin_serial_dev
 * @{
 */

/**
 * Attach Altera PS/2 controller to the machine (FDT-based)
 *
 * \param machine Machine handle
 * \param chardev Character device handle (Nullable)
 * \param addr    Base MMIO address
 * \param irq_dev Wired interrupt controller handle (Nullable)
 * \param irq     Device wired interrupt line
 * \return        Region device handle or NULL
 */
RVVM_PUBLIC rvvm_reg_dev_t* rvvm_ps2_altera_init(rvvm_machine_t*  machine, /**/
                                                 rvvm_char_dev_t* chardev, /**/
                                                 rvvm_addr_t      addr,    /**/
                                                 rvvm_irq_dev_t*  irq_dev, /**/
                                                 rvvm_irq_t       irq);

static inline rvvm_reg_dev_t* rvvm_ps2_altera_init_auto(rvvm_machine_t* machine, rvvm_char_dev_t* chardev)
{
    return rvvm_ps2_altera_init(machine, chardev, 0x20000000U, NULL, 0);
}

/**
 * Attach OpenCores I2C controller to the machine (FDT-based)
 *
 * If no I2C controllers existed previously, sets
 * this as the default I2C controller for machine
 *
 * \param machine Machine handle
 * \param addr    Base MMIO address
 * \param irq_dev Wired interrupt controller handle (Nullable)
 * \param irq     Device wired interrupt line
 * \return        I2C bus handle or NULL
 */
RVVM_PUBLIC rvvm_i2c_bus_t* rvvm_i2c_ocores_init(rvvm_machine_t* machine, /**/
                                                 rvvm_addr_t     addr,    /**/
                                                 rvvm_irq_dev_t* irq_dev, /**/
                                                 rvvm_irq_t      irq);

static inline rvvm_i2c_bus_t* rvvm_i2c_ocores_init_auto(rvvm_machine_t* machine)
{
    return rvvm_i2c_ocores_init(machine, 0x10030000UL, NULL, 0);
}

/**
 * @}
 * @defgroup rvvm_builtin_rtc_dev Built-in real-time clock devices
 * @addtogroup rvvm_builtin_rtc_dev
 * @{
 */

/**
 * Attach Goldfish real-time clock to the machine (FDT-based)
 *
 * \param machine Machine handle
 * \param addr    Base MMIO address
 * \param irq_dev Wired interrupt controller handle (Nullable)
 * \param irq     Device wired interrupt line
 * \return        Region device handle or NULL
 *
 * This device is hot-removable via rvvm_region_free()
 */
RVVM_PUBLIC rvvm_reg_dev_t* rvvm_rtc_goldfish_init(rvvm_machine_t* machine, /**/
                                                   rvvm_addr_t     addr,    /**/
                                                   rvvm_irq_dev_t* irq_dev, /**/
                                                   rvvm_irq_t      irq);

/**
 * Attach PC CMOS real-time clock to the machine
 *
 * \param machine Machine handle
 * \param port    Base IO port
 * \param irq_dev Wired interrupt controller handle (Nullable)
 * \param irq     Device wired interrupt line
 * \return        Region device handle or NULL
 *
 * This device is hot-removable via rvvm_region_free()
 */
RVVM_PUBLIC rvvm_reg_dev_t* rvvm_rtc_cmos_init(rvvm_machine_t* machine, /**/
                                               rvvm_addr_t     port,    /**/
                                               rvvm_irq_dev_t* irq_dev, /**/
                                               rvvm_irq_t      irq);

static inline rvvm_reg_dev_t* rvvm_rtc_cmos_init_auto(rvvm_machine_t* machine)
{
    return rvvm_rtc_cmos_init(machine, 0x70, NULL, 8);
}

/**
 * Attach Dallas DS1742 real-time clock to the machine (FDT-based)
 *
 * \param machine Machine handle
 * \param addr    Base MMIO address
 * \return        Region device handle or NULL
 *
 * This device is hot-removable via rvvm_region_free()
 */
RVVM_PUBLIC rvvm_reg_dev_t* rvvm_rtc_ds1742_init(rvvm_machine_t* machine, rvvm_addr_t addr);

/**
 * @}
 * @defgroup rvvm_builtin_misc_dev Built-in miscellaneous devices
 * @addtogroup rvvm_builtin_misc_dev
 * @{
 */

/**
 * Attach syscon power-management device to the machine (FDT-based)
 *
 * \param machine Machine handle
 * \param addr    Base MMIO address
 * \return        Region device handle or NULL
 *
 * This device is hot-removable via rvvm_region_free()
 */
RVVM_PUBLIC rvvm_reg_dev_t* rvvm_syscon_init(rvvm_machine_t* machine, rvvm_addr_t addr);

/**
 * Attach SiFive GPIO device to the machine (FDT-based)
 *
 * GPIO device becomes owned by machine and will be freed automatically
 *
 * \param machine Machine handle
 * \param gpio    GPIO device handle (Nullable)
 * \param addr    Base MMIO address
 * \param irq_dev Wired interrupt controller handle (Nullable)
 * \param irqs    Vector of 32 wired interrupts for each GPIO line (Nullable)
 * \return        Region device handle or NULL
 *
 * This device is hot-removable via rvvm_region_free()
 */
RVVM_PUBLIC rvvm_reg_dev_t* rvvm_gpio_sifive_init(rvvm_machine_t*   machine, /**/
                                                  rvvm_gpio_dev_t*  gpio,    /**/
                                                  rvvm_addr_t       addr,    /**/
                                                  rvvm_irq_dev_t*   irq_dev, /**/
                                                  const rvvm_irq_t* irqs);

static inline rvvm_reg_dev_t* rvvm_gpio_sifive_init_auto(rvvm_machine_t* machine, rvvm_gpio_dev_t* gpio)
{
    return rvvm_gpio_sifive_init(machine, gpio, 0x10060000UL, NULL, NULL);
}

/**
 * @}
 * @defgroup rvvm_builtin_gfx_dev Built-in graphics devices
 * @addtogroup rvvm_builtin_gfx_dev
 * @{
 */

/**
 * Attach simple-framebuffer display device to the machine (FDT-based)
 *
 * The fbdev becomes owned by machine and will be freed automatically,
 * video mode is expected to be set up by the caller
 *
 * \param machine Machine handle
 * \param fbdev   Framebuffer device handle (Nullable)
 * \param addr    Framebuffer base MMIO address
 * \return        Region device handle or NULL
 *
 * This device is hot-removable via rvvm_region_free()
 */
RVVM_PUBLIC rvvm_reg_dev_t* rvvm_simplefb_init(rvvm_machine_t* machine, /**/
                                               rvvm_fbdev_t*   fbdev,   /**/
                                               rvvm_addr_t     addr);

/**
 * Attach Bochs display device to the machine (PCI-based)
 *
 * The fbdev becomes owned by machine and will be freed automatically,
 * video mode can be reconfigured by the guest, minimum VRAM is 16MiB
 *
 * \param machine Machine handle
 * \param fbdev   Framebuffer device handle (Nullable)
 * \param addr    PCI bus address
 * \return        PCI function handle or NULL
 *
 * This device is hot-removable via rvvm_pci_func_free()
 */
RVVM_PUBLIC rvvm_pci_func_t* rvvm_bochs_display_init(rvvm_machine_t* machine, /**/
                                                     rvvm_fbdev_t*   fbdev,   /**/
                                                     rvvm_pci_addr_t addr);

static inline rvvm_pci_func_t* rvvm_bochs_display_init_auto(rvvm_machine_t* machine, rvvm_fbdev_t* fbdev)
{
    return rvvm_bochs_display_init(machine, fbdev, -1);
}

/**
 * @}
 * @defgroup rvvm_builtin_blk_dev Built-in storage devices
 * @addtogroup rvvm_builtin_blk_dev
 * @{
 */

/**
 * Attach mtd-ram block device to the machine (FDT-based)
 *
 * Block device becomes owned by machine and will be freed automatically
 *
 * The main purpose of this device is to allow guests to flash firmware,
 * however it may be used as a very simplistic storage device
 *
 * \param machine Machine handle
 * \param addr    Base MMIO address
 * \param blk     Block device handle (Nullable)
 * \return        Region device handle or NULL
 *
 * This device is hot-removable via rvvm_region_free()
 */
RVVM_PUBLIC rvvm_reg_dev_t* mtd_ram_init_blk(rvvm_machine_t* machine, /**/
                                             rvvm_addr_t     addr,    /**/
                                             rvvm_blk_dev_t* blk);

/**
 * Attach NVMe block device to the machine (PCI-based)
 *
 * Block device becomes owned by machine and will be freed automatically
 *
 * \param machine Machine handle
 * \param blk     Block device handle (Nullable)
 * \param addr    PCI bus address
 * \return        PCI function handle or NULL
 *
 * This device is hot-removable via rvvm_pci_func_free()
 */
RVVM_PUBLIC rvvm_pci_func_t* rvvm_nvme_init(rvvm_machine_t* machine, rvvm_blk_dev_t* blk, rvvm_pci_addr_t addr);

static inline rvvm_pci_func_t* rvvm_nvme_init_auto(rvvm_machine_t* machine, const char* path)
{
    return rvvm_nvme_init(machine, rvvm_blk_open(path, NULL, RVVM_BLK_RW), -1);
}

/**
 * Attach Parallel ATA (IDE) block device to the machine (PCI-based)
 *
 * Block device becomes owned by machine and will be freed automatically
 *
 * The ATA device pre-allocates its IO BARs to 0x1F0 and 0x3F6 for legacy ATA PIO
 *
 * \param machine  Machine handle
 * \param blk      Block device handle (Nullable)
 * \param addr     PCI bus address
 * \return         PCI function handle or NULL
 *
 * This device is hot-removable via rvvm_pci_func_free()
 */
RVVM_PUBLIC rvvm_pci_func_t* rvvm_ata_init(rvvm_machine_t* machine, /**/
                                           rvvm_blk_dev_t* blk,     /**/
                                           rvvm_pci_addr_t addr);

static inline rvvm_pci_func_t* rvvm_ata_init_auto(rvvm_machine_t* machine, const char* path)
{
    return rvvm_ata_init(machine, rvvm_blk_open(path, NULL, RVVM_BLK_RW), -1);
}

/**
 * @}
 * @defgroup rvvm_board_setup Built-in boards
 * @addtogroup rvvm_board_setup
 * @{
 */

/**
 * Attach main RAM region to the machine
 *
 * Address and size must be page-aligned (4kb)
 *
 * This automatically handles suspend, ACPI/FDT reporting, etc
 *
 * \param machine Machine handle
 * \param addr    RAM region address
 * \param size    RAM region size
 * \return        Region device handle or NULL
 *
 * This device is hot-removable via rvvm_region_free()
 */
RVVM_PUBLIC rvvm_reg_dev_t* rvvm_main_ram_init(rvvm_machine_t* machine, rvvm_addr_t addr, size_t size);

/**
 * Resize main RAM region
 *
 * Size must be page-aligned (4kb)
 *
 * \param ram  Region device handle obtained via rvvm_main_ram_init()
 * \param size RAM region size
 * \return     Success
 */
RVVM_PUBLIC bool rvvm_main_ram_resize(rvvm_reg_dev_t* ram, size_t size);

static inline bool rvvm_board_riscv_virt_init(rvvm_machine_t* machine, bool aia)
{
    rvvm_irq_dev_t* irq_dev = NULL;
    /* Initialize RISC-V ACLINT */
    if (!rvvm_riscv_clint_init(machine, 0x02000000UL)) {
        return false;
    }
    if (aia) {
        /* Initialize RISC-V IMSIC & APLIC (Advanced Interrupt Architecture) */
        if (!rvvm_riscv_imsic_init(machine, 0x24000000UL, 0x28000000UL)) {
            return false;
        }
        irq_dev = rvvm_riscv_aplic_init(machine, 0x0C000000UL, 0x0D000000UL);
    } else {
        /* Initialize RISC-V PLIC */
        irq_dev = rvvm_riscv_plic_init(machine, 0x0C000000UL);
    }
    if (!irq_dev) {
        return false;
    }
    /* Initialize PCIe ECAM */
    if (!rvvm_pci_ecam_init(machine, 0, 0x30000000UL,   /**/
                            irq_dev, NULL,              /**/
                            0x03000000UL, 0x00010000UL, /**/
                            0x40000000UL, 0x40000000UL)) {
        return false;
    }
    /* Initialize Syscon */
    if (!rvvm_syscon_init(machine, 0x00100000UL)) {
        return false;
    }
    /* Initialize Goldfish RTC */
    if (!rvvm_rtc_goldfish_init(machine, 0x00101000UL, irq_dev, 0)) {
        return false;
    }

    return true;
}

/** @}*/

RVVM_EXTERN_C_END

#endif
