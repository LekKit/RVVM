/*
riscv_mmu.c - RISC-V Memory Mapping Unit
Copyright (C) 2021  LekKit <github.com/LekKit>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "riscv_mmu.h"
#include "riscv_csr.h"
#include "riscv_hart.h"
#include "riscv_cpu.h"
#include "bit_ops.h"
#include "atomics.h"
#include "utils.h"
#include <stdlib.h>
#include <string.h>

#define SV32_VPN_BITS     10
#define SV32_VPN_MASK     0x3FF
#define SV32_PHYS_BITS    34
#define SV32_LEVELS       2

#define SV64_VPN_BITS     9
#define SV64_VPN_MASK     0x1FF
#define SV64_PHYS_BITS    56
#define SV64_PHYS_MASK    bit_mask(SV64_PHYS_BITS)
#define SV39_LEVELS       3
#define SV48_LEVELS       4
#define SV57_LEVELS       5

#ifdef __unix__
#include <sys/mman.h>
#endif

bool riscv_init_ram(rvvm_ram_t* mem, paddr_t begin, paddr_t size)
{
    // Memory boundaries should be always aligned to page size
    if ((begin & PAGE_MASK) || (size & PAGE_MASK)) {
        rvvm_error("Memory boundaries misaligned: 0x%08"PRIxXLEN" - 0x%08"PRIxXLEN, begin, begin+size);
        return false;
    }
#ifdef __unix__
    vmptr_t data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (data != MAP_FAILED) {
#ifdef __linux__
        if (!rvvm_has_arg("no_ksm")) {
            if (madvise(data, size, MADV_MERGEABLE) == -1) {
                rvvm_info("KSM madvise() failed");
            }
        }
        if (!rvvm_has_arg("no_thp") && (size > (256 << 20))) {
            if (madvise(data, size, MADV_HUGEPAGE) == -1) {
                rvvm_info("THP madvise() failed");
            } else {
                rvvm_info("THP enabled");
            }
        }
        
#endif
#else
    vmptr_t data = calloc(size, 1);
    if (data != NULL) {
#endif
        mem->data = data;
        mem->begin = begin;
        mem->size = size;
        return true;
    }
    rvvm_error("Memory allocation failure");
    return false;
}

void riscv_free_ram(rvvm_ram_t* mem)
{
#ifdef __unix__
    munmap(mem->data, mem->size);
#else
    free(mem->data);
#endif
    // Prevent accidental access
    mem->data = NULL;
    mem->begin = 0;
    mem->size = 0;
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

void riscv_tlb_flush_page(rvvm_hart_t* vm, vaddr_t addr)
{
    vaddr_t vpn = (addr >> PAGE_SHIFT);
    // VPN is off by 1, thus invalidating the entry
    vm->tlb[vpn & TLB_MASK].r = vpn - 1;
    vm->tlb[vpn & TLB_MASK].w = vpn - 1;
    vm->tlb[vpn & TLB_MASK].e = vpn - 1;
    riscv_restart_dispatch(vm);
}

static void riscv_tlb_put(rvvm_hart_t* vm, vaddr_t vaddr, vmptr_t ptr, uint8_t op)
{
    vaddr_t vpn = vaddr >> PAGE_SHIFT;
    rvvm_tlb_entry_t* entry = &vm->tlb[vpn & TLB_MASK];
    
    /*
    * Add only requested access bits for correct access/dirty flags
    * implementation. Assume the software does not clear A/D bits without
    * calling SFENCE.VMA
    */
    switch (op) {
        case MMU_READ:
            entry->r = vpn;
            // If same tlb entry contains different VPNs,
            // they should be invalidated
            if (entry->w != vpn) entry->w = vpn - 1;
            if (entry->e != vpn) entry->e = vpn - 1;
            break;
        case MMU_WRITE:
            entry->r = vpn;
            entry->w = vpn;
            if (entry->e != vpn) entry->e = vpn - 1;
            break;
        case MMU_EXEC:
            if (entry->r != vpn) entry->r = vpn - 1;
            //if (entry->w != vpn) entry->w = vpn - 1;
            entry->w = vpn - 1; // W^X on the TLB to track dirtiness
            entry->e = vpn;
            break;
        default:
            // (???) lets just complain and flush the entry
            rvvm_error("Unknown MMU op in riscv_tlb_put");
            entry->r = vpn - 1;
            entry->w = vpn - 1;
            entry->e = vpn - 1;
            break;
    }

    entry->ptr = ((size_t)ptr) - TLB_VADDR(vaddr);
}

