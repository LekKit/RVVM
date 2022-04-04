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
    uint64_t pos;
    uint8_t pos_state;
    spinlock_t lock;
    void* ptr;
    int fd;
} rvfile_t;

rvfile_t* rvopen(const char* filepath, uint8_t mode); // Returns NULL on failure
void      rvclose(rvfile_t* file);

uint64_t  rvfilesize(rvfile_t* file);
size_t    rvread(rvfile_t* file, void* destination, size_t count, uint64_t offset);
size_t    rvwrite(rvfile_t* file, const void* source, size_t count, uint64_t offset);
bool      rvtrim(rvfile_t* file, uint64_t offset, uint64_t count);
bool      rvseek(rvfile_t* file, uint64_t offset, uint8_t startpos);
uint64_t  rvtell(rvfile_t* file);
bool      rvflush(rvfile_t* file);
bool      rvtruncate(rvfile_t* file, uint64_t length);

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

typedef struct {
    uint64_t  request_id;
    rvfile_t* file;
    uint64_t  offset;
    size_t    length;
    void*     userdata;
    rvfile_async_callback_t callback_handler;
} aio_request_data;

bool rvread_async(rvfile_t* file, void* destination, size_t count, uint64_t offset, rvfile_async_callback_t callback, void* userdata);
bool rvwrite_async(rvfile_t* file, const void* source, size_t count, uint64_t offset, rvfile_async_callback_t callback, void* userdata);
bool rvtrim_async(rvfile_t* file, uint64_t count, uint64_t offset, rvfile_async_callback_t callback, void* userdata);
bool rvfsync_async(rvfile_t* file);

bool rvasync_va(rvfile_t* file, rvaio_op_t* iolist, size_t count, rvfile_async_callback_t callback, void* userdata);

#if 0

dev_blk* blk_init();
void blk_attach_drive(int drive_type, void* arg);

#define TRANS_PREPARED  0
#define TRANS_LAUNCHED  1
#define TRANS_RELAUNCH  2
#define TRANS_DONE      3
#define TRANS_ERROR     4

#define TROP_READ       0
#define TROP_WRITE      1
#define TROP_TRIM       2
#define TROP_SYNC       3

typedef struct blk_transaction blk_transaction;
typedef struct blk_dev blk_dev;

struct blk_dev
{
    uint8_t  async;
    void*    internal_device;
    uint64_t size;

    vector_t(blk_transaction*) tqueue;

    void (*blk_open) (void*);
    void (*blk_close)(void*);

    void (*blk_read) (blk_transaction*,void*);
    void (*blk_write)(blk_transaction*,void*);
    void (*blk_trim) (blk_transaction*,void*);
    void (*blk_sync) (blk_transaction*,void*);

    void (*operation_callback)(int);


};

struct blk_transaction
{
    uint64_t  transaction_id;
    uint8_t   transaction_status;
    uint8_t   transaction_op;
    blk_dev*  block_device;

    uint64_t  transaction_offset;
    uint32_t  transaction_length;
    void*     external_buffer;
    void*     internal_data;

    vector_t(aio_op*) opqueue;

};

#endif

#endif
