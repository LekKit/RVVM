/*
riscv_mmu.c - RISC-V Memory Mapping Unit
Copyright (C) 2021  LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include "riscv_mmu.h"
#include "riscv_csr.h"
#include "riscv_hart.h"
#include "riscv_cpu.h"
#include "bit_ops.h"
#include "atomics.h"
#include "utils.h"
#include "vma_ops.h"
#include <string.h>

#define SV32_VPN_BITS     10
#define SV32_VPN_MASK     0x3FF
#define SV32_PHYS_BITS    34
#define SV32_LEVELS       2

#define SV64_VPN_BITS     9
#define SV64_VPN_MASK     0x1FF
#define SV64_PHYS_BITS    56
#define SV64_PHYS_MASK    0xFFFFFFFFFFFFFFULL
#define SV39_LEVELS       3
#define SV48_LEVELS       4
#define SV57_LEVELS       5

#define RISCV_MMU_DEBUG_ACCESS (RISCV_MMU_READ | RISCV_MMU_EXEC | RISCV_MMU_WRITE)

bool riscv_init_ram(rvvm_ram_t* mem, rvvm_addr_t base_addr, size_t size)
{
    // Memory boundaries should be always aligned to page size
    if ((base_addr & RISCV_PAGE_MASK) || (size & RISCV_PAGE_MASK)) {
        rvvm_error("Memory boundaries misaligned: 0x%08"PRIx64" - 0x%08"PRIx64, base_addr, base_addr + size);
        return false;
    }

    uint32_t vma_flags = VMA_RDWR;
    if (!rvvm_has_arg("no_ksm")) vma_flags |= VMA_KSM;
    if (!rvvm_has_arg("no_thp")) vma_flags |= VMA_THP;
    mem->data = vma_alloc(NULL, size, vma_flags);

    if (!mem->data) {
        rvvm_error("Memory allocation failure");
        return false;
    }

    mem->addr = base_addr;
    mem->size = size;
    return true;
}

void riscv_free_ram(rvvm_ram_t* mem)
{
    vma_free(mem->data, mem->size);
    // Prevent accidental access
    mem->size = 0;
    mem->addr = 0;
    mem->data = NULL;
}

#ifdef USE_JIT

void riscv_jit_tlb_flush(rvvm_hart_t* vm)
{
    memset(vm->jtlb, 0, sizeof(vm->jtlb));
    vm->jtlb[0].pc = -1;
}

#endif

void riscv_tlb_flush(rvvm_hart_t* vm)
{
    // Any lookup to nonzero page fails as VPN is zero
    memset(vm->tlb, 0, sizeof(vm->tlb));
    // For zero page, place nonzero VPN
    vm->tlb[0].r = -1;
    vm->tlb[0].w = -1;
    vm->tlb[0].e = -1;
#ifdef USE_JIT
    riscv_jit_tlb_flush(vm);
#endif
    riscv_restart_dispatch(vm);
}

void riscv_tlb_flush_page(rvvm_hart_t* vm, rvvm_addr_t addr)
{
    // TLB entry invalidation is done via off-by-1 VPN
    const rvvm_addr_t vpn = (addr >> RISCV_PAGE_SHIFT);
    rvvm_tlb_entry_t* entry = &vm->tlb[vpn & RVVM_TLB_MASK];
    if (unlikely(entry->r == vpn)) {
        entry->r = vpn - 1;
    }
    if (unlikely(entry->w == vpn)) {
        entry->w = vpn - 1;
    }
    if (unlikely(entry->e == vpn)) {
        entry->e = vpn - 1;
#ifdef USE_JIT
        riscv_jit_tlb_flush(vm);
#endif
        riscv_restart_dispatch(vm);
    }
}

static void riscv_tlb_put(rvvm_hart_t* vm, rvvm_addr_t vaddr, void* ptr, uint8_t access)
{
    const rvvm_addr_t vpn = vaddr >> RISCV_PAGE_SHIFT;
    rvvm_tlb_entry_t* entry = &vm->tlb[vpn & RVVM_TLB_MASK];

    /*
    * Add only requested access bits for correct access/dirty flags
    * implementation. Assume the software does not clear A/D bits without
    * calling SFENCE.VMA
    */
    switch (access) {
        case RISCV_MMU_READ:
            entry->r = vpn;
            // If same tlb entry contains different VPNs,
            // they should be invalidated
            if (entry->w != vpn) entry->w = vpn - 1;
            if (entry->e != vpn) entry->e = vpn - 1;
            break;
        case RISCV_MMU_WRITE:
            entry->r = vpn;
            entry->w = vpn;
            if (entry->e != vpn) entry->e = vpn - 1;
            break;
        case RISCV_MMU_EXEC:
            if (entry->r != vpn) entry->r = vpn - 1;
            //if (entry->w != vpn) entry->w = vpn - 1;
            entry->w = vpn - 1; // W^X on the TLB to track dirtiness
            entry->e = vpn;
            break;
        default:
            rvvm_fatal("Unknown MMU access in riscv_tlb_put()");
            break;
    }

    entry->ptr = ((size_t)ptr) - vaddr;
}

