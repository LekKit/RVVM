/*
drive.h - Drive API header
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


#ifndef DRIVE_H
#define DRIVE_H


#define DISK_TYPE_NOEX  -0x1
#define DISK_TYPE_NONE  0x0
#define DISK_TYPE_RVVD  0x1

#include <stdio.h>
#include <stdlib.h>
#include "rvvm_types.h"

typedef struct {
    void (*blk_open)(void*); // (void* drive)
    void (*blk_close)(void*); // (void* drive)
    void (*blk_allocate)(void*,void*,uint64_t); // (void* drive, void* data, uint64_t sector_id)
    void (*blk_read)(void*,void*,uint64_t,uint32_t); // (void* drive, void* dest_buffer, uint64_t offset, uint64 len)
    void (*blk_write)(void*,void*,uint64_t,uint32_t); // (void* drive, void* data, uint64_t offset, uint64 len)
    void (*blk_trim)(void*,uint64_t,uint32_t); // (void* drive, uint64, uint64_t offset, uint64 len)
    void (*blk_sync)(void*); // (void* drive)
    size_t (*blk_size)(void*);

    void* internal_drive;
} blk_dev;

static inline void blk_destroy(blk_dev* blk_device) {
    free(blk_device->internal_drive);
}

#endif
