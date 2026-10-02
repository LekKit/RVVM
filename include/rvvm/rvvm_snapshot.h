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
 * by a file, deduplicated image, provided over network, etc.
 *
 * Devices must serialize/deserialize their state from suspend callbacks.
 * Every device state should be stored in a separate, optionally versioned section.
 * Section order with same name is preserved in snapshot within constructed machine.
 *
 * Serialization direction will usually be transparent to device implementations,
 * which should only enumerate their fields once to support state read/write
 *
 * For backward-compatible additions to device state, section name should be retained,
 * with new, optional data stored after all backward-compatible state fields
 *
 * Upon IO error or reading corrupted data, any further operation will fail.
 * The deserialized state after failed rvvm_snapshot_close() is unspecified.
 *
 * This API is NOT thread-safe, but suspend callbacks guarantee exclusive caller.
 */

/**
 * Open snapshot around block device handle
 *
 * \param blk Block device handle
 * \param out Intent to write out snapshot state
 * \return    Snapshot handle (`NULL` on failure)
 */
RVVM_PUBLIC rvvm_snapshot_t* rvvm_snapshot_open(rvvm_blk_dev_t* blk, bool out);

/**
 * Close snapshot handle
 *
 * Synchronizes changes to backing storage after writing.
 * Returns sticky serialization failure flag.
 *
 * \param snap Snapshot handle
 * \return     Success
 */
RVVM_PUBLIC bool rvvm_snapshot_close(rvvm_snapshot_t* snap);

/**
 * Open next snapshot section, reset data pointer
 *
 * When writing to snapshot:
 * - Finalizes previous section, creates new section for writing
 * When reading from snapshot:
 * - Opens next section with matching name for reading
 *
 * For a missing section, all serialization calls will fail.
 * Failure is sticky and reported via final `rvvm_snapshot_close()`.
 *
 * \param snap Snapshot handle
 * \param name Section name for reading/writing
 */
RVVM_PUBLIC bool rvvm_snapshot_section(rvvm_snapshot_t* snap, const char* name);

/**
 * Check snapshot direction (Reading/Writing)
 *
 * \param snap Snapshot handle
 * \return     Writing state to snapshot
 */
RVVM_PUBLIC bool rvvm_snapshot_writing(rvvm_snapshot_t* snap);

/**
 * Read/Write opaque data to current snapshot section, advance pointer
 *
 * Direction is decided by current snapshot open mode.
 * Failure is sticky and reported via final `rvvm_snapshot_close()`.
 *
 * \param snap Snapshot handle
 * \param data Opaque data pointer
 * \param size Opaque data size
 */
RVVM_PUBLIC bool rvvm_snapshot_data(rvvm_snapshot_t* snap, void* data, size_t size);

/**
 * Read/Write host-endian variable to current snapshot section, advance pointer
 *
 * Serializes host-endian variable as little-endian for sizes of 1, 2, 4, 8.
 * Direction is decided by current snapshot mode.
 * Failure is sticky and reported via final `rvvm_snapshot_close()`.
 *
 * \param snap Snapshot handle
 * \param data Opaque data pointer
 * \param size Opaque data size
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

/** @}*/

RVVM_EXTERN_C_END

#endif
