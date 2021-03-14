/*
hashmap.h - Open-addressing hashmap implementation
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

/*
* This is intended to speed up some parts of the VM,
* for example MMU regions -> MMU handlers mapping, etc.
* Open-addressing is used because i want the hashmap to be a single
* memory block, preferably allocated at startup - reduces memory fragmentation.
* Also, this allows usage on systems without a memory allocator,
* which may help in the future for code executing in VM.
*/

#ifndef HASHMAP_H
#define HASHMAP_H

#include <stdlib.h>
#include <stdint.h>

#define HASHMAP_MAX_PROBES 8

typedef struct {
    size_t key;
    size_t val;
} hashmap_bucket_t;

/*
* key=0 is treated as unused bucket to reduce memory usage
* (no additional flag), so hashmap stores map[0] values in zkval instead.
* size is actually a bitmask holding lowest 1s to represent encoding space
*/
typedef struct {
    hashmap_bucket_t* buckets;
    size_t zkval;
    size_t size;
} hashmap_t;

void hashmap_init(hashmap_t* map, size_t bits);
void hashmap_destroy(hashmap_t* map);
void hashmap_realloc(hashmap_t* map, size_t key, size_t val);

static inline size_t hashmap_hash(size_t key, size_t size)
{
    return (key + (key >> 12) + (key >> 24)) & size;
}

static inline void hashmap_put(hashmap_t* map, size_t key, size_t val)
{
    if (!key) {
        map->zkval = val;
        return;
    }
    size_t hash = hashmap_hash(key, map->size);
    size_t index;
    for (size_t i=0; i<HASHMAP_MAX_PROBES; ++i) {
        index = (hash + i) & map->size;
        if (!map->buckets[index].key || map->buckets[index].key == key) {
            map->buckets[index].key = key;
            map->buckets[index].val = val;
            return;
        }
    }
    // Near-key space is polluted with colliding entries, reallocate and rehash
    // Puts the new entry as well to simplify the inlined function
    hashmap_realloc(map, key, val);
}

static inline size_t hashmap_get(const hashmap_t* map, size_t key)
{
    if (!key) return map->zkval;
    size_t hash = hashmap_hash(key, map->size);
    size_t index;
    for (size_t i=0; i<HASHMAP_MAX_PROBES; ++i) {
        index = (hash + i) & map->size;
        if (map->buckets[index].key == key) {
            return map->buckets[index].val;
        }
    }
    return 0;
}

static inline void hashmap_remove(hashmap_t* map, size_t key)
{
    if (!key) {
        map->zkval = 0;
        return;
    }
    size_t hash = hashmap_hash(key, map->size);
    size_t index;
    for (size_t i=0; i<HASHMAP_MAX_PROBES; ++i) {
        index = (hash + i) & map->size;
        if (map->buckets[index].key == key) {
            map->buckets[index].key = 0;
            return;
        }
    }
}

#endif
