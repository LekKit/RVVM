/*
rvvd.c - Risc-V Virtual Drive image implementation
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

#define RVVD_SECTOR_SIZE 512

#include "rvvd.h"
#include "mem_ops.h"
#include "utils.h"

int rvvd_init(struct rvvd_disk* disk, const char* filename, uint64_t size) {
    // Creates new Risc-V Virtual Drive by filename & size
    memcpy(disk->filename, filename, 256);
    // disk->filename = filename;
    disk->disk_type = DTYPE_SOLID;
    disk->compression_type = DCOMPESSION_NONE;
    if ( !(disk->_fd = fopen(filename, "wb+")) ) {
        rvvm_error("RVVD ERROR: Could not create disk file!");
        return -1;
    }

    // Writing header
    uint8_t header[512] = {0};
    memcpy(header, "RVVD", 4);
    write_uint32_le(header+4, RVVD_VERSION);
    write_uint64_le(header+8, size);
    rvvd_sector_write(disk, header, 0);
    disk->size = size;
    disk->version = RVVD_VERSION;

    // Allocating sector table
    memset(header, 0, 512);
    disk->sector_table_size = (size + 32767) >> 15;
    for (size_t i=0; i < (size + 32767) >> 15; ++i) {
        fwrite(header, 512, 1, disk->_fd);
    }

    return 0;

}

int rvvd_init_overlay(struct rvvd_disk* disk, const char* base_filename, const char* filename){
    // Creates disk in overlay mode, using existing disk image

    struct rvvd_disk* base_disk = safe_malloc(sizeof(struct rvvd_disk));

    if (!rvvd_open(base_disk, base_filename)) {
        rvvm_error("RVVD ERROR: Could not open base disk file!");
        return -1;
    }
    if (rvvd_init(disk, filename, base_disk->size)) {
        rvvm_error("RVVD ERROR: Could not create disk file!");
        return -2;
    }
    disk->base_disk = base_disk;
    disk->disk_type = DTYPE_OVERLAY;

    //Seeking in file start pos then modifying default header
    uint8_t header[512] = {0};
    rvvd_sector_read(disk, header, 0);
    write_uint8(header+16, DTYPE_OVERLAY);
    write_uint8(header+17, 0);
    memcpy(header+18, base_disk->filename, 256);

    rvvd_sector_write(disk, header, 0);

    return 0;

}

int rvvd_init_from_image(struct rvvd_disk* disk, const char* image_filename, const char* filename) {
    // Creates disk from passed .img file

    FILE* img_fd;

    if (!(img_fd = fopen(image_filename, "rb"))) {
        rvvm_error("RVVD ERROR: Could not create disk from image: Can not open image file");
        return -1;
    }

    // Getting file size;
    uint64_t size;
    fseek(img_fd, 0, 2);
    size = ftell(img_fd);
    fseek(img_fd, 0, 0);

    if (rvvd_init(disk, filename, size)) {
        rvvm_error("RVVD ERROR: Could not create disk file!");
        return -2;
    }

    uint8_t bufferspace[1024*1024] = {0};
    for (size_t i = 0; i < size/(1024*1024); i++) {
        fread(bufferspace, 1024*1024, 1, img_fd);
        for (size_t y = 0; y < 1024*1024/512; y++) blk_write(disk, bufferspace+512*y, y);
        memset(bufferspace, 0, 1024*1024);
    }

    fclose(img_fd);
    return 0;
}

int rvvd_open(struct rvvd_disk* disk, const char* filename) {
    // Opens Risc-V Virtual Drive
    if ( !(disk->_fd = fopen(filename, "rb+")) ) {
        rvvm_error("RVVD ERROR: Could not open disk file!");
        return -1;
    }

    // Initializing disk structure
    uint8_t header[512] = {0};
    fread(header, 512, 1, disk->_fd);
    if (memcmp(header, "RVVD", 4)) {
        rvvm_error("RVVD ERROR: Passed \"%s\" file is not RVVD disk image.", disk->filename);
        return -2;
    }

    memcpy(disk->filename, filename, 256);
    // disk->filename = filename;
    disk->version = read_uint32_le(header+4);

    if (disk->version != RVVD_VERSION && (disk->version > RVVD_VERSION || disk->version < RVVD_MIN_VERSION)) {
        rvvm_error("RVVD ERROR: version mismatch: can't load newer version of disk image");
        return -3;
    } else if (disk->version != RVVD_VERSION && disk->version < RVVD_VERSION) {
        rvvm_warn("Disk \"%s\" version is outdated, consider update it to new version", disk->filename);
    }

    disk->size = read_uint64_le(header+8);
    disk->disk_type = read_uint8(header+16);
    disk->compression_type = read_uint8(header+17);
    if (disk->disk_type == DTYPE_OVERLAY) {
        struct rvvd_disk* base_disk = safe_malloc(sizeof(struct rvvd_disk));
        memcpy(base_disk->filename, header+18, 256);
        if (memcmp(base_disk->filename, disk->filename, 256)) {
            rvvm_error("RVVD ERROR: Base disk can not be same as this overlay file");
        }
        rvvm_info("Opening base disk \"%s\" for disk overlay \"%s\"", base_disk->filename, disk->filename);
        switch ( rvvd_open(base_disk, base_disk->filename) ) {
            case -1:
                rvvm_error("RVVD ERROR: Can't open disk base.");
                return -4;
            case -2:
                rvvm_error("RVVD ERROR: Can't open disk base: disk base already opened by another instance of rvvm");
                return -4;
            case -3:
                rvvm_error("RVVD ERROR: Can't open disk base: version mismatch: can't load newer version of disk image");
                return -4;
            case -4:
                rvvm_error("RVVD ERROR: Can't open disk base: base disk reported error");
                return -4;
            default:
                break;
        }
    }

    disk->sector_table_size = (disk->size + 32767) >> 15;
    memset(disk->sector_cache, 0, sizeof(disk->sector_cache));
    disk->sector_cache[0].id = -1;

    return 0;
}

void rvvd_close(struct rvvd_disk* disk) {
    // Closes rvvd disk, notice that disk structure passed in args will be freed

    if (disk->disk_type == DTYPE_OVERLAY) {
        rvvd_close(disk->base_disk);
    }

    fclose(disk->_fd);
    free(disk);
}

void rvvd_migrate_to_current_version(struct rvvd_disk* disk) {
    // Migrate disk file to the last RVVD version

    uint8_t header[512] = {0};
    rvvd_sector_read(disk, header, 0);
    write_uint32_le(header+4, RVVD_VERSION);
    rvvd_sector_write(disk, header, 0);
}

void rvvd_convert_to_solid(struct rvvd_disk* disk) {
    // Convert disk file to solid type

    uint8_t header[512] = {0};
    rvvd_sector_read(disk, header, 0);
    uint8_t* sector_table;
    sector_table = safe_malloc(disk->sector_table_size*512);
    fread(sector_table, disk->sector_table_size*512, 1, disk->_fd);

    uint8_t sector[512] = {0};
    disk->sector_table_size = (disk->size + 32767) >> 15;
    for (size_t i=0; i < (disk->size + 32767) >> 15; ++i) {
        fwrite(sector, 512, 1, disk->_fd);
    }

    uint8_t tmpbuf[512] = {0};
    uint64_t offset = 0;
    for (size_t i = 0; i < disk->sector_table_size*512/8; i++) {
        memset(tmpbuf, 0, 512);
        offset = read_uint64_le(sector_table+i);
        rvvd_sector_read_recursive(disk, tmpbuf, i);
        blk_write(disk, tmpbuf, i);
        rvvd_sector_read(disk, tmpbuf, offset);
        blk_write(disk, tmpbuf, i);
    }

}

void blk_read(struct rvvd_disk* disk, void* buffer, uint64_t sec_id) {
    uint8_t data[512] = {0};

    uint64_t offset = rvvd_get_sector_cache_entry(disk, sec_id);

    if (!offset) offset = rvvd_sector_get_offset(disk, sec_id);

    if (disk->disk_type != DTYPE_OVERLAY ) {
        if (offset) rvvd_sector_read(disk, data, offset);
    } else {
        if (!offset) blk_read(disk->base_disk, data, sec_id);
        else rvvd_sector_read(disk, data, offset);
    }

    memcpy(buffer, data, 512);
    rvvd_push_sector_cache(disk, sec_id, offset);


}

void blk_write(struct rvvd_disk* disk, void* data, uint64_t sec_id) {
    /*
    Writing in the disk file. If sector isn't allocated and write data not full of zeros,
    allocates it
    */

    uint64_t offset = rvvd_get_sector_cache_entry(disk, sec_id);

    if (!offset) offset = rvvd_sector_get_offset(disk, sec_id);

    for (int i = 0; i < 512/sizeof(size_t); i++) {
        if (((const size_t*)data)[i] && !offset) {
            blk_allocate(disk, data, sec_id);
            return;
        }
    }

    if (!offset) return;

    rvvd_sector_write(disk, data, offset);
    rvvd_push_sector_cache(disk, sec_id, offset);

}

