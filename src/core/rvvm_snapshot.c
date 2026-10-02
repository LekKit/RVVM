/*
rvvm_snapshot.c - RVVM Snapshot serialization
Copyright (C) 2020-2026 LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include <rvvm/rvvm_blk.h>
#include <rvvm/rvvm_snapshot.h>

#include <util/mem_ops.h>
#include <util/utils.h>

#define SNAP_MAGIC "\x7Frvvm-snapshot0\xFF"

PUSH_OPTIMIZATION_SIZE

struct rvvm_snapshot {
    rvvm_blk_dev_t* blk;

    // Start offset of current section
    uint64_t off;

    bool out;
    bool err;
};

static bool rvvm_snapshot_section_start(rvvm_snapshot_t* snap)
{
    uint8_t tmp[24] = {0};
    memcpy(tmp, SNAP_MAGIC, 16);
    snap->off = rvvm_blk_tell_head(snap->blk);
    return rvvm_blk_write_head(snap->blk, tmp, sizeof(tmp)) == sizeof(tmp);
}

static bool rvvm_snapshot_section_end(rvvm_snapshot_t* snap)
{
    uint64_t head = rvvm_blk_tell_head(snap->blk);
    if (head) {
        uint8_t tmp[8] = {0};
        if (head < snap->off + 24) {
            return false;
        }
        write_uint64_le_m(tmp, head - (snap->off + 24));
        return rvvm_blk_write(snap->blk, tmp, sizeof(tmp), snap->off + 16) == sizeof(tmp);
    }
    return true;
}

static bool rvvm_snapshot_section_read(rvvm_snapshot_t* snap)
{
    uint8_t tmp[24] = {0};
    if (rvvm_blk_read_head(snap->blk, tmp, sizeof(tmp)) != sizeof(tmp)) {
        return false;
    }
    if (memcmp(tmp, SNAP_MAGIC, 16)) {
        return false;
    }
    snap->off += read_uint64_le(tmp + 16) + 24;
    return true;
}

static bool rvvm_snapshot_section_next(rvvm_snapshot_t* snap)
{
    if (snap->out) {
        return rvvm_snapshot_section_end(snap) && rvvm_snapshot_section_start(snap);
    }
    return rvvm_snapshot_section_read(snap);
}

RVVM_PUBLIC rvvm_snapshot_t* rvvm_snapshot_open(rvvm_blk_dev_t* blk, bool out)
{
    rvvm_snapshot_t* snap = NULL;
    // Disallow serializing into non-empty blk
    if (blk && (!out || !rvvm_blk_get_size(blk))) {
        snap = safe_new_obj(rvvm_snapshot_t);
        rvvm_blk_seek_head(blk, 0, RVVM_BLK_SEEK_SET);
        snap->blk = blk;
        snap->out = out;
    }
    return snap;
}

RVVM_PUBLIC bool rvvm_snapshot_close(rvvm_snapshot_t* snap)
{
    if (snap) {
        if (snap->out) {
            rvvm_snapshot_section_end(snap);
        }
        bool ret = !snap->err;
        free(snap);
        return ret;
    }
    return false;
}

RVVM_PUBLIC bool rvvm_snapshot_section(rvvm_snapshot_t* snap, const char* name)
{
    if (snap && name && !snap->err) {
        char   buf[256] = {0};
        size_t len      = rvvm_strlen(name);
        if (len > 255) {
            len = 255;
        }
        if (snap->out) {
            memcpy(buf, name, len);
        }
        do {
            if (!rvvm_snapshot_section_next(snap) || !rvvm_snapshot_data(snap, buf, len)) {
                snap->err = true;
                return false;
            }
        } while (!snap->out && rvvm_strcmp(buf, name) == false);
        return true;
    }
    snap->err = true;
    return false;
}

RVVM_PUBLIC bool rvvm_snapshot_writing(rvvm_snapshot_t* snap)
{
    if (snap) {
        return snap->out;
    }
    return false;
}

RVVM_PUBLIC bool rvvm_snapshot_data(rvvm_snapshot_t* snap, void* data, size_t size)
{
    if (snap && data && !snap->err) {
        if (snap->out && rvvm_blk_write_head(snap->blk, data, size) == size) {
            return true;
        } else if (!snap->out &&                                          //
                   (rvvm_blk_tell_head(snap->blk) + size <= snap->off) && //
                   (rvvm_blk_read_head(snap->blk, data, size) == size)) {
            return true;
        }
    }
    snap->err = true;
    return false;
}

RVVM_PUBLIC bool rvvm_snapshot_host(rvvm_snapshot_t* snap, void* data, size_t size)
{
    if (snap && data && !(((size_t)data) & (size - 1))) {
        switch (size) {
            case 1:
                return rvvm_snapshot_data(snap, data, size);
            case 2: {
                uint16_t tmp = atomic_load_uint16_relax(data);
                write_uint16_le(&tmp, tmp);
                if (!rvvm_snapshot_data(snap, &tmp, sizeof(tmp))) {
                    return false;
                } else if (!snap->out) {
                    atomic_store_uint16_relax(data, read_uint16_le(&tmp));
                }
                return true;
            }
            case 4: {
                uint32_t tmp = atomic_load_uint32_relax(data);
                write_uint32_le(&tmp, tmp);
                if (!rvvm_snapshot_data(snap, &tmp, sizeof(tmp))) {
                    return false;
                } else if (!snap->out) {
                    atomic_store_uint32_relax(data, read_uint32_le(&tmp));
                }
                return true;
            }
            case 8: {
                uint64_t tmp = atomic_load_uint64_relax(data);
                write_uint64_le(&tmp, tmp);
                if (!rvvm_snapshot_data(snap, &tmp, sizeof(tmp))) {
                    return false;
                } else if (!snap->out) {
                    atomic_store_uint64_relax(data, read_uint64_le(&tmp));
                }
                return true;
            }
        }
    }
    snap->err = true;
    return false;
}

POP_OPTIMIZATION_SIZE
