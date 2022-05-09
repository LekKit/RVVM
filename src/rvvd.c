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



void rvvd_sector_write(struct rvvd_dev* disk, const void* data, uint64_t offset) {
    rvwrite(disk->_fd, data, 512, offset);
}

void rvvd_sector_read(struct rvvd_dev* disk, void* buffer, uint64_t offset) {
    rvread(disk->_fd, buffer, 512, offset);
}

void rvvd_sector_read_recursive(struct rvvd_dev* disk, void* buffer, uint64_t sec_id) {
    rvvd_read(disk, buffer, sec_id);
}


struct rvvd_dev* rvvd_mkimg(const char* filename, uint64_t size) {
    // Creates new Risc-V Virtual Drive by filename & size

    rvvm_info("Creating RVVD drive \"%s\" with size %ld", filename, size);

    struct rvvd_dev* disk = safe_calloc(sizeof(struct rvvd_dev), 1);

    size_t namelen = strlen(filename);
    if (namelen > 255) namelen = 255;
    memcpy(disk->filename, filename, namelen);
    disk->filename[namelen] = 0;

    disk->overlay = false;
    disk->compression_type = DCOMPESSION_NONE;

    disk->_fd = rvopen(filename, RVFILE_RW | RVFILE_CREAT | RVFILE_EXCL);

    if (!disk->_fd) return NULL;

    // Writing header
    uint8_t header[512] = {0};
    memcpy(header, "RVVD", 4);
    write_uint32_le(header+4, RVVD_VERSION);
    write_uint64_le(header+8, ((size + 511ULL) & ~511ULL)/512);
    disk->size = ((size + 511ULL) & ~511ULL);
    disk->sector_table_size = (disk->size /512 *8 /512);
    disk->version = RVVD_VERSION;
    write_uint64_le(header+16, 512+512*disk->sector_table_size);
    rvvd_sector_write(disk, header, 0);
    disk->next_sector_offset = 512+512*disk->sector_table_size;

    // Allocating sector table
    memset(header, 0, 512);
    for (size_t i=0; i < disk->sector_table_size; ++i) {
        rvwrite(disk->_fd, header, 512, 512+512*i);
    }

    memset(disk->sector_cache, 0, sizeof(disk->sector_cache));
    disk->sector_cache[0].id = -1;

    return disk;

}

struct rvvd_dev* rvvd_mkoverlay(const char* base_filename, const char* filename){
    // Creates disk in overlay mode, using existing disk image

    rvvm_info("Creating RVVD drive overlay \"%s\" (base drive \"%s\")", filename, base_filename);

    struct rvvd_dev* disk;
    struct rvvd_dev* base_disk;

    if (!(base_disk = rvvd_open(base_filename))) {
        rvvm_error("RVVD ERROR: Could not open base drive file!");
        return NULL;
    }
    if (!(disk = rvvd_mkimg(filename, base_disk->size))) {
        rvvm_error("RVVD ERROR: Could not create drive file!");
        return NULL;
    }

    rvvm_info("Changing drive type to DTYPE_OVERLAY");

    disk->base_disk = base_disk;
    disk->overlay = true;

    // Modifying image header 
    uint8_t header[512] = {0};
    rvvd_sector_read(disk, header, 0);
    write_uint64_le(header+16, 512+512*disk->sector_table_size);
    header[24] |= DOPT_OVERLAY;
    write_uint8(header+25, 0);
    memcpy(header+26, base_disk->filename, 256);

    rvvd_sector_write(disk, header, 0);

    return disk;

}

struct rvvd_dev* rvvd_mkimg_from_image(const char* image_filename, const char* filename) {
    // Creates disk from passed .img file

    rvvm_info("Creating RVVD drive \"%s\" from \"%s\"", filename, image_filename);

    rvfile_t* img_fd;

    if (!(img_fd = rvopen(image_filename, 0))) {
        rvvm_error("RVVD ERROR: Could not create drive from image: Can not open image file");
        return NULL;
    }

    uint64_t size = rvfilesize(img_fd);

    struct rvvd_dev* disk;

    if (!(disk = rvvd_mkimg(filename, size))) {
        rvvm_error("RVVD ERROR: Could not create drive file!");
        return NULL;
    }

    rvvm_info("Writing drive image data to rvvd drive");

    uint8_t bufferspace[512] = {0};
    for (size_t i = 0; i < size/512; i++)
    {
        rvread(img_fd, bufferspace, 512, RVFILE_CURPOS);
        rvvd_write(disk, bufferspace, i);
        memset(bufferspace, 0, 512);
    }

    rvclose(img_fd);
    return disk;
}