void blk_allocate(struct rvvd_disk* disk, void* data, uint64_t sec_id) {
    // Allocates block in the disk file

    fseek(disk->_fd, 0, 2);
    uint64_t offset = ftell(disk->_fd);
    fwrite(data, 512, 1, disk->_fd);
    fseek(disk->_fd, 512+sec_id*8, 0);
    uint8_t tmpbuf[8] = {0};
    write_uint64_le(tmpbuf, offset);
    fwrite(tmpbuf, 8, 1, disk->_fd);
    rvvd_push_sector_cache(disk, sec_id, offset);

}

void blk_sync(struct rvvd_disk* disk) {
    fflush(disk->_fd);
}

// void blk_trim(struct rvvd_disk* disk, uint64_t sec_id) {
//     uint64_t offset = rvvd_get_sector_cache_entry(disk, sec_id);
//     if (!offset) offset = rvvd_sector_get_offset(disk, sec_id);
//     if (!offset) {
//         if (disk->disk_type != DTYPE_OVERLAY) rvvm_warn("RVVD WARNING: Trim called on non allocated sector; Skipping it.");
//         return;
//     }
//
//
// }




void rvvd_push_sector_cache(struct rvvd_disk* disk, uint64_t sec_id, uint64_t offset) {
    // Filling up table conversion result in the sector cache table

    if (offset && disk->sector_cache[sec_id & 0x1FF].id != sec_id) {
        disk->sector_cache[sec_id & 0x1FF].offset = offset;
        disk->sector_cache[sec_id & 0x1FF].id = sec_id;
    }
}

uint64_t rvvd_get_sector_cache_entry(struct rvvd_disk* disk, uint64_t sec_id) {
    uint64_t offset = 0;
    if (disk->sector_cache[sec_id & 0x1FF].id == sec_id) {
        offset = disk->sector_cache[sec_id & 0x1FF].offset;
    }
    return offset;
}

uint64_t rvvd_sector_get_offset(struct rvvd_disk* disk, uint64_t sec_id) {
    fseek(disk->_fd, 512+sec_id*8, 0);
    uint8_t offsetbuf[8] = {0};
    fread(offsetbuf, 8, 1, disk->_fd);
    return read_uint64_le(offsetbuf);
}

void rvvd_sector_write(struct rvvd_disk* disk, void* data, uint64_t offset) {
    fseek(disk->_fd, offset, 0);
    fwrite(data, 512, 1, disk->_fd);
}

void rvvd_sector_read(struct rvvd_disk* disk, void* buffer, uint64_t offset) {
    fseek(disk->_fd, offset, 0);
    fread(buffer, 512, 1, disk->_fd);
}

void rvvd_sector_read_recursive(struct rvvd_disk* disk, void* buffer, uint64_t sec_id) {
    blk_read(disk, buffer, sec_id);
}
