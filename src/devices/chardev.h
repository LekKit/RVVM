/*
chardev.h - backend for UART
Copyright (C) 2023  宋文武 <iyzsong@envs.net>

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

#ifndef CHARDEV_H
#define CHARDEV_H

#include "rvvmlib.h"

typedef struct chardev chardev_t;
struct chardev {
    size_t (*read)(chardev_t* dev, void* buf, size_t nbytes);
    size_t (*write)(chardev_t* dev, const void* buf, size_t nbytes);
    void (*destroy)(chardev_t* dev);
    void (*watch)(chardev_t* dev,
                  void (*on_input_available)(chardev_t* dev, void* watcher_data),
                  void (*on_output_available)(chardev_t* dev, void* watcher_data),
                  void* watcher_data);
    void* data;
};

static inline size_t chardev_read(chardev_t* dev, void* buf, size_t nbytes)
{
    return dev->read(dev, buf, nbytes);
}
static inline size_t chardev_write(chardev_t* dev, const void* buf, size_t nbytes)
{
    return dev->write(dev, buf, nbytes);
}
static inline void chardev_watch(chardev_t* dev,
                                 void (*on_input_available)(chardev_t* dev, void* watcher_data),
                                 void (*on_output_available)(chardev_t* dev, void* watcher_data),
                                 void* watcher_data)
{
    return dev->watch(dev, on_input_available, on_output_available, watcher_data);
}
static inline void chardev_destroy(chardev_t* dev)
{
    return dev->destroy(dev);
}

#endif
