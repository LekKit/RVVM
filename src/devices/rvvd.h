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

#define RVVD_VERSION       0x1
#define RVVD_MIN_VERSION   0x1

#define DOPT_OVERLAY       0x1

#define DCOMPESSION_NONE   0x0
#define DCOMPESSION_LZMA   0x1
#define DCOMPESSION_ZSTD   0x2
#define DCOMPESSION_LZO    0x3

#define SECTOR_CACHE_SIZE 512



#include "rvvm_types.h"
// #include "vector.h"
#include "blk_dev.h"

#include <stdio.h>

typedef struct {
    uint64_t id;
    uint64_t offset;
} sector_cache_entry;

struct rvvd_dev {
    // blk functions api
    blk_dev* blk_dev;

    char filename[256];
    struct rvvd_dev* base_disk;
    uint64_t size;
    uint32_t version;

    //Options
    uint16_t compression_type;
    bool overlay;

    //Sectors
    uint64_t sector_table_size;
    sector_cache_entry sector_cache[SECTOR_CACHE_SIZE];
    uint64_t next_sector_offset;
    // vector_t(sector_cache_entry*) dirty_sectors;

    //For internal usage
    FILE* _fd;

};

blk_dev* rvvd_dev(const char* filename);

// Disk creation moment
int rvvd_init(struct rvvd_dev* disk, const char* filename, uint64_t size); // Creates new disk
int rvvd_init_overlay(struct rvvd_dev* disk, const char* base_filename, const char* filename); // Creates new disk in overlay mode on other disk
int rvvd_init_from_image(struct rvvd_dev* disk, const char* image_filename, const char* filename); // Converts disk image to rvvd disk format

int rvvd_open(struct rvvd_dev* disk, const char* filename);
void rvvd_close(struct rvvd_dev* disk);

// Disk settings
void rvvd_migrate_to_current_version(struct rvvd_dev* disk); // Converts disk in current disk version
void rvvd_convert_to_solid(struct rvvd_dev* disk); // Converts disk overlay in solid disk type
void rvvd_change_compression_type(struct rvvd_dev* disk, int compression_type);
bool rvvd_change_size(struct rvvd_dev* disk, size_t new_size);
void rvvd_deduplicate(struct rvvd_dev* disk);
void rvvd_dump_to_image(struct rvvd_dev* disk, const char* filename);

// Blk API
void blk_open(void* drive);
void blk_close(void* drive);
void blk_allocate(void* drive, void* data, uint64_t sector_id);
void blk_read(void* drive, void* buffer, uint64_t offset, uint32_t len);
void blk_write(void* drive, void* data, uint64_t offset, uint32_t len);
void blk_trim(void* drive, uint64_t offset, uint32_t len);
void blk_sync(void* drive);
size_t blk_size(void* drive);

// RVVD API
void rvvd_allocate(struct rvvd_dev* disk, void* data, uint64_t sec_id);
void rvvd_read(struct rvvd_dev* disk, void* buffer, uint64_t sec_id);
void rvvd_write(struct rvvd_dev* disk, void* data, uint64_t sec_id);
void rvvd_trim(struct rvvd_dev* disk, uint64_t sec_id);
void rvvd_sync(struct rvvd_dev* disk);

//Sector table cache operations
void rvvd_sc_push(struct rvvd_dev* disk, uint64_t sec_id, uint64_t offset);
void rvvd_sc_forward_predict(struct rvvd_dev* disk, uint64_t from_sector, uint32_t sector_count);
uint64_t rvvd_sc_get(struct rvvd_dev* disk, uint64_t sec_id);

uint64_t rvvd_sector_get_offset(struct rvvd_dev* disk, uint64_t sec_id);
void rvvd_sector_write(struct rvvd_dev* disk, void* data, uint64_t offset);
void rvvd_sector_read(struct rvvd_dev* disk, void* buffer, uint64_t offset);
void rvvd_sector_read_recursive(struct rvvd_dev* disk, void* buffer, uint64_t sec_id);
void rvvd_reverse_sector_lookup(struct rvvd_dev* disk, uint64_t offset);

#endif
