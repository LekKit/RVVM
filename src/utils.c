/*
utils.с - Util functions
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

#include "utils.h"
#include "rvtimer.h"
#include "vector.h"
#include "spinlock.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int loglevel = LOG_WARN;

static int argc = 0;
static const char** argv = NULL;


size_t rvvm_strlen(const char* string) {
    size_t size = 0;
    while (string[size]) size++;
    return size;
}

bool rvvm_strcmp(const char* s1, const char* s2) {
    size_t i = 0;
    while (s1[i] && s1[i] == s2[i]) i++;
    return s1[i] == s2[i];
}

void rvvm_set_loglevel(int level)
{
    loglevel = level;
}

PRINT_FORMAT void rvvm_info(const char* str, ...)
{
    if (loglevel < LOG_INFO) return;
    fputs("INFO: ", stdout);
    va_list args;
    va_start(args, str);
    vprintf(str, args);
    va_end(args);
    putchar('\n');
}

PRINT_FORMAT void rvvm_warn(const char* str, ...)
{
    if (loglevel < LOG_WARN) return;
    fputs("WARN: ", stdout);
    va_list args;
    va_start(args, str);
    vprintf(str, args);
    va_end(args);
    putchar('\n');
}

PRINT_FORMAT void rvvm_error(const char* str, ...)
{
    if (loglevel < LOG_ERROR) return;
    fputs("ERROR: ", stdout);
    va_list args;
    va_start(args, str);
    vprintf(str, args);
    va_end(args);
    putchar('\n');
}

void rvvm_fatal(const char* str)
{
    fputs("FATAL: ", stdout);
    puts(str);
    abort();
}

SAFE_MALLOC void* safe_malloc(size_t size)
{
    void* ret = malloc(size);
    if (unlikely(!size)) rvvm_warn("Suspicious 0-byte allocation");
    if (unlikely(ret == NULL)) {
        rvvm_fatal("Out of memory!");
    }
    return ret;
}

SAFE_CALLOC void* safe_calloc(size_t size, size_t n)
{
    void* ret = calloc(size, n);
    if (unlikely(!size || !n)) rvvm_warn("Suspicious 0-byte allocation");
    if (unlikely(ret == NULL)) {
        rvvm_fatal("Out of memory!");
    }
    // Fence zeroing of allocated memory
    atomic_fence_ex(ATOMIC_RELEASE);
    return ret;
}

SAFE_REALLOC void* safe_realloc(void* ptr, size_t size)
{
    void* ret = realloc(ptr, size);
    if (unlikely(!size)) rvvm_warn("Suspicious 0-byte allocation");
    if (unlikely(ret == NULL)) {
        rvvm_fatal("Out of memory!");
    }
    return ret;
}

typedef void (*deinit_func_t)();
static vector_t(uint32_t*) deinit_tickets;
static vector_t(deinit_func_t) deinit_funcs;
static spinlock_t deinit_lock;

NOINLINE void do_once_finalize(uint32_t* ticket, bool claimed)
{
    if (claimed) {
        // Register the DO_ONCE ticket for deinit
        spin_lock(&deinit_lock);
        vector_push_back(deinit_tickets, ticket);
        spin_unlock(&deinit_lock);
    } else while (atomic_load_uint32_ex(ticket, ATOMIC_ACQUIRE) != 2) {
        sleep_ms(1);
    }
}

void call_at_deinit(void (*function)())
{
    spin_lock(&deinit_lock);
    vector_push_back(deinit_funcs, function);
    spin_unlock(&deinit_lock);
}

#ifdef GNU_EXTS
#define DEINIT_ATTR __attribute__((destructor))
#else
#define DEINIT_ATTR
#endif

DEINIT_ATTR void full_deinit()
{
    rvvm_info("Fully deinitializing librvvm");
    spin_lock(&deinit_lock);
    // Reset the DO_ONCE tickets and run destructors
    vector_foreach_back(deinit_tickets, i) {
        atomic_store(vector_at(deinit_tickets, i), 0);
    }
    vector_foreach_back(deinit_funcs, i) {
        vector_at(deinit_funcs, i)();
    }
    vector_free(deinit_tickets);
    vector_free(deinit_funcs);
    spin_unlock(&deinit_lock);
}

size_t int_to_str_dec(char* str, size_t size, int val)
{
    size_t len = 0;
    bool neg = val < 0;
    do {
        if (len + 1 > size) return 0;
        str[len++] = ('0' + (val % 10));
        val /= 10;
    } while (val);

    // Append sign
    if (len + 1 > size) return 0;
    if (neg) str[len++] = '-';
    str[len] = 0;

    // Reverse the string
    for (size_t i=0; i<len / 2; ++i) {
        char tmp = str[i];
        str[i] = str[len - i - 1];
        str[len - i - 1] = tmp;
    }
    return len;
}

int str_to_int_dec(const char* str)
{
    int val = 0;
    bool neg = false;
    if (*str == '-') {
        neg = true;
        str++;
    }
    while (*str >= '0' && *str <= '9') {
        val *= 10;
        val += *str - '0';
        str++;
    }
    if (neg) val *= -1;
    return val;
}

void rvvm_set_args(int new_argc, const char** new_argv)
{
    argc = new_argc;
    argv = new_argv;

    if (rvvm_has_arg("v") || rvvm_has_arg("verbose")) {
        rvvm_set_loglevel(LOG_INFO);
    }
}

bool rvvm_has_arg(const char* arg)
{
    size_t offset, j;
    for (int i = 0; i < argc; i++) {
        if (argv[i] == NULL) return false;
        if (argv[i][0] == '-') {
            offset = (argv[i][1] == '-') ? 2 : 1;
            j = 0;
            while (arg[j]
               && argv[i][j + offset]
               && argv[i][j + offset] != '='
               && argv[i][j + offset] == arg[j]) j++;
            if (arg[j] == 0 && (argv[i][j + offset] == 0 || argv[i][j + offset] == '=')) {
                return true;
            }
        }
    }
    return false;
}

const char* rvvm_getarg(const char* arg)
{
    size_t offset, j;
    for (int i = 0; i < argc; i++) {
        if (argv[i] == NULL) return NULL;
        if (argv[i][0] == '-') {
            offset = (argv[i][1] == '-') ? 2 : 1;
            j = 0;
            while (arg[j]
                && argv[i][j + offset]
                && argv[i][j + offset] != '='
                && argv[i][j + offset] == arg[j]) j++;
            if (arg[j] == 0 && (argv[i][j + offset] == 0 || argv[i][j + offset] == '=')) {
                if (argv[i + 1] != NULL && argv[i + 1][0] != '-') {
                    return argv[i + 1];
                }
            }
            if (argv[i][j + offset] == '=') {
                offset += j+1;
                return argv[i]+offset;
            }
        }
    }
    return NULL;
}

bool rvvm_getarg_bool(const char* arg)
{
    const char* argvalue = rvvm_getarg(arg);
    if (argvalue == NULL) return false;
    if (rvvm_strcmp("on", argvalue)
     || rvvm_strcmp("true", argvalue)
     || rvvm_strcmp("y", argvalue)
     || rvvm_strcmp("1", argvalue)) return true;
    return false;
}

int rvvm_getarg_int(const char* arg)
{
    const char* argvalue = rvvm_getarg(arg);
    if (argvalue == NULL) return 0;
    return str_to_int_dec(argvalue);
}

uint64_t rvvm_getarg_size(const char* arg)
{
    const char* argvalue = rvvm_getarg(arg);
    if (argvalue == NULL) return 0;
    return ((uint64_t)str_to_int_dec(argvalue)) << mem_suffix_shift(argvalue[rvvm_strlen(argvalue)-1]);
}

void rvvm_randombytes(void* buffer, size_t size)
{
    // Xorshift RNG seeded by precise timer
    static uint64_t seed = 0;
    uint8_t* bytes = buffer;
    size_t size_rem = size & 0x7;
    size -= size_rem;
    seed += rvtimer_clocksource(1000000000ULL);
    for (size_t i=0; i<size; i += 8) {
        seed ^= (seed >> 17);
        seed ^= (seed << 21);
        seed ^= (seed << 28);
        seed ^= (seed >> 49);
        memcpy(bytes + i, &seed, 8);
    }
    seed ^= (seed >> 17);
    seed ^= (seed << 21);
    seed ^= (seed << 28);
    seed ^= (seed >> 49);
    memcpy(bytes + size, &seed, size_rem);
}

void rvvm_randomserial(char* serial, size_t size)
{
    rvvm_randombytes(serial, size);
    for (size_t i=0; i<size; ++i) {
        size_t c = ((uint8_t*)serial)[i] % ('Z' - 'A' + 10);
        if (c <= 9) serial[i] = '0' + c;
        else serial[i] = 'A' + c - 10;
    }
}
