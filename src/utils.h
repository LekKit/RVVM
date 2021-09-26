/*
utils.h - Util functions
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

#ifndef UTILS_H
#define UTILS_H

#include "compiler.h"
#include <stddef.h>
#include <stdarg.h>
#include <stdlib.h>

#define LOG_ERROR 1
#define LOG_WARN  2
#define LOG_INFO  3

#ifdef GNU_EXTS
#define PRINT_FORMAT __attribute__((format(printf, 1, 2)))
#define SAFE_MALLOC  __attribute__((returns_nonnull, warn_unused_result, malloc, alloc_size(1)))
#define SAFE_CALLOC  __attribute__((returns_nonnull, warn_unused_result, malloc, alloc_size(1, 2)))
#define SAFE_REALLOC __attribute__((returns_nonnull, warn_unused_result, alloc_size(2)))
#else
#define PRINT_FORMAT
#define SAFE_MALLOC
#define SAFE_CALLOC
#define SAFE_REALLOC
#endif

void rvvm_set_loglevel(int level);

// Logging functions (controlled by loglevel)
PRINT_FORMAT void rvvm_info(const char* str, ...);
PRINT_FORMAT void rvvm_warn(const char* str, ...);
PRINT_FORMAT void rvvm_error(const char* str, ...);
void rvvm_fatal(const char* str); // Aborts the process

// These never return NULL

SAFE_MALLOC void* safe_malloc(size_t size);
SAFE_CALLOC void* safe_calloc(size_t size, size_t n);
SAFE_REALLOC void* safe_realloc(void* ptr, size_t size);

// Portable itoa/atoi replacement
size_t int_to_str_dec(char* str, size_t size, int val);
int str_to_int_dec(const char* str);

#endif