struct rvvd_dev* rvvd_open(const char* filename) {
    // Opens Risc-V Virtual Drive

    rvvm_info("Opening RVVD drive \"%s\"", filename);

    struct rvvd_dev* disk = safe_calloc(sizeof(struct rvvd_dev), 1);

    if (!(disk->_fd = rvopen(filename, RVFILE_RW | RVFILE_EXCL)) ) {
        rvvm_error("RVVD ERROR: Could not open drive file!");
        return NULL;
    }

    // Initializing disk structure
    uint8_t header[512] = {0};
    rvread(disk->_fd, header, 512, 0);

    if (memcmp(header, "RVVD", 4)) {
        rvvm_error("RVVD ERROR: Passed \"%s\" file is not RVVD drive image.", filename);
        return NULL;
    }

    size_t namelen = strlen(filename);
    if (namelen > 255) namelen = 255;
    memcpy(disk->filename, filename, namelen);
    disk->filename[namelen] = 0;

    disk->version = read_uint32_le(header+4);

    if (disk->version != RVVD_VERSION && (disk->version > RVVD_VERSION || disk->version < RVVD_MIN_VERSION)) {
        rvvm_error("RVVD ERROR: version mismatch: can't load newer version of drive image");
        return NULL;
    } else if (disk->version != RVVD_VERSION && disk->version < RVVD_VERSION) {
        rvvm_warn("Drive \"%s\" version is outdated, consider update it to new version", filename);
    }

    // Reading header
    disk->size = read_uint64_le(header+8)*512;
    disk->next_sector_offset = read_uint64_le(header+16);
    disk->overlay = header[24] & DOPT_OVERLAY;
    disk->compression_type = read_uint8(header+25);

    // If drive is overlay, we opening base image too
    if (disk->overlay) {

        rvvm_info("Drive \"%s\" is overlay drive, opening base image...", filename);

        struct rvvd_dev* base_disk;

        if (!memcmp(header+26, disk->filename, 256)) {
            rvvm_error("RVVD ERROR: Base drive can not be same as this overlay drive");
            return NULL;
        }
        if (!(base_disk = rvvd_open((const char*)header+26))) {
            rvvm_error("RVVD ERROR: Can't open base disk \"%s\"", base_disk->filename);
            return NULL;
        }
        disk->base_disk = base_disk;
    }

    disk->sector_table_size = (disk->size /512 *8 /512);
    memset(disk->sector_cache, 0, sizeof(disk->sector_cache));
    disk->sector_cache[0].id = -1;

    return disk;
}

struct rvvd_dev* rvvd_fdopen(rvfile_t* fd) {
        // Opens Risc-V Virtual Drive

    rvvm_info("Opening RVVD drive %p", fd);

    struct rvvd_dev* disk = safe_calloc(sizeof(struct rvvd_dev), 1);

    disk->_fd = fd;

    // Initializing disk structure
    uint8_t header[512] = {0};
    rvread(disk->_fd, header, 512, 0);

    if (memcmp(header, "RVVD", 4)) {
        rvvm_error("RVVD ERROR: Passed file is not RVVD drive image.");
        return NULL;
    }

    // size_t namelen = strlen(filename);
    // if (namelen > 255) namelen = 255;
    // memcpy(disk->filename, filename, namelen);
    // disk->filename[namelen] = 0;

    disk->version = read_uint32_le(header+4);

    if (disk->version != RVVD_VERSION && (disk->version > RVVD_VERSION || disk->version < RVVD_MIN_VERSION)) {
        rvvm_error("RVVD ERROR: version mismatch: can't load newer version of drive image");
        return NULL;
    } else if (disk->version != RVVD_VERSION && disk->version < RVVD_VERSION) {
        rvvm_warn("Drive version is outdated, consider update it to new version");
    }

    // Reading header
    disk->size = read_uint64_le(header+8)*512;
    disk->next_sector_offset = read_uint64_le(header+16);
    disk->overlay = header[24] & DOPT_OVERLAY;
    disk->compression_type = read_uint8(header+25);

    // If drive is overlay, we opening base image too
    if (disk->overlay) {

        rvvm_info("Drive %p is overlay drive, opening base image...", disk->_fd);

        struct rvvd_dev* base_disk;

        if (!memcmp(header+26, disk->filename, 256)) {
            rvvm_error("RVVD ERROR: Base drive can not be same as this overlay drive");
            return NULL;
        }
        if (!(base_disk = rvvd_open((const char*)header+26))) {
            rvvm_error("RVVD ERROR: Can't open base disk \"%s\"", base_disk->filename);
            return NULL;
        }
        disk->base_disk = base_disk;
    }