// Virtual memory addressing mode (SV32)
static bool riscv_mmu_translate_sv32(rvvm_hart_t* vm, vaddr_t vaddr, paddr_t* paddr, uint8_t priv, uint8_t access)
{
    // Pagetable is always aligned to PAGE_SIZE
    paddr_t pagetable = vm->root_page_table;
    paddr_t pte, pgt_off;
    vmptr_t pte_addr;
    bitcnt_t bit_off = SV32_VPN_BITS + PAGE_SHIFT;

    for (size_t i=0; i<SV32_LEVELS; ++i) {
        pgt_off = ((vaddr >> bit_off) & SV32_VPN_MASK) << 2;
        pte_addr = riscv_phys_translate(vm, pagetable + pgt_off);
        if (pte_addr) {
            pte = read_uint32_le(pte_addr);
            if (pte & MMU_VALID_PTE) {
                if (pte & MMU_LEAF_PTE) {
                    // PGT entry is a leaf, check permissions
                    // Check U bit != priv mode, otherwise do extended check
                    if (!!(pte & MMU_USER_USABLE) == !!priv) {
                        // If we are supervisor with SUM bit set, rw operations are allowed
                        // MXR sets access to MMU_READ | MMU_EXEC
                        if (access == MMU_EXEC ||
                            priv != PRIVILEGE_SUPERVISOR || 
                            (vm->csr.status & CSR_STATUS_SUM) == 0)
                            return false;
                    }
                    // Check access bits & translate
                    if (pte & access) {
                        vaddr_t vmask = bit_mask(bit_off);
                        paddr_t pmask = bit_mask(SV32_PHYS_BITS - bit_off) << bit_off;
                        paddr_t pte_flags = pte | MMU_PAGE_ACCESSED | ((access & MMU_WRITE) << 5);
                        paddr_t pte_shift = pte << 2;
                        // Check that PPN[i-1:0] is 0, otherwise the page is misaligned
                        if (unlikely(pte_shift & vmask & PAGE_PNMASK))
                            return false;
                        // Atomically update A/D flags
                        if (pte != pte_flags) atomic_cas_uint32_le(pte_addr, pte, pte_flags);
                        // Combine ppn & vpn & pgoff
                        *paddr = (pte_shift & pmask) | (vaddr & vmask);
                        return true;
                    }
                } else if ((pte & MMU_WRITE) == 0) {
                    // PGT entry is a pointer to next pagetable
                    pagetable = (pte >> 10) << PAGE_SHIFT;
                    bit_off -= SV32_VPN_BITS;
                    continue;
                }
            }
        }
        // No valid address translation can be done (invalid PTE or protection fault)
        return false;
    }
    return false;
}

#ifdef USE_RV64

// Virtual memory addressing mode (RV64 MMU template)
static bool riscv_mmu_translate_rv64(rvvm_hart_t* vm, vaddr_t vaddr, paddr_t* paddr, uint8_t priv, uint8_t access, uint8_t sv_levels)
{
    // Pagetable is always aligned to PAGE_SIZE
    paddr_t pagetable = vm->root_page_table;
    paddr_t pte, pgt_off;
    vmptr_t pte_addr;
    bitcnt_t bit_off = (sv_levels * SV64_VPN_BITS) + PAGE_SHIFT - SV64_VPN_BITS;
    
    if (unlikely(vaddr != (vaddr_t)sign_extend(vaddr, bit_off+SV64_VPN_BITS)))
        return false;

    for (size_t i=0; i<sv_levels; ++i) {
        pgt_off = ((vaddr >> bit_off) & SV64_VPN_MASK) << 3;
        pte_addr = riscv_phys_translate(vm, pagetable + pgt_off);
        if (pte_addr) {
            pte = read_uint64_le(pte_addr);
            if (pte & MMU_VALID_PTE) {
                if (pte & MMU_LEAF_PTE) {
                    // PGT entry is a leaf, check permissions
                    // Check U bit != priv mode, otherwise do extended check
                    if (!!(pte & MMU_USER_USABLE) == !!priv) {
                        // If we are supervisor with SUM bit set, rw operations are allowed
                        // MXR sets access to MMU_READ | MMU_EXEC
                        if (access == MMU_EXEC ||
                            priv != PRIVILEGE_SUPERVISOR || 
                            (vm->csr.status & CSR_STATUS_SUM) == 0)
                            return false;
                    }
                    // Check access bits & translate
                    if (pte & access) {
                        vaddr_t vmask = bit_mask(bit_off);
                        paddr_t pmask = bit_mask(SV64_PHYS_BITS - bit_off) << bit_off;
                        paddr_t pte_flags = pte | MMU_PAGE_ACCESSED | ((access & MMU_WRITE) << 5);
                        paddr_t pte_shift = pte << 2;
                        // Check that PPN[i-1:0] is 0, otherwise the page is misaligned
                        if (unlikely(pte_shift & vmask & PAGE_PNMASK))
                            return false;
                        // Atomically update A/D flags
                        if (pte != pte_flags) atomic_cas_uint64_le(pte_addr, pte, pte_flags);
                        // Combine ppn & vpn & pgoff
                        *paddr = (pte_shift & pmask) | (vaddr & vmask);
                        return true;
                    }
                } else if ((pte & MMU_WRITE) == 0) {
                    // PGT entry is a pointer to next pagetable
                    pagetable = ((pte >> 10) << PAGE_SHIFT) & SV64_PHYS_MASK;
                    bit_off -= SV64_VPN_BITS;
                    continue;
                }
            }
        }
        // No valid address translation can be done (invalid PTE or protection fault)
        return false;
    }
    return false;
}

