 /*
vector.h - Vector container
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

#ifndef VECTOR_GROW_FACTOR
#define VECTOR_GROW_FACTOR 1.5
#endif

#define vector_t(type) struct {type* data; size_t size; size_t count;}

#define vector_init(vec) \
{ \
    if (sizeof(*(vec).data) > 8) (vec).size = 2; \
    else (vec).size = 32 / sizeof(*(vec).data); \
    (vec).count = 0; \
    (vec).data = safe_malloc(sizeof(*(vec).data) * (vec).size); \
}

#define vector_free(vec) free((vec).data)

#define vector_size(vec) (vec).count

#define vector_capacity(vec) (vec).size

#define vector_at(vec, pos) (vec).data[pos]

#define vector_clear(vec) \
{ \
    vector_free(vec); \
    vector_init(vec); \
}

#define vector_grow(vec) \
{ \
    if ((vec).count >= (vec).size) { \
        (vec).size *= VECTOR_GROW_FACTOR; \
        (vec).data = safe_realloc((vec).data, (vec).size * sizeof(*(vec).data)); \
    } \
}

#define vector_push_back(vec, val) \
{ \
    vector_grow(vec); \
    (vec).data[(vec).count] = val; \
    (vec).count++; \
}

#define vector_insert(vec, pos, val) \
{ \
    vector_grow(vec); \
    for (size_t i=(vec).count; i>pos; --i) (vec).data[i] = (vec).data[i-1]; \
    (vec).data[pos] = val; \
    (vec).count++; \
}

#define vector_emplace_back(vec) \
{ \
    vector_grow(vec); \
    memset(&(vec).data[(vec).count], 0, sizeof(*(vec).data)); \
    (vec).count++; \
}

#define vector_emplace(vec, pos) \
{ \
    vector_grow(vec); \
    for (size_t i=(vec).count; i>pos; --i) (vec).data[i] = (vec).data[i-1]; \
    memset(&(vec).data[(vec).count], 0, sizeof(*(vec).data)); \
    (vec).count++; \
}

#define vector_foreach(vec, i) \
    for (size_t i=0; i<(vec).count; ++i)

#define vector_erase(vec, pos) \
{ \
    for (size_t i=pos; i<(vec).count-1; ++i) (vec).data[i] = (vec).data[i+1]; \
    (vec).count--; \
    if ((vec).count < (vec).size >> 1) { \
        (vec).size >>= 1; \
        (vec).data = safe_realloc((vec).data, (vec).size * sizeof(*(vec).data)); \
    } \
}

#endif