static forceinline void* riscv_phys_access(rvvm_hart_t* vm, rvvm_addr_t paddr)
{
    if (likely(paddr >= vm->mem.addr && (paddr - vm->mem.addr) < vm->mem.size)) {
        return ((uint8_t*)vm->mem.data) + (paddr - vm->mem.addr);
    }
    return NULL;
}

static inline bool riscv_mmu_check_priv(rvvm_hart_t* vm, uint8_t priv, uint8_t access)
{
    if (access == RISCV_MMU_DEBUG_ACCESS) {
        // Unrestrained page access to external debugger
        return true;
    }
    if (access == RISCV_MMU_EXEC) {
        // Do not allow executing user pages from supervisor and vice versa
        // MXR sets access to RISCV_MMU_READ | RISCV_MMU_EXEC
        return false;
    }
    if (priv != RISCV_PRIV_SUPERVISOR || !(vm->csr.status & CSR_STATUS_SUM)) {
        // If we are supervisor with SUM bit set, RW operations on user pages are allowed
        return false;
    }
    return true;
}

#ifdef USE_RV32

// Virtual memory addressing mode (SV32)
static inline bool riscv_mmu_translate_sv32(rvvm_hart_t* vm, rvvm_addr_t vaddr, rvvm_addr_t* paddr, uint8_t priv, uint8_t access)
{
    // Pagetable is always aligned to PAGE_SIZE
    rvvm_addr_t pagetable = vm->root_page_table;
    bitcnt_t bit_off = SV32_VPN_BITS + RISCV_PAGE_SHIFT;

    for (size_t i = 0; i < SV32_LEVELS; ++i) {
        size_t pgt_off = ((vaddr >> bit_off) & SV32_VPN_MASK) << 2;
        void* pte_ptr = riscv_phys_access(vm, pagetable + pgt_off);
        if (unlikely(!pte_ptr)) {
            // Physical fault on pagetable walk
            return false;
        }
        while (true) {
            uint32_t pte = read_uint32_le(pte_ptr);
            if (likely(pte & RISCV_PTE_VALID)) {
                if (pte & RISCV_PTE_LEAF) {
                    // PGT entry is a leaf, check permissions
                    // Check that PTE U-bit matches U-mode, otherwise do extended check
                    if (unlikely(!!(pte & RISCV_PTE_USER) == !!priv)) {
                        if (!riscv_mmu_check_priv(vm, priv, access)) {
                            return false;
                        }
                    }
                    // Check access bits & translate
                    if (likely(pte & access)) {
                        rvvm_addr_t vmask = bit_mask(bit_off);
                        rvvm_addr_t pmask = bit_mask(SV32_PHYS_BITS - bit_off) << bit_off;
                        rvvm_addr_t pte_shift = pte << 2;
                        uint32_t pte_ad = pte | RISCV_PTE_ACCESSED | ((access & RISCV_MMU_WRITE) << 5);
                        if (unlikely(pte_shift & vmask & RISCV_PAGE_PNMASK)) {
                            // Misaligned page
                            return false;
                        }
                        if (unlikely(pte != pte_ad)) {
                            // Atomically update A/D flags
                            if (unlikely(!atomic_cas_uint32_le(pte_ptr, pte, pte_ad))) {
                                // CAS failed, reload the PTE and start over
                                continue;
                            }
                        }
                        // Combine ppn & vpn & pgoff
                        *paddr = (pte_shift & pmask) | (vaddr & vmask);
                        return true;
                    }
                } else if (likely(!(pte & RISCV_MMU_WRITE))) {
                    // PTE is a pointer to next pagetable level
                    pagetable = (pte >> 10) << RISCV_PAGE_SHIFT;
                    bit_off -= SV32_VPN_BITS;
                }
            }
            break;
        }
    }
    // No valid address translation can be done
    return false;
}