#endif

// Translate virtual address to physical with respect to current CPU mode
static inline bool riscv_mmu_translate(rvvm_hart_t* vm, vaddr_t vaddr, paddr_t* paddr, uint8_t access)
{
    uint8_t priv = vm->priv_mode;
    // If MPRV is enabled, and we aren't fetching an instruction,
    // change effective privilege mode to STATUS.MPP
    if ((vm->csr.status & CSR_STATUS_MPRV) && (access != MMU_EXEC)) {
        priv = bit_cut(vm->csr.status, 11, 2);
    }
    // If MXR is enabled, reads from pages marked as executable-only should succeed
    if ((vm->csr.status & CSR_STATUS_MXR) && (access == MMU_READ)) {
        access |= MMU_EXEC;
    }
    if (priv <= PRIVILEGE_SUPERVISOR) {
        switch (vm->mmu_mode) {
            case CSR_SATP_MODE_PHYS:
                *paddr = vaddr;
                return true;
            case CSR_SATP_MODE_SV32:
                return riscv_mmu_translate_sv32(vm, vaddr, paddr, priv, access);
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

static bool riscv_mmio_unaligned_op(rvvm_mmio_dev_t* dev, rvvm_mmio_handler_t rwfunc, void* dest, paddr_t offset, uint8_t size)
{
    if (unlikely(dev->max_op_size < dev->min_op_size)) {
        rvvm_warn("Device \"%s\" has incorrect access properties: min %u, max %u",
                  dev->type ? dev->type->name : "null", dev->min_op_size, dev->max_op_size);
        return false;
    }
    if ((size < dev->min_op_size) || (offset & (dev->min_op_size - 1))) {
        // Operation size smaller than possible or address misaligned
        // Read bigger chunk, then use only part of it
        paddr_t aligned_offset = offset & ~(paddr_t)(dev->min_op_size - 1);
        uint8_t offset_diff = offset - aligned_offset;
        uint8_t new_size = dev->min_op_size;
        uint8_t misaligned_size = size + offset_diff;
        uint8_t tmp[16] = {0};
        if (unlikely(new_size > 8)) {
            rvvm_warn("Device \"%s\" has incorrect min op size: %u",
                  dev->type ? dev->type->name : "null", dev->min_op_size);
            return false;
        }
        while (new_size < misaligned_size) new_size <<= 1;
        if (!riscv_mmio_unaligned_op(dev, dev->read, tmp, aligned_offset, new_size)) return false;
        if (rwfunc == dev->write) {
            if (!riscv_mmio_unaligned_op(dev, rwfunc, tmp, aligned_offset, new_size)) return false;
        } else {
            memcpy(dest, tmp + offset_diff, size);
        }
        return true;
    }
    if (size > dev->max_op_size) {
        // Max operation size exceeded, cut into smaller parts
        uint8_t size_half = size >> 1;
        return riscv_mmio_unaligned_op(dev, rwfunc, dest, offset, size_half) &&
               riscv_mmio_unaligned_op(dev, rwfunc, ((vmptr_t)dest) + size_half, offset + size_half, size_half);
    }
    return rwfunc(dev, dest, offset, size);
}

// Receives any operation on physical address space out of RAM region
static bool riscv_mmio_scan(rvvm_hart_t* vm, vaddr_t vaddr, paddr_t paddr, void* dest, uint8_t size, uint8_t access)
{
    rvvm_mmio_dev_t* dev;
    rvvm_mmio_handler_t rwfunc;
    paddr_t offset;
    
    //rvvm_info("Scanning MMIO at 0x%08"PRIxXLEN, paddr);
    
    vector_foreach(vm->machine->mmio, i) {
        dev = &vector_at(vm->machine->mmio, i);
        if (paddr >= dev->addr && paddr < (dev->addr + dev->size)) {
            //rvvm_info("Hart %p accessing MMIO at 0x%08x", vm, paddr);
            // Found the device
            offset = paddr - dev->addr;
            if (access == MMU_WRITE) {
                rwfunc = dev->write;
            } else {
                rwfunc = dev->read;
            }
            
            if (rwfunc == NULL) {
                // Missing handler, this is a direct memory region
                // Copy the data, cache translation in TLB if possible
                if (access == MMU_WRITE) {
                    memcpy(((vmptr_t)dev->data) + offset, dest, size);
                } else {
                    memcpy(dest, ((vmptr_t)dev->data) + offset, size);
                }
                if ((offset >= PAGE_SIZE || riscv_block_aligned(dev->addr, PAGE_SIZE)) &&
                    (dev->size - offset  >= PAGE_SIZE || riscv_block_aligned(dev->addr + dev->size, PAGE_SIZE))) {
                    riscv_tlb_put(vm, vaddr, ((vmptr_t)dev->data) + offset, access);
                }
                return true;
            }
            
            if (unlikely(size > dev->max_op_size || size < dev->min_op_size || (offset & (dev->min_op_size-1)))) {
                //rvvm_info("Hart %p accessing unaligned MMIO at 0x%08"PRIxXLEN, vm, paddr);
                return riscv_mmio_unaligned_op(dev, rwfunc, dest, offset, size);
            }
            return rwfunc(dev, dest, offset, size);
        }
    }
    
    return false;
}

/*
 * Since aligned loads/stores expect relaxed atomicity, MMU should use this
 * instead of a regular memcpy, to prevent other harts from observing
 * half-made memory operation on TLB miss
 */
TSAN_SUPPRESS static inline void atomic_memcpy_relaxed(void* dest, const void* src, uint8_t size)
{
    if (likely(((((size_t)src) & (size-1)) == 0) && ((((size_t)dest) & (size-1)) == 0))) {
        switch(size) {
#ifdef USE_RV64
            case 8:
                *(uint64_t*)dest = *(const uint64_t*)src;
                return;
#endif
            case 4:
                *(uint32_t*)dest = *(const uint32_t*)src;
                return;
            case 2:
                *(uint16_t*)dest = *(const uint16_t*)src;
                return;
        }
    }
    for (uint8_t i=0; i<size; ++i) {
        ((uint8_t*)dest)[i] = ((const uint8_t*)src)[i];
    }
}

static bool riscv_mmu_op(rvvm_hart_t* vm, vaddr_t addr, void* dest, uint8_t size, uint8_t access)
{
    //rvvm_info("Hart %p tlb miss at 0x%08"PRIxXLEN, vm, addr);
    paddr_t paddr;
    vmptr_t ptr;
    uint32_t trap_cause;

    // Handle misalign between pages
    if (!riscv_block_in_page(addr, size)) {
        // Prevent recursive faults by checking return flag
        uint8_t part_size = PAGE_SIZE - (addr & PAGE_MASK);
        return riscv_mmu_op(vm, addr, dest, part_size, access) &&
               riscv_mmu_op(vm, addr + part_size, ((vmptr_t)dest) + part_size, size - part_size, access);
    }

    if (riscv_mmu_translate(vm, addr, &paddr, access)) {
        //rvvm_info("Hart %p accessing physmem at 0x%08x", vm, paddr);
        ptr = riscv_phys_translate(vm, paddr);
        if (ptr) {
            // Physical address in main memory, cache address translation
            riscv_tlb_put(vm, addr, ptr, access);
            if (access == MMU_WRITE) {
                // Clear JITted blocks & flush trace cache if necessary
                riscv_jit_mark_dirty_mem(vm->machine, paddr, size);
                // Should we make this atomic? RVWMO expects ld/st atomicity
                //memcpy(ptr, dest, size);
                atomic_memcpy_relaxed(ptr, dest, size);
            } else {
                //memcpy(dest, ptr, size);
                atomic_memcpy_relaxed(dest, ptr, size);
            }
            return true;
        }
        // Physical address not in memory region, check MMIO
        if (riscv_mmio_scan(vm, addr, paddr, dest, size, access)) {
            return true;
        }
        // Physical memory access fault (bad physical address)
        switch (access) {
            case MMU_WRITE:
                trap_cause = TRAP_STORE_FAULT;
                break;
            case MMU_READ:
                trap_cause = TRAP_LOAD_FAULT;
                break;
            case MMU_EXEC:
                trap_cause = TRAP_INSTR_FETCH;
                break;
            default:
                rvvm_error("Unknown MMU op in riscv_mmu_op (phys)");
                trap_cause = 0;
                break;
        }
    } else {
        // Pagefault (no translation for address or protection fault)
        switch (access) {
            case MMU_WRITE:
                trap_cause = TRAP_STORE_PAGEFAULT;
                break;
            case MMU_READ:
                trap_cause = TRAP_LOAD_PAGEFAULT;
                break;
            case MMU_EXEC:
                trap_cause = TRAP_INSTR_PAGEFAULT;
                break;
            default:
                rvvm_error("Unknown MMU op in riscv_mmu_op (page)");
                trap_cause = 0;
                break;
        }
    }
    // Trap the CPU & instruct the caller to discard operation
    riscv_trap(vm, trap_cause, addr);
    return false;
}

/*
 * Non-inlined slow memory operations, perform MMU translation,
 * call MMIO handlers if needed.
 */

vmptr_t riscv_mmu_vma_translate(rvvm_hart_t* vm, vaddr_t addr, uint8_t access)
{
    //rvvm_info("Hart %p vma tlb miss at 0x%08"PRIxXLEN, vm, addr);
    paddr_t paddr;
    vmptr_t ptr;
    uint32_t trap_cause;
    
    if (riscv_mmu_translate(vm, addr, &paddr, access)) {
        ptr = riscv_phys_translate(vm, paddr);
        if (ptr) {
            if (access == MMU_WRITE) {
                // Clear JITted blocks & flush trace cache if necessary
                riscv_jit_mark_dirty_mem(vm->machine, paddr, 8);
            }
            // Physical address in main memory, cache address translation
            riscv_tlb_put(vm, addr, ptr, access);
            return ptr;
        }
        // Physical memory access fault (bad physical address)
        switch (access) {
            case MMU_WRITE:
                trap_cause = TRAP_STORE_FAULT;
                break;
            case MMU_READ:
                trap_cause = TRAP_LOAD_FAULT;
                break;
            case MMU_EXEC:
                trap_cause = TRAP_INSTR_FETCH;
                break;
            default:
                rvvm_error("Unknown MMU op in riscv_mmu_op (phys)");
                trap_cause = 0;
                break;
        }
    } else {
        // Pagefault (no translation for address or protection fault)
        switch (access) {
            case MMU_WRITE:
                trap_cause = TRAP_STORE_PAGEFAULT;
                break;
            case MMU_READ:
                trap_cause = TRAP_LOAD_PAGEFAULT;
                break;
            case MMU_EXEC:
                trap_cause = TRAP_INSTR_PAGEFAULT;
                break;
            default:
                rvvm_error("Unknown MMU op in riscv_mmu_op (page)");
                trap_cause = 0;
                break;
        }
    }
    // Trap the CPU & instruct the caller to discard operation
    riscv_trap(vm, trap_cause, addr);
    return NULL;
}

bool riscv_mmu_fetch_inst(rvvm_hart_t* vm, vaddr_t addr, uint32_t* inst)
{
    uint8_t buff[4] = {0};
    if (!riscv_block_in_page(addr, 4)) {
        if (!riscv_mmu_op(vm, addr, buff, 2, MMU_EXEC)) return false;
        if ((buff[0] & 0x3) == 0x3) {
            // This is a 4-byte instruction scattered between pages
            // Fetch second part (may trigger a pagefault, that's the point)
            if (!riscv_mmu_op(vm, addr + 2, buff + 2, 2, MMU_EXEC)) return false;
        }
        *inst = read_uint32_le_m(buff);
        return true;
    }
    
    if (riscv_mmu_op(vm, addr, buff, 4, MMU_EXEC)) {
        *inst = read_uint32_le_m(buff);
        return true;
    } else {
        return false;
    }
}

void riscv_mmu_load_u64(rvvm_hart_t* vm, vaddr_t addr, regid_t reg)
{
    uint8_t buff[8];
    if (riscv_mmu_op(vm, addr, buff, 8, MMU_READ)) {
        vm->registers[reg] = read_uint64_le_m(buff);
    }
}

void riscv_mmu_load_u32(rvvm_hart_t* vm, vaddr_t addr, regid_t reg)
{
    uint8_t buff[4];
    if (riscv_mmu_op(vm, addr, buff, 4, MMU_READ)) {
        vm->registers[reg] = read_uint32_le_m(buff);
    }
}

void riscv_mmu_load_s32(rvvm_hart_t* vm, vaddr_t addr, regid_t reg)
{
    uint8_t buff[4];
    if (riscv_mmu_op(vm, addr, buff, 4, MMU_READ)) {
        vm->registers[reg] = (int32_t)read_uint32_le_m(buff);
    }
}

void riscv_mmu_load_u16(rvvm_hart_t* vm, vaddr_t addr, regid_t reg)
{
    uint8_t buff[2];
    if (riscv_mmu_op(vm, addr, buff, 2, MMU_READ)) {
        vm->registers[reg] = read_uint16_le_m(buff);
    }
}

void riscv_mmu_load_s16(rvvm_hart_t* vm, vaddr_t addr, regid_t reg)
{
    uint8_t buff[2];
    if (riscv_mmu_op(vm, addr, buff, 2, MMU_READ)) {
        vm->registers[reg] = (int16_t)read_uint16_le_m(buff);
    }
}

void riscv_mmu_load_u8(rvvm_hart_t* vm, vaddr_t addr, regid_t reg)
{
    uint8_t buff[1];
    if (riscv_mmu_op(vm, addr, buff, 1, MMU_READ)) {
        vm->registers[reg] = buff[0];
    }
}

void riscv_mmu_load_s8(rvvm_hart_t* vm, vaddr_t addr, regid_t reg)
{
    uint8_t buff[1];
    if (riscv_mmu_op(vm, addr, buff, 1, MMU_READ)) {
        vm->registers[reg] = (int8_t)buff[0];
    }
}



void riscv_mmu_store_u64(rvvm_hart_t* vm, vaddr_t addr, regid_t reg)
{
    uint8_t buff[8];
    write_uint64_le_m(buff, vm->registers[reg]);
    riscv_mmu_op(vm, addr, buff, 8, MMU_WRITE);
}

void riscv_mmu_store_u32(rvvm_hart_t* vm, vaddr_t addr, regid_t reg)
{
    uint8_t buff[4];
    write_uint32_le_m(buff, vm->registers[reg]);
    riscv_mmu_op(vm, addr, buff, 4, MMU_WRITE);
}

void riscv_mmu_store_u16(rvvm_hart_t* vm, vaddr_t addr, regid_t reg)
{
    uint8_t buff[2];
    write_uint16_le_m(buff, vm->registers[reg]);
    riscv_mmu_op(vm, addr, buff, 2, MMU_WRITE);
}

void riscv_mmu_store_u8(rvvm_hart_t* vm, vaddr_t addr, regid_t reg)
{
    uint8_t buff[1];
    buff[0] = vm->registers[reg];
    riscv_mmu_op(vm, addr, buff, 1, MMU_WRITE);
}



#ifdef USE_FPU

void riscv_mmu_load_double(rvvm_hart_t* vm, vaddr_t addr, regid_t reg)
{
    uint8_t buff[8];
    if (riscv_mmu_op(vm, addr, buff, 8, MMU_READ)) {
        vm->fpu_registers[reg] = read_double_le_m(buff);
        fpu_set_fs(vm, FS_DIRTY);
    }
}

void riscv_mmu_load_float(rvvm_hart_t* vm, vaddr_t addr, regid_t reg)
{
    uint8_t buff[4];
    if (riscv_mmu_op(vm, addr, buff, 4, MMU_READ)) {
        write_float_nanbox(&vm->fpu_registers[reg], read_float_le_m(buff));
        fpu_set_fs(vm, FS_DIRTY);
    }
}



void riscv_mmu_store_double(rvvm_hart_t* vm, vaddr_t addr, regid_t reg)
{
    uint8_t buff[8];
    write_double_le_m(buff, vm->fpu_registers[reg]);
    riscv_mmu_op(vm, addr, buff, 8, MMU_WRITE);
}

void riscv_mmu_store_float(rvvm_hart_t* vm, vaddr_t addr, regid_t reg)
{
    uint8_t buff[4];
    write_float_le_m(buff, read_float_nanbox(&vm->fpu_registers[reg]));
    riscv_mmu_op(vm, addr, buff, 4, MMU_WRITE);
}

#endif
