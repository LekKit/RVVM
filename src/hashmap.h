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

#ifndef HASHMAP_H
#define HASHMAP_H

#include <stdlib.h>
#include <stdint.h>

/*
 * This is the worst-case scenario lookup complexity,
 * only 1/256 of entries may reach this point at all.
 * Setting the value lower may improve worst-case scenario
 * by a slight margin, but increases memory consumption
 * by orders of magnitude.
 */
#define HASHMAP_MAX_PROBES 256
// ((map->size >> 1) & 255)

typedef struct {
    size_t key;
    size_t val;
} hashmap_bucket_t;

/*
* val=0 is treated as unused bucket to reduce memory usage
* (no additional flag), size is actually a bitmask holding
* lowest 1s to represent encoding space
*/
typedef struct {
    hashmap_bucket_t* buckets;
    size_t size;
    size_t entries;
} hashmap_t;

// Hint the expected amount of entries on map creation
void hashmap_init(hashmap_t* map, size_t size);

void hashmap_destroy(hashmap_t* map);
void hashmap_resize(hashmap_t* map, size_t size);
void hashmap_grow(hashmap_t* map, size_t key, size_t val);
void hashmap_shrink(hashmap_t* map);
void hashmap_clear(hashmap_t* map);

static inline size_t hashmap_used_mem(hashmap_t* map)
{
    return (map->size + 1) * sizeof(hashmap_bucket_t);
}

#define hashmap_foreach(map, k, v) \
    for (size_t _i=0, k, v; k=(map)->buckets[_i & (map)->size].key, v=(map)->buckets[_i & (map)->size].val, _i<=(map)->size; ++_i) if (v)

static inline size_t hashmap_hash(size_t k)
{
    k ^= k << 21;
    k ^= k >> 17;
#if (SIZE_MAX > 0xFFFFFFFF)
    k ^= k >> 35;
    k ^= k >> 51;
#endif
    return k;
}

void hashmap_rebalance(hashmap_t* map, size_t index);

static inline void hashmap_put(hashmap_t* map, size_t key, size_t val)
{
    size_t hash = hashmap_hash(key);
    size_t index;
    for (size_t i=0; i<HASHMAP_MAX_PROBES; ++i) {
        index = (hash + i) & map->size;

        if (map->buckets[index].key == key) {
            // The key is already used, change value
            map->buckets[index].val = val;

            if (!val) {
                // Value = 0 means we can clear a bucket
                // Rebalance colliding trailing entries
                hashmap_rebalance(map, index);
                map->entries--;
            }
            return;
        } else if (!map->buckets[index].val && val) {
            // Empty bucket found, the key is unused
            map->entries++;
            map->buckets[index].key = key;
            map->buckets[index].val = val;
            return;
        }
    }
    // Near-key space is polluted with colliding entries, reallocate and rehash
    // Puts the new entry as well to simplify the inlined function
    if (val) hashmap_grow(map, key, val);
}

static inline size_t hashmap_get(const hashmap_t* map, size_t key)
{
    size_t hash = hashmap_hash(key);
    size_t index;
    for (size_t i=0; i<HASHMAP_MAX_PROBES; ++i) {
        index = (hash + i) & map->size;
        if (map->buckets[index].key == key || !map->buckets[index].val) {
            return map->buckets[index].val;
        }
    }
    return 0;
}

static inline void hashmap_remove(hashmap_t* map, size_t key)
{
    // Treat value zero as removed key
    hashmap_put(map, key, 0);
    if (map->entries < (map->size >> 2)) {
        hashmap_shrink(map);
    }
}

#endif
