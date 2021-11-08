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
