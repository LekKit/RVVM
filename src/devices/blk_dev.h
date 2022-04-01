/*
blk_dev.h - Block device API definition
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

#ifndef BLK_DEV_H
#define BLK_DEV_H

#include "rvvm_types.h"
#include "vector.h"
#include <aiocb.h>
#include <aio.h>

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
};

#endif
