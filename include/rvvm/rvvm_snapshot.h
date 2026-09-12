/*
<rvvm/rvvm_snapshot.h> - RVVM Snapshot serialization
Copyright (C) 2020-2026 LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#ifndef _RVVM_SNAPSHOT_H
#define _RVVM_SNAPSHOT_H

#include <rvvm/rvvm_base.h>

RVVM_EXTERN_C_BEGIN

/**
 * @defgroup rvvm_snapshot Snapshot serialization
 * @addtogroup rvvm_snapshot
 * @{
 *
 * Snapshots are stored to block devices, which internally may be represented
 * by a file, deduplicated image, provided over network, etc
 *
 * Devices must serialize/deserialize their state from suspend/resume callbacks
 *
 * Every device state should be stored in a separate, optionally versioned section
 *
 * Section order with same name is preserved in snapshot within constructed machine
 *
 * Serialization direction will usually be transparent to device implementations,
 * which should only enumerate their fields once to support state read/write
 *
 * For backward-compatible additions to device state, section name should be retained,
 * with new, optional data stored after all backward-compatible state fields
 *
 * Upon IO error or reading corrupted data, any further operation will fail
 *
 * The deserialized state after failed rvvm_snapshot_close() is unspecified
 *
 * This API is NOT thread-safe, but suspend callbacks guarantee exclusive caller
 */

/**
 * Open snapshot for reading or writing from block device handle
 *
 * \param blk   Block device handle
 * \param write Intent to write snapshot state
 * \return      Snapshot handle or NULL
 */
RVVM_PUBLIC rvvm_snapshot_t* rvvm_snapshot_open(rvvm_blk_dev_t* blk, bool write);

/**
 * Close snapshot handle
 *
 * Synchronizes changes to backing storage after writing
 *
 * \param snap Snapshot handle
 * \return     Success (No IO error or corruption)
 */
RVVM_PUBLIC bool rvvm_snapshot_close(rvvm_snapshot_t* snap);

/**
 * Open next snapshot section, reset data pointer and failure flag
 *
 * When writing to snapshot:
 * - Finalizes previous section, creates new section for writing
 * When reading from snapshot:
 * - Opens next section with matching name for reading
 *
 * For a missing section, all serialization calls will fail
 *
 * \param snap Snapshot handle
 * \param name Section name for reading/writing
 * \return     Success (Section exists)
 */
RVVM_PUBLIC bool rvvm_snapshot_section(rvvm_snapshot_t* snap, const char* name);

/**
 * Check opened snapshot direction (Reading/Writing)
 *
 * \param snap Snapshot handle
 * \return     Writing state to snapshot
 */
RVVM_PUBLIC bool rvvm_snapshot_writing(rvvm_snapshot_t* snap);

/**
 * Read/Write opaque data to current snapshot section, advance pointer
 *
 * Direction is decided by current snapshot open mode
 *
 * \param snap Snapshot handle
 * \param data Opaque data pointer
 * \param size Opaque data size
 * \return     Success (Data was available)
 */
RVVM_PUBLIC bool rvvm_snapshot_data(rvvm_snapshot_t* snap, void* data, size_t size);

/**
 * Read/Write host-endian variable to current snapshot section, advance pointer
 *
 * Serializes host-endian variable as little-endian for sizes of 1, 2, 4, 8
 *
 * Direction is decided by current snapshot open mode
 *
 * \param snap Snapshot handle
 * \param data Opaque data pointer
 * \param size Opaque data size
 * \return     Success (Data was available and size is correct)
 */
RVVM_PUBLIC bool rvvm_snapshot_host(rvvm_snapshot_t* snap, void* data, size_t size);

/**
 * Read/Write host field to current snapshot section, advance pointer
 *
 * \param snap  Snapshot handle
 * \param field Opaque variable field
 * \return      Success (Data was available and size is correct)
 */
#define rvvm_snapshot_field(snap, field) rvvm_snapshot_host(snap, &(field), sizeof(field))

/**
 * Serialize or deserialize whole machine state to/from a snapshot
 *
 * The machine must be paused (See rvvm_pause_machine()), and is left paused
 *
 * Writes or reads RAM, hart state and every device state in a fixed section order
 *
 * A machine is only resumable from a snapshot of an identically constructed machine:
 * same RAM size, hart count and device set, as built by the same machine description
 *
 * Block devices backing the machine storage are not part of a snapshot and must be
 * restored to their matching state by the caller
 *
 * \param machine Machine handle
 * \param snap    Snapshot handle, opened for reading or writing
 * \return        Success (No IO error or corruption)
 */
RVVM_PUBLIC bool rvvm_machine_snapshot(rvvm_machine_t* machine, rvvm_snapshot_t* snap);

/** @}*/

RVVM_EXTERN_C_END

#endif
