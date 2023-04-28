/*
chardev.h - Character device backend for UART
Copyright (C) 2023  宋文武 <iyzsong@envs.net>
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

#ifndef RVVM_CHARDEV_H
#define RVVM_CHARDEV_H

#include "rvvmlib.h"

/*
 * IO Device - UART (or else) owning the chardev
 * Chardev - Terminal, emulated VT, socket
 */

typedef struct rvvm_chardev chardev_t;

struct rvvm_chardev {
    // IO Dev -> Chardev calls
    size_t (*read)(chardev_t* dev, void* buf, size_t nbytes);
    size_t (*write)(chardev_t* dev, const void* buf, size_t nbytes);
    uint32_t (*poll)(chardev_t* dev);

    // Chardev -> IO Device notifications (IRQ)
    void   (*notify)(void* io_dev, uint32_t flags);

    // Common RVVM API features
    void   (*update)(chardev_t* dev);
    void   (*remove)(chardev_t* dev);

    void* data;
    void* io_dev;
};

#define CHARDEV_RX 0x1
#define CHARDEV_TX 0x2

static inline size_t chardev_read(chardev_t* dev, void* buf, size_t nbytes)
{
    return dev->read(dev, buf, nbytes);
}

static inline size_t chardev_write(chardev_t* dev, const void* buf, size_t nbytes)
{
    return dev->write(dev, buf, nbytes);
}

static inline uint32_t chardev_poll(chardev_t* dev)
{
    return dev->poll(dev);
}

static inline void chardev_free(chardev_t* dev)
{
    if (dev->remove) dev->remove(dev);
}

static inline void chardev_update(chardev_t* dev)
{
    if (dev->update) dev->update(dev);
}

static inline void chardev_notify(chardev_t* dev, uint32_t flags)
{
    if (dev->notify) dev->notify(dev->io_dev, flags);
}

// Built-in chardev implementations

PUBLIC chardev_t* chardev_term_create(void);

#endif
