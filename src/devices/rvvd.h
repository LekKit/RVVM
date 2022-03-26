/*
rvvd.h - Risc-V Virtual Drive image declaration
Copyright (C) 2022 KotB <github.com/0xCatPKG>

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

#ifndef RVVD_H
#define RVVD_H

#define RVVD_VERSION       0x0
#define RVVD_MIN_VERSION   0x0

#define DISK_TYPE_RVVD     0x1

#define DTYPE_SOLID        0x0
#define DTYPE_OVERLAY      0x1

#define DCOMPESSION_NONE   0x0
#define DCOMPESSION_LZMA   0x1
#define DCOMPESSION_ZSTD   0x2
#define DCOMPESSION_LZO    0x3

#define SECTOR_CACHE_SIZE 512



#include "rvvm_types.h"
#include "vector.h"

#include <stdio.h>

// typedef struct {
//     uint64_t id;
//     uint64_t offset;
//     // uint8_t ref_count;
//
//     // bool is_reference;
//     // bool is_edited;
//
// } rvvd_sector;

typedef struct {
    uint64_t id;
    uint64_t offset;
} sector_cache_entry;

struct rvvd_disk {
    char filename[256];
    struct rvvd_disk* base_disk;
    uint64_t size;
    uint32_t version;

    //Options
    uint16_t compression_type;
    uint16_t disk_type;

    //Sectors
    uint64_t sector_table_size;
    sector_cache_entry sector_cache[SECTOR_CACHE_SIZE];
    // vector_t(sector_cache_entry*) dirty_sectors;

    //For internal usage
    FILE* _fd;

};

// Disk creation moment
int rvvd_init(struct rvvd_disk* disk, const char* filename, uint64_t size); // Creates new disk
int rvvd_init_overlay(struct rvvd_disk* disk, const char* base_filename, const char* filename); // Creates new disk in overlay mode on other disk
int rvvd_init_from_image(struct rvvd_disk* disk, const char* image_filename, const char* filename); // Converts disk image to rvvd disk format

int rvvd_open(struct rvvd_disk* disk, const char* filename);
void rvvd_close(struct rvvd_disk* disk);

//Disk settings
void rvvd_migrate_to_current_version(struct rvvd_disk* disk); // Converts disk in current disk version
void rvvd_convert_to_solid(struct rvvd_disk* disk); // Converts disk overlay in solid disk type
void rvvd_change_compression_type(struct rvvd_disk* disk, int compression_type);
bool rvvd_change_size(struct rvvd_disk* disk, size_t new_size);
void rvvd_deduplicate(struct rvvd_disk* disk);

void blk_open(struct rvvd_disk* disk, const char* filename);
void blk_close(struct rvvd_disk* disk);

void blk_allocate(struct rvvd_disk*, void* data, uint64_t sec_id);

void blk_read(struct rvvd_disk* disk, void* buffer, uint64_t sec_id);
void blk_write(struct rvvd_disk* disk, void* data, uint64_t sec_id);
void blk_trim(struct rvvd_disk* disk, uint64_t sec_id);

void blk_sync(struct rvvd_disk* disk);

//Sector table cache operations
void rvvd_push_sector_cache(struct rvvd_disk* disk, uint64_t sec_id, uint64_t offset);
uint64_t rvvd_get_sector_cache_entry(struct rvvd_disk* disk, uint64_t sec_id);

uint64_t rvvd_sector_get_offset(struct rvvd_disk* disk, uint64_t sec_id);
void rvvd_sector_write(struct rvvd_disk* disk, void* data, uint64_t offset);
void rvvd_sector_read(struct rvvd_disk* disk, void* buffer, uint64_t offset);
void rvvd_sector_read_recursive(struct rvvd_disk* disk, void* buffer, uint64_t sec_id);
void rvvd_reverse_sector_lookup(struct rvvd_disk* disk, uint64_t offset);

#endif