    disk->sector_table_size = (disk->size /512 *8 /512);
    memset(disk->sector_cache, 0, sizeof(disk->sector_cache));
    disk->sector_cache[0].id = -1;

    return disk;
}

void rvvd_close(struct rvvd_dev* disk) {
    // Closes rvvd disk

    rvvm_info("Closing RVVD drive %p", disk->_fd);

    if (disk->overlay) {
        rvvm_info("Closing RVVD drive base \"%s\"", disk->base_disk->filename);
        rvvd_close(disk->base_disk);
    }

    rvclose(disk->_fd);
    free(disk);
}

void rvvd_migrate_to_current_version(struct rvvd_dev* disk) {
    // Migrate disk file to the last RVVD version

    uint8_t header[512] = {0};
    rvvd_sector_read(disk, header, 0);
    write_uint32_le(header+4, RVVD_VERSION);
    rvvd_sector_write(disk, header, 0);
}

void rvvd_convert_to_solid(struct rvvd_dev* disk) {
    // Convert overlay image in solid mode

    rvvm_info("RVVD %p: Changing overlay into solid", disk->_fd);

    uint8_t buffer[512] = {0};
    for (size_t i = 0; i < disk->sector_table_size*64; i++) {
        rvvd_read(disk, buffer, i);
        rvvd_write(disk, buffer, i);
        memset(buffer, 0, 512);
    }

    rvvd_sector_read(disk, buffer, 0);
    buffer[24] &= ~DOPT_OVERLAY;
    rvvd_sector_write(disk, buffer, 0);
    disk->overlay = false;
    rvvd_sync(disk);

}

void rvvd_dump_to_image(struct rvvd_dev* disk, const char* filename) {
    //Dump contents of RVVD drive into image file

    rvvm_info("RVVD %p: Dumping image", disk->_fd);

    rvfile_t* img = rvopen(filename, RVFILE_RW | RVFILE_CREAT | RVFILE_EXCL);
    if (img == NULL) {
        rvvm_error("RVVD ERROR at %p: Could not create image file", disk->_fd);
        return;
    }
    uint8_t buffer[512] = {0};
    for (size_t i = 0; i < disk->sector_table_size*64; i++) {
        rvvd_read(disk, buffer, i);
        rvwrite(img, buffer, 512, RVFILE_CURPOS);
        memset(buffer, 0, 512);
    }

    rvclose(img);

}

void rvvd_read(struct rvvd_dev* disk, void* buffer, uint64_t sec_id) {

    rvvm_info("RVVD %p: Reading sector %ld", disk->_fd, sec_id);

    uint8_t data[512] = {0};

    uint64_t offset = rvvd_sc_get(disk, sec_id);

    // Get offset directly, if cache is invalid
    if (offset == ((uint64_t)-1)) offset = rvvd_sector_get_offset(disk, sec_id); 

    
    if (!disk->overlay) {
        if (offset) rvvd_sector_read(disk, data, offset);
    } else {
        // If sector isn't allocated, try to read that sector in base image
        if (!offset) rvvd_read(disk->base_disk, data, sec_id);
        else rvvd_sector_read(disk, data, offset);
    }

    memcpy(buffer, data, 512);
    rvvd_sc_push(disk, sec_id, offset);


}

void rvvd_write(struct rvvd_dev* disk, const void* data, uint64_t sec_id) {
    /*
    Writing in the disk file. If sector isn't allocated and write data not full of zeros,
    allocates it
    */

    rvvm_info("RVVD %p: Writing sector %ld", disk->_fd, sec_id);

    uint64_t offset = rvvd_sc_get(disk, sec_id);

    // Get offset directly, if cache is invalid
    if (offset == ((uint64_t)-1)) offset = rvvd_sector_get_offset(disk, sec_id);

    for (size_t i = 0; i < 512/sizeof(size_t); i++) {
        // If sector isn't allocated & data not empty, allocate new block
        if (((const size_t*)data)[i] && !offset) {
            rvvd_allocate(disk, data, sec_id);
            return;
        }
    }

    // If block isn't allocated & data is empty, here nothing to do
    if (!offset) return; 

    rvvd_sector_write(disk, data, offset);
    rvvd_sc_push(disk, sec_id, offset);

}