#endif

#ifdef USE_RV64

// Virtual memory addressing mode (RV64 MMU template)
static inline bool riscv_mmu_translate_rv64(rvvm_hart_t* vm, rvvm_addr_t vaddr, rvvm_addr_t* paddr, uint8_t priv, uint8_t access, uint8_t sv_levels)
{
    // Pagetable is always aligned to PAGE_SIZE
    rvvm_addr_t pagetable = vm->root_page_table;
    bitcnt_t bit_off = (sv_levels * SV64_VPN_BITS) + RISCV_PAGE_SHIFT - SV64_VPN_BITS;

    if (unlikely(vaddr != (rvvm_addr_t)sign_extend(vaddr, bit_off + SV64_VPN_BITS))) {
        // Non-canonical virtual address
        return false;
    }

    for (size_t i = 0; i < sv_levels; ++i) {
        size_t pgt_off = ((vaddr >> bit_off) & SV64_VPN_MASK) << 3;
        void* pte_ptr = riscv_phys_access(vm, pagetable + pgt_off);
        if (unlikely(!pte_ptr)) {
            // Physical fault on pagetable walk
            return false;
        }
        while (true) {
            uint64_t pte = read_uint64_le(pte_ptr);
            if (likely(pte & RISCV_PTE_VALID)) {
                if (unlikely(pte & (1ULL << 54))) {
                    // PTE bit 54 is reserved in the exposed RV64 modes.
                    return false;
                }
                if (pte & RISCV_PTE_LEAF) {
                    // PGT entry is a leaf, check permissions
                    // Check that PTE U-bit matches U-mode, otherwise do extended check
                    if (unlikely(!!(pte & RISCV_PTE_USER) == !!priv)) {
                        if (!riscv_mmu_check_priv(vm, priv, access)) {
                            return false;
                        }
                    }
                    // Check access bits & translate
                    if (likely(pte & access)) {
                        rvvm_addr_t vmask = bit_mask(bit_off);
                        rvvm_addr_t pmask = bit_mask(SV64_PHYS_BITS - bit_off) << bit_off;
                        rvvm_addr_t pte_shift = pte << 2;
                        uint64_t pte_ad = pte | RISCV_PTE_ACCESSED | ((access & RISCV_MMU_WRITE) << 5);
                        if (unlikely((pte_shift & vmask) & RISCV_PAGE_PNMASK)) {
                            // Misaligned page
                            return false;
                        }
                        if (unlikely(pte != pte_ad)) {
                            // Atomically update A/D flags
                            if (unlikely(!atomic_cas_uint64_le(pte_ptr, pte, pte_ad))) {
                                // CAS failed, reload the PTE and start over
                                continue;
                            }
                        }
                        // Combine ppn & vpn & pgoff
                        *paddr = (pte_shift & pmask) | (vaddr & vmask);
                        return true;
                    }
                } else if (likely(!(pte & RISCV_MMU_WRITE))) {
                    // PTE is a pointer to next pagetable level
                    pagetable = ((pte >> 10) << RISCV_PAGE_SHIFT) & SV64_PHYS_MASK;
                    bit_off -= SV64_VPN_BITS;
                }
            }
            break;
        }
    }
    // No valid address translation can be done
    return false;
}

