/*
gpio_api.h - General-Purpose IO API
Copyright (C) 2024  LekKit <github.com/LekKit>

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

#ifndef RVVM_GPIO_API_H
#define RVVM_GPIO_API_H

typedef struct rvvm_gpio_dev rvvm_gpio_dev_t;

struct rvvm_gpio_dev {
    // IO Dev -> GPIO Dev calls
    bool (*pins_out)(rvvm_gpio_dev_t* dev, size_t off, uint32_t pins);

    // GPIO Dev -> IO Dev calls
    bool     (*pins_in)(rvvm_gpio_dev_t* dev, size_t off, uint32_t pins);
    uint32_t (*pins_read)(rvvm_gpio_dev_t* dev, size_t off);

    // Common RVVM API features
    void (*update)(rvvm_gpio_dev_t* dev);
    void (*remove)(rvvm_gpio_dev_t* dev);

    void* data;
    void* io_dev;
};

static inline bool gpio_pins_out(rvvm_gpio_dev_t* dev, size_t off, uint32_t pins)
{
    if (dev) return dev->pins_out(dev, off, pins);
    return false;
}

static inline bool gpio_pins_in(rvvm_gpio_dev_t* dev, size_t off, uint32_t pins)
{
    if (dev) return dev->pins_in(dev, off, pins);
    return false;
}

static inline uint32_t gpio_pins_read(rvvm_gpio_dev_t* dev, size_t off)
{
    if (dev) return dev->pins_read(dev, off);
    return 0;
}

static inline void gpio_free(rvvm_gpio_dev_t* dev)
{
    if (dev && dev->remove) dev->remove(dev);
}

static inline void gpio_update(rvvm_gpio_dev_t* dev)
{
    if (dev && dev->update) dev->update(dev);
}

#endif