void rvvd_allocate(struct rvvd_dev* disk, const void* data, uint64_t sec_id) {
    // Allocates block in the disk file

    rvvm_info("RVVD %p: Allocating sector %ld", disk->_fd, sec_id);

    // Gets last sector offset, then writes the sector
    uint64_t offset = disk->next_sector_offset;
    disk->next_sector_offset += 512;
    rvwrite(disk->_fd, data, 512, offset);

    uint8_t tmpbuf[8] = {0};

    // Writing offset to the sector table
    write_uint64_le(tmpbuf, offset);
    rvwrite(disk->_fd, tmpbuf, 8, 512+sec_id*8);

    // Writing last block offset to the file header
    write_uint64_le(tmpbuf, offset);
    rvwrite(disk->_fd, tmpbuf, 8, 16);

    rvvd_sc_push(disk, sec_id, offset);

}

bool rvvd_sync(struct rvvd_dev* disk) {

    rvvm_info("RVVD %p: Sync request", disk->_fd);
    rvflush(disk->_fd);
    return true;
}


void rvvd_sc_push(struct rvvd_dev* disk, uint64_t sec_id, uint64_t offset) {
    // Filling up table conversion result in the sector cache table

    rvvm_info("RVVD %p: Pushing sector cache {%ld : %ld}", disk->_fd, sec_id, offset);
    disk->sector_cache[sec_id & 0x1FF].offset = offset;
    disk->sector_cache[sec_id & 0x1FF].id = sec_id;

}

uint64_t rvvd_sc_get(struct rvvd_dev* disk, uint64_t sec_id) {

    rvvm_info("RVVD %p: Getting sector cache entry with sector_id = %ld", disk->_fd, sec_id);

    // Get sector cache entry

    uint64_t offset = ((uint64_t)-1);
    if (disk->sector_cache[sec_id & 0x1FF].id == sec_id) {
        offset = disk->sector_cache[sec_id & 0x1FF].offset;
    }
    return offset;
}

void rvvd_sc_forward_predict(struct rvvd_dev* disk, uint64_t from_sector, uint32_t sector_count) {

    rvvm_info("RVVD %p: Forward prediction of %d offsets", disk->_fd, sector_count);

    // Reads sector_count sectors from from_sector id

    if (sector_count > 64) sector_count = 64;
    if (from_sector + sector_count > disk->sector_table_size * 64) return;
    uint8_t buffer[512] = {0};
    rvread(disk->_fd, buffer, 8*sector_count, 512+(8*from_sector));
    for (size_t i = 0; i < sector_count; i++) {
        uint64_t offset = read_uint64_le(buffer+(8*i));
        rvvd_sc_push(disk, from_sector+i, offset);
    }
}

uint64_t rvvd_sector_get_offset(struct rvvd_dev* disk, uint64_t sec_id) {

    // Gets sector offset by sector id

    uint8_t offsetbuf[8] = {0};
    rvread(disk->_fd, offsetbuf, 8, 512+sec_id*8);
    return read_uint64_le(offsetbuf);
}

size_t rvvd_blk_read(void* dev, void* dst, size_t count, uint64_t offset) {

    // Read wrapper for blk API

    struct rvvd_dev* drive = (struct rvvd_dev*)dev;
    if (!drive->_fd) return 0;
    if (count % 512 != 0 || offset % 512 != 0) return 0;

    uint64_t start_sector_id = offset/512;
    size_t sector_count = count/512;

    rvvd_sc_forward_predict(drive, start_sector_id, sector_count);

    for (size_t i = 0; i < sector_count; i++) rvvd_read(drive, dst+512*i, start_sector_id+i);
    return count;

}

size_t rvvd_blk_write(void* dev, const void* src, size_t count, uint64_t offset) {

    // Write wrapper for blk API

    struct rvvd_dev* drive = (struct rvvd_dev*)dev;
    if (!drive->_fd) return 0;
    if (count % 512 != 0 || offset % 512 != 0) return 0;

    uint64_t start_sector_id = offset/512;
    size_t sector_count = count/512;

    for (size_t i = 0; i < sector_count; i++) rvvd_write(drive, src+512*i, start_sector_id+i);
    return count;

}

bool rvvd_blk_trim (void* dev, uint64_t offset, uint64_t count) {

    // Trim wrapper for blk API

    UNUSED(dev);
    UNUSED(offset);
    UNUSED(count);
    return false;
}

static blkdev_type_t blkdev_type_rvvd = {
    .name = "rvvd",
    .close = (const void*)rvvd_close,
    .read = rvvd_blk_read,
    .write = rvvd_blk_write,
    .trim = rvvd_blk_trim,
    .sync = (const void*)rvvd_sync,
};

bool blk_init_rvvd(blkdev_t* dev, rvfile_t* file)
{
    struct rvvd_dev* disk = rvvd_fdopen(file);
    if (!disk) return false;
    dev->type = &blkdev_type_rvvd;
    dev->data = disk;
    dev->size = disk->size;
    
    return true;
}