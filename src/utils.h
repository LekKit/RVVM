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

#ifndef RVVM_UTILS_H
#define RVVM_UTILS_H

#include "compiler.h"
#include "atomics.h"
#include <stddef.h>
#include <stdlib.h>
#include <stdbool.h>

#define LOG_ERROR 1
#define LOG_WARN  2
#define LOG_INFO  3

#if GNU_ATTRIBUTE(format)
#define PRINT_FORMAT __attribute__((format(printf, 1, 2)))
#else
#define PRINT_FORMAT
#endif

#if GNU_ATTRIBUTE(returns_nonnull) && GNU_ATTRIBUTE(warn_unused_result) && GNU_ATTRIBUTE(alloc_size)
#define SAFE_MALLOC  __attribute__((returns_nonnull, warn_unused_result, malloc, alloc_size(1)))
#define SAFE_CALLOC  __attribute__((returns_nonnull, warn_unused_result, malloc, alloc_size(1, 2)))
#define SAFE_REALLOC __attribute__((returns_nonnull, warn_unused_result, alloc_size(2)))
#else
#define SAFE_MALLOC
#define SAFE_CALLOC
#define SAFE_REALLOC
#endif

void rvvm_set_loglevel(int level);

// Logging functions (controlled by loglevel)
PRINT_FORMAT void rvvm_info(const char* str, ...);
PRINT_FORMAT void rvvm_warn(const char* str, ...);
PRINT_FORMAT void rvvm_error(const char* str, ...);
PRINT_FORMAT void rvvm_fatal(const char* str, ...); // Aborts the process

// These never return NULL
SAFE_MALLOC void* safe_malloc(size_t size);
SAFE_CALLOC void* safe_calloc(size_t size, size_t n);
SAFE_REALLOC void* safe_realloc(void* ptr, size_t size);

// Safe object allocation with type checking & zeroing
#define safe_new_obj(type) ((type*)safe_calloc(1, sizeof(type)))
#define safe_new_arr(type, size) ((type*)safe_calloc(size, sizeof(type)))

#define default_free(ptr) (free)(ptr)

#define safe_free(ptr) \
do { \
    default_free(ptr); \
    ptr = NULL; \
} while(0)

// Implicitly NULL freed pointer to prevent use-after-free
#define free(ptr) safe_free(ptr)

NOINLINE void do_once_finalize(uint32_t* ticket);

// Run a function only once upon reaching this, for lazy init, etc
#define DO_ONCE(expr) \
do { \
    static uint32_t already_done_once = 0; \
    if (unlikely(atomic_load_uint32_ex(&already_done_once, ATOMIC_RELAXED) != 2)) { \
        if (atomic_cas_uint32(&already_done_once, 0, 1)) { \
            expr; \
            atomic_store_uint32_ex(&already_done_once, 2, ATOMIC_RELEASE); \
        } \
        do_once_finalize(&already_done_once); \
    } \
} while (0)

// Evaluate max/min value
#define EVAL_MAX(a, b) ((a) > (b) ? (a) : (b))
#define EVAL_MIN(a, b) ((a) < (b) ? (a) : (b))

// Compute length of a static array
#define STATIC_ARRAY_SIZE(arr) (sizeof(arr) / sizeof(*(arr)))

static inline size_t align_size_up(size_t x, size_t align)
{
    return (x + (align - 1)) & ~(align - 1);
}

static inline size_t align_size_down(size_t x, size_t align)
{
    return x & ~(align - 1);
}

void call_at_deinit(void (*function)());

// Portable strtol/ltostr replacement
size_t   uint_to_str_base(char* str, size_t size, uint64_t val, uint8_t base);
uint64_t str_to_uint_base(const char* str, size_t* len, uint8_t base);
size_t   int_to_str_base(char* str, size_t size, int64_t val, uint8_t base);
int64_t  str_to_int_base(const char* str, size_t* len, uint8_t base);
size_t   int_to_str_dec(char* str, size_t size, int64_t val);
int64_t  str_to_int_dec(const char* str);

// Global argparser
void rvvm_set_args(int new_argc, const char** new_argv);
bool rvvm_has_arg(const char* arg);
const char* rvvm_getarg(const char* arg);
bool rvvm_getarg_bool(const char* arg);
int rvvm_getarg_int(const char* arg);
uint64_t rvvm_getarg_size(const char* arg);

// Portable & safer string.h replacement
size_t      rvvm_strlen(const char* string);
size_t      rvvm_strnlen(const char* string, size_t size);
bool        rvvm_strcmp(const char* s1, const char* s2);
size_t      rvvm_strlcpy(char* dst, const char* src, size_t size);
const char* rvvm_strfind(const char* string, const char* pattern);

static inline size_t mem_suffix_shift(char suffix)
{
    switch (suffix) {
        case 'k': return 10;
        case 'K': return 10;
        case 'm': return 20;
        case 'M': return 20;
        case 'g': return 30;
        case 'G': return 30;
        default: return 0;
    }
}

// Generate random bytes
void rvvm_randombytes(void* buffer, size_t size);

// Generate random serial number (0-9, A-Z)
void rvvm_randomserial(char* serial, size_t size);

#endif
