/*
vector.h - Vector Container
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

#ifndef VECTOR_H
#define VECTOR_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "utils.h"

#define vector_t(type) struct {type* data; size_t size; size_t count;}

// Empty vectors do not preallocate memory
// This allows static initialization & conserves memory
#define VECTOR_INIT {0}

// Grow factor: 1.5 (Better memory reusage), initial capacity: 2
#define VECTOR_GROW(vec) \
    if ((vec).count >= (vec).size) { \
        if ((vec).size < 2) (vec).size = 2; \
        else (vec).size += (vec).size >> 1; \
        (vec).data = safe_realloc((vec).data, (vec).size * sizeof(*(vec).data)); \
    }

#define vector_init(vec) \
do { \
    (vec).data = NULL; \
    (vec).size = 0; \
    (vec).count = 0; \
} while(0)

// May be called multiple times, the vector is empty yet reusable afterwards
// Semantically identical to clear(), but also frees memory
#define vector_free(vec) \
do { \
    free((vec).data); \
    (vec).data = NULL; \
    (vec).size = 0; \
    (vec).count = 0; \
} while(0)

#define vector_clear(vec) do { (vec).count = 0; } while(0)

#define vector_size(vec) (vec).count

#define vector_capacity(vec) (vec).size

#define vector_at(vec, pos) (vec).data[pos]

#define vector_push_back(vec, val) \
do { \
    VECTOR_GROW(vec); \
    (vec).data[(vec).count++] = val; \
} while(0)

#define vector_insert(vec, pos, val) \
do { \
    VECTOR_GROW(vec); \
    for (size_t _vec_i=(vec).count; _vec_i>pos; --_vec_i) (vec).data[_vec_i] = (vec).data[_vec_i-1]; \
    (vec).data[pos] = val; \
    (vec).count++; \
} while(0)

#define vector_emplace_back(vec) \
do { \
    VECTOR_GROW(vec); \
    memset(&(vec).data[(vec).count++], 0, sizeof(*(vec).data)); \
} while(0)

#define vector_emplace(vec, pos) \
do { \
    VECTOR_GROW(vec); \
    for (size_t _vec_i=(vec).count; _vec_i>pos; --_vec_i) (vec).data[_vec_i] = (vec).data[_vec_i-1]; \
    memset(&(vec).data[pos], 0, sizeof(*(vec).data)); \
    (vec).count++; \
} while(0)

#define vector_erase(vec, pos) \
do { \
    if (pos < (vec).count) { \
        (vec).count--; \
        for (size_t _vec_i=pos; _vec_i<(vec).count; ++_vec_i) (vec).data[_vec_i] = (vec).data[_vec_i+1]; \
    } \
} while(0)

// Be sure to break loop after vector_erase() since it invalidates forward iterators
#define vector_foreach(vec, iter) \
    for (size_t iter=0; iter<(vec).count; ++iter)

// Iterates the vector in reversed order, which is safe for vector_erase()
#define vector_foreach_back(vec, iter) \
    for (size_t iter=(vec).count; iter--;)
        
#endif
