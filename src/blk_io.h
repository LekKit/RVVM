/*
blk_io.h - Cross-platform Block & File IO library
Copyright (C) 2022 KotB <github.com/0xCatPKG>
                   LekKit <github.com/LekKit>
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

#ifndef BLK_IO_H
#define BLK_IO_H

#include "rvvm_types.h"
#include "spinlock.h"

/*
 * File API
 */

#define RVFILE_RW    1    // Open file in read/write mode
#define RVFILE_CREAT 2    // Create file if it doesn't exist (for RW only)
#define RVFILE_EXCL  4    // Prevent other processes from opening this file

#define RVFILE_SET   0    // Set file cursor
#define RVFILE_CUR   1    // Move file cursor
#define RVFILE_END   2    // Set file cursor relative to end

// Use file cursor as offset for read/write
// Not suitable for async IO
#define RVFILE_CURPOS ((uint64_t)-1)

typedef struct {
    uint64_t size;
    uint64_t pos;
    uint64_t pos_real;
    uint8_t  pos_state;
    spinlock_t lock;
    void* ptr;
    int fd;
} rvfile_t;

rvfile_t* rvopen(const char* filepath, uint8_t mode); // Returns NULL on failure
void      rvclose(rvfile_t* file);

uint64_t  rvfilesize(rvfile_t* file);

// If offset == RVFILE_CURPOS, uses current file position as offset
// Otherwise is equialent to pread/pwrite, and is thread-safe
size_t    rvread(rvfile_t* file, void* destination, size_t count, uint64_t offset);
size_t    rvwrite(rvfile_t* file, const void* source, size_t count, uint64_t offset);

bool      rvseek(rvfile_t* file, int64_t offset, uint8_t startpos);
uint64_t  rvtell(rvfile_t* file);
bool      rvtrim(rvfile_t* file, uint64_t offset, uint64_t count);
bool      rvflush(rvfile_t* file);
bool      rvtruncate(rvfile_t* file, uint64_t length);

/*
 * Async IO API
 * (needs review for rationale since we have thread_task)
 */

#define ASYNC_IO_DONE      0
#define ASYNC_IO_FAIL      1
#define ASYNC_IO_DISCARDED 2

#define RVFILE_ASYNC_READ  0
#define RVFILE_ASYNC_WRITE 1
#define RVFILE_ASYNC_TRIM  2
#define RVFILE_ASYNC_VA    3

typedef struct {
    uint8_t  opcode;
    void*    buffer;
    uint64_t offset;
    size_t   length;
} rvaio_op_t;

typedef void (*rvfile_async_callback_t)(rvfile_t* file, void* user_data, uint8_t flags);

bool rvread_async(rvfile_t* file, void* destination, size_t count, uint64_t offset, rvfile_async_callback_t callback, void* userdata);
bool rvwrite_async(rvfile_t* file, const void* source, size_t count, uint64_t offset, rvfile_async_callback_t callback, void* userdata);
bool rvtrim_async(rvfile_t* file, uint64_t count, uint64_t offset, rvfile_async_callback_t callback, void* userdata);
bool rvfsync_async(rvfile_t* file);

bool rvasync_va(rvfile_t* file, rvaio_op_t* iolist, size_t count, rvfile_async_callback_t callback, void* userdata);

/*
 * Block device API
 */

#define BLKDEV_RW  RVFILE_RW

#define BLKDEV_SET RVFILE_SET
#define BLKDEV_CUR RVFILE_CUR
#define BLKDEV_END RVFILE_END

#define BLKDEV_CURPOS RVFILE_CURPOS

typedef struct {
    const char* name;
    void     (*close)(void* dev);
    size_t   (*read)(void* dev, void* dst, size_t count, uint64_t offset);
    size_t   (*write)(void* dev, const void* src, size_t count, uint64_t offset);
    bool     (*trim)(void* dev, uint64_t offset, uint64_t count);
    bool     (*sync)(void* dev);
} blkdev_type_t;

typedef struct blkdev_t blkdev_t;

struct blkdev_t {
    blkdev_type_t* type;
    void* data;
    uint64_t size;
    uint64_t pos;
};

blkdev_t* blk_open(const char* filename, uint8_t opts);
void      blk_close(blkdev_t* dev);

static inline uint64_t blk_getsize(blkdev_t* dev)
{
    if (!dev) return 0;
    return dev->size;
}

static inline size_t blk_read(blkdev_t* dev, void* dst, size_t count, uint64_t offset)
{
    if (!dev) return 0;
    uint64_t real_pos = (offset == RVFILE_CURPOS) ? dev->pos : offset;
    if (real_pos + count > dev->size) return 0;
    size_t ret = dev->type->read(dev->data, dst, count, real_pos);
    if (offset == RVFILE_CURPOS) dev->pos += ret;
    return ret;
}

// It's illegal to seek out of device bounds,
// resizing the device is also impossible
static inline size_t blk_write(blkdev_t* dev, const void* src, size_t count, uint64_t offset)
{
    if (!dev) return 0;
    uint64_t real_pos = (offset == RVFILE_CURPOS) ? dev->pos : offset;
    if (real_pos + count > dev->size) return 0;
    size_t ret = dev->type->write(dev->data, src, count, real_pos);
    if (offset == RVFILE_CURPOS) dev->pos += ret;
    return ret;
}

static inline bool blk_seek(blkdev_t* dev, int64_t offset, uint8_t startpos)
{
    if (!dev) return false;
    if (startpos == BLKDEV_CUR) {
        offset = dev->pos + offset;
    } else if (startpos == BLKDEV_END) {
        offset = dev->size - offset;
    } else if (startpos != BLKDEV_SET) {
        return false;
    }
    if (((uint64_t)offset) <= dev->size) {
        dev->pos = offset;
        return true;
    } else {
        return false;
    }
}

static inline uint64_t blk_tell(blkdev_t* dev)
{
    if (!dev) return 0;
    return dev->pos;
}

static inline bool blk_trim(blkdev_t* dev, uint64_t offset, uint64_t count)
{
    if (!dev) return false;
    uint64_t real_pos = (offset == RVFILE_CURPOS) ? dev->pos : offset;
    if (real_pos + count > dev->size) return false;
    return dev->type->trim(dev->data, real_pos, count);
}

static inline bool blk_sync(blkdev_t* dev)
{
    if (!dev) return false;
    return dev->type->sync(dev->data);
}

#endif
