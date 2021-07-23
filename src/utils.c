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
#include <stdlib.h>
#include <stdio.h>

static int loglevel;

void rvvm_set_loglevel(int level)
{
    loglevel = level;
}

void rvvm_info(const char* str, ...)
{
    if (loglevel < LOG_INFO) return;
    fputs("INFO: ", stdout);
    va_list args;
    va_start(args, str);
    vprintf(str, args);
    va_end(args);
    putchar('\n');
}

void rvvm_warn(const char* str, ...)
{
    if (loglevel < LOG_WARN) return;
    fputs("WARN: ", stdout);
    va_list args;
    va_start(args, str);
    vprintf(str, args);
    va_end(args);
    putchar('\n');
}

void rvvm_error(const char* str, ...)
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
