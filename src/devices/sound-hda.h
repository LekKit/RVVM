/*
sound-hda.h - HD Audio
Copyright (C) 2025  David Korenchuk <github.com/epoll-reactor-2>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#ifndef SOUND_HDA_H
#define SOUND_HDA_H

#include "rvvmlib.h"
#include "pci-bus.h"

typedef struct sound_subsystem_t sound_subsystem_t;

struct sound_subsystem_t {
    void *sound_data;
    void (*write)(sound_subsystem_t *subsystem, void *data, size_t size);
};

/*
 * Public callback type for library consumers who want to install their own
 * host-side audio sink without going through a compile-time USE_* backend.
 *
 * The HDA stream worker invokes this callback on its own thread with a chunk
 * of 16-bit signed little-endian PCM data — mono, 48 kHz, matching the
 * format the codec advertises. `user_data` is whatever pointer was passed
 * to sound_hda_init_ex / sound_hda_init_auto_ex.
 *
 * Useful for embedders routing audio into a non-native audio stack (a
 * managed runtime, an IPC channel, a WAV capture test harness, etc.)
 * without relying on a backend compiled into librvvm.
 */
typedef void (*sound_hda_backend_write_fn)(void *user_data, void *pcm_data, size_t size);

// Internal use
bool alsa_sound_init(sound_subsystem_t *sound);

PUBLIC pci_dev_t* sound_hda_init(pci_bus_t* pci_bus);
PUBLIC pci_dev_t* sound_hda_init_auto(rvvm_machine_t* machine);

/*
 * Extended init variants that let the caller plug in a custom host-side
 * audio sink. Pass NULL for `write_fn` to fall back to the compile-time
 * default (USE_ALSA if enabled, silent otherwise — same behaviour as
 * sound_hda_init / sound_hda_init_auto).
 *
 * When a non-NULL `write_fn` is supplied, the HDA device skips any
 * compiled-in backend and routes every PCM chunk through `write_fn`.
 */
PUBLIC pci_dev_t* sound_hda_init_ex(pci_bus_t* pci_bus,
                                    sound_hda_backend_write_fn write_fn,
                                    void* user_data);
PUBLIC pci_dev_t* sound_hda_init_auto_ex(rvvm_machine_t* machine,
                                         sound_hda_backend_write_fn write_fn,
                                         void* user_data);

#endif
