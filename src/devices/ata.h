/*
ata.h - ATA disk controller
Copyright (C) 2021  cerg2010cerg2010 <github.com/cerg2010cerg2010>

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

#ifndef ATA_H
#define ATA_H

#include "rvvm.h"
#include "rvvm_types.h"

#include <stdio.h>

void ata_init(rvvm_machine_t* machine, paddr_t data_base_addr, paddr_t ctl_base_addr, FILE* fp0, FILE* fp1);

#endif
