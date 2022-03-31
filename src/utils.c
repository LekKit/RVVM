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
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>

static int loglevel;

static int argc;
static const char** argv;


size_t rvvm_strlen(const char* string) {
    int size = 0;
    while (string[size] != '\0') size ++;
    return size;
}

bool rvvm_strcmp(const char* __value1, const char*  __value2) {
    if (rvvm_strlen(__value1) != rvvm_strlen(__value2)) return false;
    for (size_t i = 0; i < rvvm_strlen(__value2); i++) {
        if (__value1[i] != __value2[i]) return false;
    }
    return true;
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
    if (unlikely(ret == NULL)) {
        rvvm_fatal("Out of memory!");
    }
    return ret;
}

SAFE_CALLOC void* safe_calloc(size_t size, size_t n)
{
    void* ret = calloc(size, n);
    if (unlikely(ret == NULL)) {
        rvvm_fatal("Out of memory!");
    }
    return ret;
}

SAFE_REALLOC void* safe_realloc(void* ptr, size_t size)
{
    void* ret = realloc(ptr, size);
    if (unlikely(ret == NULL)) {
        rvvm_fatal("Out of memory!");
    }
    return ret;
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


void rvvm_setargs(int _argc, const char** _argv)
{
    argc = _argc;
    argv = _argv;
}

bool rvvm_has_arg(const char* arg)
{
    for (int i = 0; i < argc; i++) {
        size_t offset = 1;
        if (argv[i] == NULL) continue;
        if (argv[i][0] != '-' ) continue;
        offset = (argv[i][1] == '-') ? 2 : 1;
        size_t j = 0;
        while (arg[j]
               && argv[i][j + offset]
               && argv[i][j + offset] != '='
               && argv[i][j + offset] == arg[j]) j++;
        if (arg[j] == 0 && (argv[i][j + offset] == 0 || argv[i][j + offset] == '=')) return true;
    }
    return false;
}

const char* rvvm_getarg(const char* arg)
{
    for (int i = 0; i < argc; i++) {
        size_t offset = 1;
        if (argv[i] == NULL) continue;
        if (argv[i][0] != '-' ) continue;
        offset = (argv[i][1] == '-') ? 2 : 1;
        size_t j = 0;
        while (arg[j]
               && argv[i][j + offset]
               && argv[i][j + offset] != '='
               && argv[i][j + offset] == arg[j]) j++;
        if (!(arg[j] == 0 && (argv[i][j + offset] == 0 || argv[i][j + offset] == '=')) ) continue;
        if (argv[i][j + offset] == '=') {
            offset += j+1;
            return argv[i]+offset;
        }
        if (argv[i+1] == NULL || argv[i+1][0] == '-') continue;
        i++;
        return argv[i];

    }
    return NULL;
}

bool rvvm_getarg_bool(const char* arg)
{
    const char* argvalue = rvvm_getarg(arg);
    if (argvalue == NULL) return rvvm_has_arg(arg);
    else if (argvalue[0] == 0) return false;
    if (rvvm_strcmp("on", argvalue) ||
       rvvm_strcmp("true", argvalue) ||
       rvvm_strcmp("1", argvalue)) return true;
    return false;
}

size_t rvvm_getarg_int(const char* arg)
{
    const char* argvalue = rvvm_getarg(arg);
    if (argvalue == NULL) return 0;
    return str_to_int_dec(argvalue);
}

uint64_t rvvm_getarg_size(const char* arg)
{
    const char* argvalue = rvvm_getarg(arg);
    // printf("SIZE: %s %c %ld %ld\n", argvalue, argvalue[rvvm_strlen(argvalue)-1],
    //                         str_to_int_dec(argvalue), mem_suffix_shift(argvalue[rvvm_strlen(argvalue)-1]));
    if (argvalue == NULL) return 0;
    return (uint64_t)str_to_int_dec(argvalue) << mem_suffix_shift(argvalue[rvvm_strlen(argvalue)-1]);
}
