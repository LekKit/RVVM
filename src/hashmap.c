/*
hashmap.c - Open-addressing hashmap implementation
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

#include "hashmap.h"

void hashmap_init(hashmap_t* map, size_t size)
{
    if (!size) size = 16;
    map->size = 0;
    map->entries = 0;
    while (map->size < size)
        map->size = (map->size << 1) | 0x1;
    map->buckets = (hashmap_bucket_t*)calloc(map->size+1, sizeof(hashmap_bucket_t));
}

void hashmap_destroy(hashmap_t* map)
{
    free(map->buckets);
}

void hashmap_resize(hashmap_t* map, size_t size)
{
    hashmap_t tmp;
    hashmap_init(&tmp, size);
    hasmap_foreach(map, k, v)
        hashmap_put(&tmp, k, v);
    free(map->buckets);
    map->buckets = tmp.buckets;
    map->size = tmp.size;
}

void hashmap_grow(hashmap_t* map, size_t key, size_t val)
{
    hashmap_resize(map, map->size << 1);
    hashmap_put(map, key, val);
}

void hashmap_shrink(hashmap_t* map)
{
    hashmap_resize(map, map->size >> 2);
}

void hashmap_clear(hashmap_t* map)
{
    hashmap_destroy(map);
    hashmap_init(map, 16);
}
