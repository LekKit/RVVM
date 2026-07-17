/*
parport-pci.h - PCI Parallel Port (NetMos/MosChip MCS9900)
Copyright (C) 2026  Sol Astrius <sol@astrius.ink>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#ifndef PARPORT_PCI_H
#define PARPORT_PCI_H

#include "rvvmlib.h"
#include "pci-bus.h"

/*
 * Backend callbacks for the emulated NetMos 9900 PCI parallel port.
 *
 * Forward path (guest → host):
 *   write_fn fires each time the guest pulses the Centronics strobe in
 *   forward (compat) mode. Called from the MMIO write path with no
 *   device lock held — the backend may block briefly on a host file or
 *   pipe write.
 *
 * Reverse path (host → guest):
 *   The host pushes bytes into the device via parport_pci_inject_byte().
 *   Bytes queue in a small ring; the IEEE 1284 nibble-mode reverse
 *   handshake delivers them to the guest as it pulls. The guest sees
 *   nFault deasserted (the Error status bit set) when the ring is empty,
 *   which an IEEE 1284 host recognizes as "no more data".
 */
typedef void (*parport_pci_write_fn)(void *user_data, uint8_t byte);

/*
 * Attach a NetMos 9900 PCI parport to the given PCI bus. Single port,
 * SPP forward + IEEE 1284 nibble-mode reverse. A host parallel-port
 * driver picks it up via PCI ID match (vendor 0x9710 device 0x9900) and
 * exposes the usual parport / lp / ppdev nodes (negotiating to nibble
 * mode enables bidirectional reads). EPP/ECP register windows return 0.
 *
 * write_fn / user_data: optional. If NULL, forward bytes are silently
 * dropped after the Centronics strobe. Useful for headless testing.
 */
PUBLIC pci_dev_t* parport_pci_init(pci_bus_t* pci_bus,
                                   parport_pci_write_fn write_fn,
                                   void* user_data);

PUBLIC pci_dev_t* parport_pci_init_auto(rvvm_machine_t* machine,
                                        parport_pci_write_fn write_fn,
                                        void* user_data);

/*
 * Inject a byte from the host into the parport's reverse-channel ring.
 * Returns true on success, false if the ring is full (caller should
 * back off and retry). The ring holds 256 bytes; under typical 1284
 * nibble timing the guest drains roughly 50 KB/s, so a backend that
 * paces its writes against this rate is fine without flow control.
 *
 * Safe to call from any thread; takes the device's spinlock briefly.
 */
PUBLIC bool parport_pci_inject_byte(pci_dev_t* dev, uint8_t byte);

#endif
