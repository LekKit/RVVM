/*
elf_load.c - ELF Loader
Copyright (C) 2023  LekKit <github.com/LekKit>
              2021  cerg2010cerg2010 <github.com/cerg2010cerg2010>

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

#include "elf_load.h"
#include "mem_ops.h"
#include "utils.h"
#include "vma_ops.h"

#define ELF_ET_NONE 0x0
#define ELF_ET_REL  0x1
#define ELF_ET_EXEC 0x2
#define ELF_ET_DYN  0x3

#define ELF_PT_NULL    0x0
#define ELF_PT_LOAD    0x1
#define ELF_PT_DYNAMIC 0x2
#define ELF_PT_INTERP  0x3
#define ELF_PT_NOTE    0x4
#define ELF_PT_SHLIB   0x5
#define ELF_PT_PHDR    0x6
#define ELF_PT_TLS     0x7

#define ELF_PF_X 0x1
#define ELF_PF_W 0x2
#define ELF_PF_R 0x4

// TODO: Handling >64k PHENTs
#define ELF_PN_XNUM 0xFFFF

#define WRAP_ERR(cond, error) \
    if (!(cond)) { \
        rvvm_error(error); \
        return false; \
    }

bool elf_load_file(rvfile_t* file, elf_desc_t* elf)
{
    uint8_t tmp[64];
    WRAP_ERR(file && elf, "Invalid arguments to elf_load_file()");
    WRAP_ERR(rvread(file, tmp, 64, 0) == 64, "Failed to read ELF header");
    WRAP_ERR(read_uint32_le_m(tmp) == 0x464c457F, "Not an ELF file");
    WRAP_ERR(tmp[4] == 1 || tmp[4] == 2, "Invalid ELF class");
    WRAP_ERR(tmp[5] == 1, "Not a little-endian ELF");

    // Parse ELF header
    bool objcopy = !!elf->base;
    bool class64 = (tmp[4] == 2);
    uint16_t elf_type = read_uint16_le_m(tmp + 16);
    uint64_t elf_entry = class64 ? read_uint64_le_m(tmp + 24) : read_uint32_le_m(tmp + 24);
    uint64_t elf_phoff = class64 ? read_uint64_le_m(tmp + 32) : read_uint32_le_m(tmp + 28);
    //uint64_t elf_shoff = class64 ? read_uint64_le_m(tmp + 40) : read_uint32_le_m(tmp + 32);
    size_t   elf_phnsz = class64 ? 56 : 32;
    size_t   elf_phnum = read_uint16_le_m(tmp + (class64 ? 56 : 44));

    elf->entry = elf_entry;
    elf->interp_path = NULL;
    elf->phdr = 0;
    elf->phnum = elf_phnum;

    // Determine lowest / highest virtual address, PHDR address
    uint64_t elf_loaddr = (uint64_t)-1;
    uint64_t elf_hiaddr = 0;
    for (size_t i=0; i<elf_phnum; ++i) {
        uint64_t elf_phent_off = elf_phoff + (elf_phnsz * i);
        WRAP_ERR(rvread(file, tmp, elf_phnsz, elf_phent_off) == elf_phnsz, "Failed to read ELF phent");
        uint32_t p_type = read_uint32_le_m(tmp);
        uint64_t p_vaddr = class64 ? read_uint64_le_m(tmp + 16) : read_uint32_le_m(tmp + 8);
        uint64_t p_memsz = class64 ? read_uint64_le_m(tmp + 40) : read_uint32_le_m(tmp + 20);
        if (p_type == ELF_PT_LOAD || p_type == ELF_PT_PHDR) {
            if (p_vaddr < elf_loaddr) elf_loaddr = p_vaddr;
            if (p_vaddr + p_memsz > elf_hiaddr) elf_hiaddr = p_vaddr + p_memsz;
        }
        if (p_type == ELF_PT_PHDR) elf->phdr = p_vaddr;
    }
    if (elf_loaddr == (uint64_t)-1) elf_loaddr = 0; // No ELF segments

    // Relocate pointers
    if (objcopy) {
        if (elf->entry) elf->entry -= elf_loaddr;
        if (elf->phdr)  elf->phdr  -= elf_loaddr;
    } else {
        // Userland ELF loading
        elf->buf_size = elf_hiaddr - elf_loaddr;
        if (elf_type == ELF_ET_DYN) {
            // Dynamic (PIC) ELF, relocate it
            elf->base = vma_alloc(NULL, elf->buf_size, VMA_NONE);
            WRAP_ERR(elf->base, "Failed to relocate dynamic ELF");
            vma_free(elf->base, elf->buf_size);
        }
        if (elf->entry) elf->entry += (size_t)elf->base;
        if (elf->phdr)  elf->phdr  += (size_t)elf->base;
    }

    for (size_t i=0; i<elf_phnum; ++i) {
        uint64_t elf_phent_off = elf_phoff + (elf_phnsz * i);
        WRAP_ERR(rvread(file, tmp, elf_phnsz, elf_phent_off) == elf_phnsz, "Failed to read ELF phent");
        uint32_t p_type = read_uint32_le_m(tmp);
        uint64_t p_offset = class64 ? read_uint64_le_m(tmp + 8) : read_uint32_le_m(tmp + 4);
        uint64_t p_vaddr = class64 ? read_uint64_le_m(tmp + 16) : read_uint32_le_m(tmp + 8);
        uint64_t p_fsize = class64 ? read_uint64_le_m(tmp + 32) : read_uint32_le_m(tmp + 16);
        uint64_t p_memsz = class64 ? read_uint64_le_m(tmp + 40) : read_uint32_le_m(tmp + 20);
        //uint32_t p_flags = class64 ? read_uint32_le_m(tmp + 4) : read_uint32_le_m(tmp + 24);

        if (p_type == ELF_PT_LOAD || p_type == ELF_PT_PHDR) {
            // Load ELF program segment or PHDR segment
            if (objcopy) {
                p_vaddr -= elf_loaddr;
                WRAP_ERR(p_vaddr + p_memsz <= elf->buf_size, "ELF does not fit in objcopy buffer");
            }
            void* vaddr = ((uint8_t*)elf->base) + p_vaddr;
            if (!objcopy) {
                WRAP_ERR(vma_alloc(vaddr, p_memsz, VMA_RWX | VMA_FIXED) == vaddr, "Failed to allocate ELF VMA");
            }

            WRAP_ERR(rvread(file, vaddr, p_fsize, p_offset) == p_fsize, "Failed to read ELF segment");
        }
        if (p_type == ELF_PT_INTERP && !objcopy && !elf->interp_path) {
            // Get ELF interpreter path
            WRAP_ERR(p_fsize < 1024, "ELF interpreter path is too long");
            elf->interp_path = safe_new_arr(char, p_fsize + 1);
            WRAP_ERR(rvread(file, elf->interp_path, p_fsize, p_offset) == p_fsize, "Failed to read ELF interp_path");
        }
    }

    return true;
}

bool bin_objcopy(rvfile_t* file, void* buffer, size_t size, bool try_elf)
{
    uint8_t mag[4] = {0};
    if (try_elf && rvread(file, mag, 4, 0) == 4 && read_uint32_le_m(mag) == 0x464c457F) {
        elf_desc_t elf = {
            .base = buffer,
            .buf_size = size,
        };
        if (elf_load_file(file, &elf)) return true;
    }
    return rvread(file, buffer, size, 0);
}
