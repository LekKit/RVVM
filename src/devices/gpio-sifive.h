/*
gpio-sifive.h - SiFive GPIO Controller
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

#ifndef RVVM_GPIO_SIFIVE_H
#define RVVM_GPIO_SIFIVE_H

#include "rvvmlib.h"
#include "gpio_api.h"

#define GPIO_SIFIVE_PINS 32

#define GPIO_SIFIVE_DEFAULT_MMIO 0x10060000

PUBLIC rvvm_mmio_dev_t* gpio_sifive_init(rvvm_machine_t* machine, rvvm_gpio_dev_t* gpio,
                                         rvvm_addr_t base_addr, plic_ctx_t* plic, uint32_t* irqs);

PUBLIC rvvm_mmio_dev_t* gpio_sifive_init_auto(rvvm_machine_t* machine, rvvm_gpio_dev_t* gpio);

#endif
