/*
<rvvm/rvvm_base.h> - RVVM Base types and definitions
Copyright (C) 2020-2026 LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#ifndef _RVVM_BASE_API_H
#define _RVVM_BASE_API_H

#if defined(__cplusplus)
#define RVVM_EXTERN_C_BEGIN extern "C" {
#define RVVM_EXTERN_C_END   }
#else
#define RVVM_EXTERN_C_BEGIN
#define RVVM_EXTERN_C_END
#endif

RVVM_EXTERN_C_BEGIN

/* For size_t, NULL */
#include <stddef.h>

#if defined(__GNUC__) || defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
/* For bool, uint32_t, etc */
#if __STDC_VERSION__ < 202311L && !defined(__cplusplus)
#include <stdbool.h>
#endif
#include <stdint.h>
#else
/* Pre-C99 translation unit, expose C99 definitions for public headers */
#if defined(_MSC_VER) && _MSC_VER <= 1500
typedef int _Bool;
#endif
typedef signed char    __int8_t;
typedef unsigned char  __uint8_t;
typedef signed short   __int16_t;
typedef unsigned short __uint16_t;
typedef signed int     __int32_t;
typedef unsigned int   __uint32_t;
#if defined(__LP64__)
typedef signed long int   __int64_t;
typedef unsigned long int __uint64_t;
#else
typedef signed long long   __int64_t;
typedef unsigned long long __uint64_t;
#endif
/* Replace <stdbool.h> */
#if !defined(__bool_true_false_are_defined) && !defined(__cplusplus)
#define __bool_true_false_are_defined 1
#undef bool
#define bool _Bool
#undef false
#define false 0
#undef true
#define true 1
#endif
/* Replace <stdint.h> */
#undef int8_t
#define int8_t __int8_t
#undef uint8_t
#define uint8_t __uint8_t
#undef int16_t
#define int16_t __int16_t
#undef uint16_t
#define uint16_t __uint16_t
#undef int32_t
#define int32_t __int32_t
#undef uint32_t
#define uint32_t __uint32_t
#undef int64_t
#define int64_t __int64_t
#undef uint64_t
#define uint64_t __uint64_t
#endif

/* Expose inline attribute */
#undef inline
#if !(defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L) && defined(_MSC_VER)
#define inline __inline
#elif !(defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L) && defined(__GNUC__)
#define inline __inline__
#elif !(defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L)
#define inline
#endif

#if !defined(RVVM_STATIC) && (defined(_WIN32) || defined(__CYGWIN__)) && defined(USE_LIB)
/* Mark symbols as dllexport when building shared librvvm */
#define RVVM_PUBLIC __declspec(dllexport)
#elif !defined(RVVM_STATIC) && (defined(_WIN32) || defined(__CYGWIN__)) && !defined(RVVM_VERSION)
/* Mark symbols as dllimport when linking to shared librvvm */
#define RVVM_PUBLIC __declspec(dllimport)
#elif !defined(RVVM_STATIC) && defined(__GNUC__) && __GNUC__ >= 4 && (!defined(RVVM_VERSION) || defined(USE_LIB))
/* Mark symbols as visible when building/linking to shared librvvm */
#define RVVM_PUBLIC __attribute__((__visibility__("default")))
#else
#define RVVM_PUBLIC
#endif

#if !defined(RVVM_VERSION)
/**
 * Version string
 */
#define RVVM_VERSION "0.7-git"
#endif

/**
 * Increments on each API/ABI breakage, defined to -1 in staging versions with unstable API/ABI
 */
#define RVVM_ABI_VERSION -1

/**
 * @defgroup rvvm_types Base types and definitions
 * @addtogroup rvvm_types
 * @{
 */

/**
 * Physical memory address
 */
typedef uint64_t rvvm_addr_t;

/**
 * Interrupt index
 */
typedef uint32_t rvvm_irq_t;

/**
 * PCI bus address
 *
 * Encodes func [0:2] dev [3:7] bus [8:15] domain [16:31]
 */
typedef uint32_t rvvm_pci_addr_t;

/**
 * Pixel RGB format enum
 */
typedef uint32_t rvvm_rgb_t;

/**
 * Keyboard keycode
 */
typedef uint8_t rvvm_keycode_t;

/**
 * Machine handle
 */
typedef struct rvvm_machine_t rvvm_machine_t;

/**
 * CPU handle
 */
typedef struct rvvm_hart_t rvvm_cpu_t;

/**
 * Region-based device handle
 */
typedef struct rvvm_reg_dev rvvm_reg_dev_t;

/**
 * Character device handle
 */
typedef struct rvvm_char_dev rvvm_char_dev_t;

/**
 * Network device handle
 */
typedef struct rvvm_net_dev rvvm_net_dev_t;

/**
 * GPIO device handle
 */
typedef struct rvvm_gpio_dev rvvm_gpio_dev_t;

/**
 * Block device handle
 */
typedef struct rvvm_blk_dev rvvm_blk_dev_t;

/**
 * Wired interrupt controller handle
 */
typedef struct rvvm_irq_dev rvvm_irq_dev_t;

/**
 * Framebuffer device (2D GPU) handle
 */
typedef struct rvvm_fbdev rvvm_fbdev_t;

/**
 * Keyboard handle
 */
typedef struct rvvm_keyboard rvvm_keyboard_t;

/**
 * Mouse handle
 */
typedef struct rvvm_mouse rvvm_mouse_t;

/**
 * Snapshot state handle
 */
typedef struct rvvm_snapshot rvvm_snapshot_t;

/**
 * PCI / PCIe bus handle
 */
typedef struct rvvm_pci_bus rvvm_pci_bus_t;

/*
 * PCI function handle
 */
typedef struct rvvm_pci_function rvvm_pci_func_t;

/**
 * USB bus handle
 */
typedef struct rvvm_usb_bus rvvm_usb_bus_t;

/**
 * USB device handle
 */
typedef struct rvvm_usb_dev rvvm_usb_dev_t;

/**
 * I2C bus handle
 */
typedef struct rvvm_i2c_bus rvvm_i2c_bus_t;

/**
 * Flattened Device Tree node handle
 */
typedef struct fdt_node rvvm_fdt_node_t;

/** @}*/

RVVM_EXTERN_C_END

/*
 * Deprecated definitions
 * TODO: Get rid or complete renaming these
 */

#undef PUBLIC
#define PUBLIC RVVM_PUBLIC

typedef struct rvvm_hart_t rvvm_hart_t;

typedef struct rvvm_mmio_dev_t rvvm_mmio_dev_t;

typedef struct rvvm_pci_bus pci_bus_t;

typedef struct rvvm_i2c_bus i2c_bus_t;

typedef struct rvvm_irq_dev rvvm_intc_t;

#endif
