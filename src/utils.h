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

#define LOG_ERROR 1
#define LOG_WARN  2
#define LOG_INFO  3

void rvvm_set_loglevel(int level);

// Logging functions (controlled by loglevel)
void rvvm_info(const char* str, ...);
void rvvm_warn(const char* str, ...);
void rvvm_error(const char* str, ...);
void rvvm_fatal(const char* str); // Aborts the process

#ifdef GNU_EXTS
#define SAFE_ALLOC __attribute__((malloc, returns_nonnull))
#else
#define SAFE_ALLOC
#endif

// These never return NULL

SAFE_ALLOC void* safe_malloc(size_t size);
SAFE_ALLOC void* safe_calloc(size_t size, size_t n);
SAFE_ALLOC void* safe_realloc(void* ptr, size_t size);

#endif
