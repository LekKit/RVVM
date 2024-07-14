/*
vma_ops.h - Virtual memory area operations
Copyright (C) 2023  LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.

Alternatively, the contents of this file may be used under the terms
of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or any later version.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef VMA_OPS_H
#define VMA_OPS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define VMA_NONE  0x0
#define VMA_EXEC  0x1
#define VMA_WRITE 0x2
#define VMA_READ  0x4
#define VMA_RDWR  (VMA_READ | VMA_WRITE)
#define VMA_RDEX  (VMA_READ | VMA_EXEC)
#define VMA_RWX   (VMA_READ | VMA_WRITE | VMA_EXEC)

#define VMA_FIXED 0x8  // Forcefully map into occupied zone
#define VMA_THP   0x10 // Transparent hugepages
#define VMA_KSM   0x20 // Kernel same-page merging

// Get host page size
size_t vma_page_size();

// Allocate VMA, force needed address using VMA_FIXED
void* vma_alloc(void* addr, size_t size, uint32_t flags);

// Create separate RW/exec VMAs (For W^X JIT)
bool  vma_multi_mmap(void** rw, void** exec, size_t size);

// Resize VMA, pass VMA_FIXED to make sure it stays in place
void* vma_remap(void* addr, size_t old_size, size_t new_size, uint32_t flags);

// Change VMA protection flags
bool  vma_protect(void* addr, size_t size, uint32_t flags);

// Hint to free (zero-fill) underlying memory, VMA is still intact
bool  vma_clean(void* addr, size_t size, bool lazy);

// Hint to pageout memory, data is kept intact
bool  vma_pageout(void* addr, size_t size, bool lazy);

// Unmap the VMA
bool  vma_free(void* addr, size_t size);

#endif