#endif

// Translate virtual address to physical with respect to current CPU mode
static inline bool riscv_mmu_translate(rvvm_hart_t* vm, rvvm_addr_t vaddr, rvvm_addr_t* paddr, uint8_t access)
{
    uint8_t priv = vm->priv_mode;
    // If MPRV is enabled, and we aren't fetching an instruction,
    // change effective privilege mode to STATUS.MPP
    if (unlikely((vm->csr.status & CSR_STATUS_MPRV) && (access != RISCV_MMU_EXEC))) {
        priv = bit_cut(vm->csr.status, 11, 2);
    }
    // If MXR is enabled, reads from pages marked as executable-only should succeed
    if (unlikely((vm->csr.status & CSR_STATUS_MXR) && (access == RISCV_MMU_READ))) {
        access |= RISCV_MMU_EXEC;
    }
    if (priv < RISCV_PRIV_MACHINE) {
        switch (vm->mmu_mode) {
            case CSR_SATP_MODE_BARE:
                *paddr = vaddr;
                return true;
#ifdef USE_RV32
            case CSR_SATP_MODE_SV32:
                return riscv_mmu_translate_sv32(vm, vaddr, paddr, priv, access);
#endif
#ifdef USE_RV64
            case CSR_SATP_MODE_SV39:
                return riscv_mmu_translate_rv64(vm, vaddr, paddr, priv, access, SV39_LEVELS);
            case CSR_SATP_MODE_SV48:
                return riscv_mmu_translate_rv64(vm, vaddr, paddr, priv, access, SV48_LEVELS);
            case CSR_SATP_MODE_SV57:
                return riscv_mmu_translate_rv64(vm, vaddr, paddr, priv, access, SV57_LEVELS);
#endif
            default:
                // satp is a WARL field
                rvvm_error("Unknown MMU mode in riscv_mmu_translate");
                return false;
        }
    } else {
        *paddr = vaddr;
        return true;
    }
}

/*
 * Since aligned loads/stores expect relaxed atomicity, MMU should use this instead of a regular memcpy,
 * to prevent other harts from observing half-made memory operation on TLB miss
 */

TSAN_SUPPRESS static forceinline void atomic_memcpy_relaxed(void* dst, const void* src, size_t size)
{
    size_t srci = (size_t)src;
    size_t dsti = (size_t)dst;
    if (likely(size == 8 && !(srci & 7) && !(dsti & 7))) {
        *(uint64_t*)dst = *(const uint64_t*)src;
    } else if (likely(size == 4 && !(srci & 3) && !(dsti & 3))) {
        *(uint32_t*)dst = *(const uint32_t*)src;
    } else if (likely(size == 2 && !(srci & 1) && !(dsti & 1))) {
        *(uint16_t*)dst = *(const uint16_t*)src;
    } else {
        for (size_t i = 0; i < size; ++i) {
            ((uint8_t*)dst)[i] = ((const uint8_t*)src)[i];
        }
    }
}

slow_path static bool riscv_mmio_unaligned_op(rvvm_mmio_dev_t* dev, void* data, size_t offset, uint8_t size, uint8_t access)
{
    uint8_t tmp[16] = {0};
    size_t align = (size < dev->min_op_size) ? dev->min_op_size :
                   ((size > dev->max_op_size) ? dev->max_op_size : size);
    size_t offset_align = offset & ~(size_t)(align - 1);
    size_t offset_diff = offset - offset_align;
    size_t offset_dest = 0, size_dest = 0;

    if (align > sizeof(tmp)) {
        // This should not happen, but a sanity check is always nice
        rvvm_fatal("MMIO realign bounce buffer overflow!");
        return false;
    }

    while (size) {
        // Amount of bytes actually being read/written by this iteration
        size_dest = (align - offset_diff > size) ? size : (align - offset_diff);
        if (access != RISCV_MMU_WRITE || offset_diff != 0 || size_dest != align) {
            // Either this is a read operation, or an RMW due to misaligned write
            if (dev->read == NULL || !dev->read(dev, tmp, offset_align, align)) return false;
        }

        if (access == RISCV_MMU_WRITE) {
            // Carry the changed bytes in the RMW operation, write back to the device
            memcpy(tmp + offset_diff, ((uint8_t*)data) + offset_dest, size_dest);
            if (dev->write == NULL || !dev->write(dev, tmp, offset_align, align)) return false;
        } else {
            // Copy the read bytes from the aligned buffer
            memcpy(((uint8_t*)data) + offset_dest, tmp + offset_diff, size_dest);
        }

        // Advance the pointers
        offset_dest += size_dest;
        offset_align += align;
        size -= size_dest;
        // First iteration always takes care of offset diff
        offset_diff = 0;
    }
    return true;
}

