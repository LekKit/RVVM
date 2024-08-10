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


size_t rvvm_strlen(const char* string)
{
    size_t i = 0;
    while (string[i]) i++;
    return i;
}

size_t rvvm_strnlen(const char* string, size_t size)
{
    size_t i = 0;
    while (i < size && string[i]) i++;
    return i;
}

bool rvvm_strcmp(const char* s1, const char* s2)
{
    size_t i = 0;
    while (s1[i] && s1[i] == s2[i]) i++;
    return s1[i] == s2[i];
}

size_t rvvm_strlcpy(char* dst, const char* src, size_t size)
{
    size_t i = 0;
    while (i + 1 < size && src[i]) {
        dst[i] = src[i];
        i++;
    }
    if (size) dst[i] = 0;
    return i;
}

const char* rvvm_strfind(const char* string, const char* pattern)
{
    while (*string) {
        const char* tmp = string;
        const char* pat = pattern;
        while (*tmp && *tmp == *pat) {
            tmp++;
            pat++;
        }
        if (!(*pat)) return string;
        string++;
    }
    return NULL;
}

void rvvm_set_loglevel(int level)
{
    loglevel = level;
}

static bool log_has_colors()
{
    return getenv("TERM") != NULL;
}

static void log_print(const char* prefix, const char* fmt, va_list args)
{
    char buffer[256] = {0};
    size_t pos = rvvm_strlcpy(buffer, prefix, sizeof(buffer));
    size_t vsp_size = sizeof(buffer) - EVAL_MIN(pos + 6, sizeof(buffer));
    if (vsp_size > 1) {
        int tmp = vsnprintf(buffer + pos, vsp_size, fmt, args);
        if (tmp > 0) pos += EVAL_MIN(vsp_size - 1, (size_t)tmp);
    }
    rvvm_strlcpy(buffer + pos, log_has_colors() ? "\033[0m\n" : "\n", sizeof(buffer) - pos);
    fputs(buffer, stderr);
}

PRINT_FORMAT void rvvm_info(const char* str, ...)
{
    if (loglevel < LOG_INFO) return;
    va_list args;
    va_start(args, str);
    log_print(log_has_colors() ? "\033[33;1mINFO\033[37;1m: " : "INFO: ", str, args);
    va_end(args);
}

PRINT_FORMAT void rvvm_warn(const char* str, ...)
{
    if (loglevel < LOG_WARN) return;
    va_list args;
    va_start(args, str);
    log_print(log_has_colors() ? "\033[31;1mWARN\033[37;1m: " : "WARN: ", str, args);
    va_end(args);
}

PRINT_FORMAT void rvvm_error(const char* str, ...)
{
    if (loglevel < LOG_ERROR) return;
    va_list args;
    va_start(args, str);
    log_print(log_has_colors() ? "\033[31;1mERROR\033[37;1m: " : "ERROR: ", str, args);
    va_end(args);
}

PRINT_FORMAT void rvvm_fatal(const char* str, ...)
{
    va_list args;
    va_start(args, str);
    log_print(log_has_colors() ? "\033[31;1mFATAL\033[37;1m: " : "FATAL: ", str, args);
    va_end(args);
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

NOINLINE void do_once_finalize(uint32_t* ticket)
{
    while (atomic_load_uint32_ex(ticket, ATOMIC_ACQUIRE) != 2) {
        sleep_ms(1);
    }
}

typedef void (*deinit_func_t)();
static vector_t(deinit_func_t) deinit_funcs = {0};
static spinlock_t deinit_lock = {0};
static bool deinit_happened = false;

void call_at_deinit(void (*function)())
{
    bool call_func = false;

    while (!spin_try_lock(&deinit_lock)) sleep_ms(1);
    if (!deinit_happened) {
        vector_push_back(deinit_funcs, function);
    } else {
        call_func = true;
    }
    spin_unlock(&deinit_lock);

    if (call_func) {
        function();
    }
}

static deinit_func_t dequeue_func(void)
{
    deinit_func_t ret = NULL;

    while (!spin_try_lock(&deinit_lock)) sleep_ms(1);
    deinit_happened = true;
    if (vector_size(deinit_funcs) == 0) {
        vector_free(deinit_funcs);
    } else {
        size_t end = vector_size(deinit_funcs) - 1;
        ret = vector_at(deinit_funcs, end);
        vector_erase(deinit_funcs, end);
    }
    spin_unlock(&deinit_lock);
    return ret;
}

GNU_DESTRUCTOR static void full_deinit(void)
{
    rvvm_info("Fully deinitializing librvvm");
    while (true) {
        deinit_func_t func = dequeue_func();
        if (func) {
            func();
        } else {
            break;
        }
    }
}

static inline char digit_symbol(uint32_t val)
{
    if (val < 10) return '0' + val;
    if (val < 36) return 'a' + val - 10;
    return '?';
}

static inline uint32_t digit_value(char digit)
{
    if (digit >= '0' && digit <= '9') return digit - '0';
    if (digit >= 'A' && digit <= 'Z') return digit - 'A' + 10;
    if (digit >= 'a' && digit <= 'z') return digit - 'a' + 10;
    return -1;
}

size_t uint_to_str_base(char* str, size_t size, uint64_t val, uint8_t base)
{
    size_t len = 0;
    if (base >= 2 && base <= 36) do {
        if (len + 1 >= size) {
            len = 0;
            break;
        }
        str[len++] = digit_symbol(val % base);
        val /= base;
    } while (val);
    // Reverse the string
    for (size_t i=0; i<len / 2; ++i) {
        char tmp = str[i];
        str[i] = str[len - i - 1];
        str[len - i - 1] = tmp;
    }
    if (size) str[len] = 0;
    return len;
}

uint64_t str_to_uint_base(const char* str, size_t* len, uint8_t base)
{
    int val = 0;
    size_t size = 0;
    if (base == 0) {
        base = 10;
        if (str[0] == '0') {
            base = 8; // Octal literal
            if (str[1] == 'o' || str[1] == 'O') {
                size = 2;
            } else if (str[1] == 'x' || str[1] == 'X') {
                base = 16; // Hex literal
                size = 2;
            } else if (str[1] == 'b' || str[1] == 'B') {
                base = 2; // Binary literal
                size = 2;
            }
        }
    }
    if (len) len[0] = 0;
    if (base >= 2 && base <= 36) while (digit_value(str[size]) < base) {
        val *= base;
        val += digit_value(str[size++]);
        if (len) len[0] = size;
    }
    return val;
}

size_t int_to_str_base(char* str, size_t size, int64_t val, uint8_t base)
{
    size_t off = (val < 0 && size) ? 1 : 0;
    size_t len = uint_to_str_base(str + off, size - off, off ? -val : val, base);
    if (!len) {
        if (size) str[0] = 0;
    } else if (off) {
        str[0] = '-';
        len += off;
    }
    return len;
}

int64_t str_to_int_base(const char* str, size_t* len, uint8_t base)
{
    bool neg = (str[0] == '-');
    uint64_t val = str_to_uint_base(str + neg, len, base);
    if (neg && len && len[0]) len[0]++;
    return neg ? -val : val;
}

size_t int_to_str_dec(char* str, size_t size, int64_t val)
{
    return int_to_str_base(str, size, val, 10);
}

int64_t str_to_int_dec(const char* str)
{
    return str_to_int_base(str, NULL, 0);
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
