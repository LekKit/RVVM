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
    map->size = 0;
    while (map->size < size)
        map->size = (map->size << 1) | 0x1;
    map->zkval = 0;
    map->buckets = (hashmap_bucket_t*)calloc(map->size+1, sizeof(hashmap_bucket_t));
}

void hashmap_destroy(hashmap_t* map)
{
    free(map->buckets);
    /*
    
    ВНДРЕЙ ГДЕ АВА С ВАРВИКОМ??
    */
}

void hashmap_realloc(hashmap_t* map, size_t key, size_t val)
{
    hashmap_t tmp;
    tmp.size = (map->size << 1) | 0x1;
    tmp.buckets = (hashmap_bucket_t*)calloc(tmp.size+1, sizeof(hashmap_bucket_t));
    for (size_t i=0; i<map->size+1; ++i) {
        if (map->buckets[i].key) {
            hashmap_put(&tmp, map->buckets[i].key, map->buckets[i].val);
        }
    }
    free(map->buckets);
    map->buckets = tmp.buckets;
    map->size = tmp.size;
    hashmap_put(map, key, val);
}