// Receives any operation on physical address space out of RAM region
static bool riscv_mmio_scan(rvvm_hart_t* vm, rvvm_addr_t vaddr, rvvm_addr_t paddr, void* data, uint8_t size, uint8_t access)
{
    vector_foreach(vm->machine->mmio_devs, i) {
        rvvm_mmio_dev_t* mmio = vector_at(vm->machine->mmio_devs, i);
        if (mmio->addr <= paddr && paddr < (mmio->addr + mmio->size)) {
            // Found the device, access lies in range
            rvvm_mmio_handler_t rwfunc = (access == RISCV_MMU_WRITE) ? mmio->write : mmio->read;
            size_t offset = paddr - mmio->addr;

            if (likely(rwfunc)) {
                if (likely(size <= mmio->max_op_size && size >= mmio->min_op_size && !(offset & (size - 1)))) {
                    // Operation size / alignment validated
                    rwfunc(mmio, data, offset, size);
                } else {
                    // Misaligned or poorly sized operation, attempt fixup
                    rvvm_debug("Misaligned MMIO access to %s at %x, size %x",
                               (mmio->type ? mmio->type->name : NULL), (uint32_t)offset, size);
                    riscv_mmio_unaligned_op(mmio, data, offset, size, access);
                }
            } else if (unlikely(!mmio->mapping)) {
                // Read zeroes
                rvvm_mmio_none(mmio, data, offset, size);
            }

            if (mmio->mapping) {
                // This is a direct memory region, cache translation in TLB if possible
                rvvm_addr_t paddr_align = (paddr & RISCV_PAGE_PNMASK);
                if (likely(mmio->addr <= paddr_align && (paddr_align + RISCV_PAGE_SIZE) <= (mmio->addr + mmio->size))) {
                    riscv_tlb_put(vm, vaddr, ((uint8_t*)mmio->mapping) + offset, access);
                }

                // Copy the data over
                if (access == RISCV_MMU_WRITE) {
                    atomic_memcpy_relaxed(((uint8_t*)mmio->mapping) + offset, data, size);
                } else {
                    atomic_memcpy_relaxed(data, ((uint8_t*)mmio->mapping) + offset, size);
                }
            }

            return true;
        }
    }

    return false;
}

static forceinline bool riscv_block_in_page(rvvm_addr_t addr, size_t size)
{
    return (addr & RISCV_PAGE_MASK) + size <= RISCV_PAGE_SIZE;
}

static void riscv_mmu_misalign(rvvm_hart_t* vm, rvvm_addr_t vaddr, uint32_t attr)
{
    if (!(attr & RISCV_MMU_ATTR_NO_TRAP)) {
        uint8_t access = attr & 0xFF;
        uint32_t cause = RISCV_TRAP_INSN_MISALIGN;
        if (access == RISCV_MMU_READ) cause = RISCV_TRAP_LOAD_MISALIGN;
        if (access == RISCV_MMU_WRITE) cause = RISCV_TRAP_STORE_MISALIGN;
        riscv_trap(vm, cause, vaddr);
    }
}

static void riscv_mmu_phys_fault(rvvm_hart_t* vm, rvvm_addr_t vaddr, uint32_t attr)
{
    if (!(attr & RISCV_MMU_ATTR_NO_TRAP)) {
        uint8_t access = attr & 0xFF;
        uint32_t cause = RISCV_TRAP_INSN_FETCH;
        if (access == RISCV_MMU_READ) cause = RISCV_TRAP_LOAD_FAULT;
        if (access == RISCV_MMU_WRITE) cause = RISCV_TRAP_STORE_FAULT;
        riscv_trap(vm, cause, vaddr);
    }
}

static void riscv_mmu_pagefault(rvvm_hart_t* vm, rvvm_addr_t vaddr, uint32_t attr)
{
    if (!(attr & RISCV_MMU_ATTR_NO_TRAP)) {
        uint8_t access = attr & 0xFF;
        uint32_t cause = RISCV_TRAP_INSN_PAGEFAULT;
        if (access == RISCV_MMU_READ) cause = RISCV_TRAP_LOAD_PAGEFAULT;
        if (access == RISCV_MMU_WRITE) cause = RISCV_TRAP_STORE_PAGEFAULT;
        riscv_trap(vm, cause, vaddr);
    }
}

cold_path void* riscv_mmu_op_internal(rvvm_hart_t* vm, rvvm_addr_t vaddr, void* data, uint32_t attr)
{
    rvvm_addr_t paddr = 0;
    uint8_t access = attr & 0xFF;
    uint8_t mmu_access = access;
    size_t size = attr >> 16;

    if (unlikely(!riscv_block_in_page(vaddr, size))) {
        if (attr & RISCV_MMU_ATTR_PTR) {
            // Can't get pointer to data spanning multiple pages
            riscv_mmu_misalign(vm, vaddr, attr);
            return NULL;
        } else {
            // Emulate misaligned load/store spanning multiple pages
            uint8_t part_size = RISCV_PAGE_SIZE - (vaddr & RISCV_PAGE_MASK);
            if (!riscv_mmu_op_helper(vm, vaddr, data, attr, part_size, access)) {
                return NULL;
            }
            return riscv_mmu_op_helper(vm, vaddr + part_size, ((uint8_t*)data) + part_size, attr, size - part_size, access);
        }
    }

    if (unlikely(attr & RISCV_MMU_ATTR_NO_PROT)) {
        mmu_access = RISCV_MMU_DEBUG_ACCESS;
    }

    if (likely(riscv_mmu_translate(vm, vaddr, &paddr, mmu_access))) {
        if (unlikely(attr & RISCV_MMU_ATTR_PHYS)) {
            // Physical translation requested
            *(rvvm_addr_t*)data = paddr;
            return data;
        }

        void* ptr = riscv_phys_access(vm, paddr);
        if (likely(ptr)) {
            // Physical address in main memory, cache address translation
            riscv_tlb_put(vm, vaddr, ptr, access);
            if (likely(!(attr & RISCV_MMU_ATTR_PTR))) {
                // Perform actual load/store on translated pointer
                if (access == RISCV_MMU_WRITE) {
                    atomic_memcpy_relaxed(ptr, data, size);
                } else {
                    atomic_memcpy_relaxed(data, ptr, size);
                }
            }
            if (access == RISCV_MMU_WRITE) {
                // Clear JITted blocks & flush trace cache if necessary
                riscv_jit_mark_dirty_mem(vm->machine, paddr, 8);
            }
            return ptr;
        }

        if (likely(data)) {
            // Attempt to access MMIO
            uint8_t mmio_access = (attr & RISCV_MMU_ATTR_PTR) ? RISCV_MMU_READ : access;
            if (riscv_mmio_scan(vm, vaddr, paddr, data, size, mmio_access)) {
                return data;
            }
        }

        if (!(paddr & ~SV64_PHYS_MASK)) {
            // Ignore writes and read zeroes on unmapped 56-bit physical addresses
            // Experimentally confirmed this actually happens on real hardware (VF2)
            if (data) {
                memset(data, 0, size);
            }
            return data;
        }

        // Physical memory access fault (Physical address outside canonic 56-bit space)
        riscv_mmu_phys_fault(vm, vaddr, attr);
    } else {
        // Pagefault (No translation for virtual address or protection fault)
        riscv_mmu_pagefault(vm, vaddr, attr);
    }
    return NULL;
}
