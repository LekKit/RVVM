/*
mtd-physmap.h - Memory Technology Device Mapping
Copyright (C) 2023  LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#ifndef RVVM_MTD_PHYSMAP_H
#define RVVM_MTD_PHYSMAP_H

#include <rvvm/rvvm_blk.h>

/*
 * TODO: Remove this in favor of <rvvm/rvvm_board.h>
 */

/*
 * The main purpose of this device is to allow guests to flash
 * different firmware into the board memory chip
 */

RVVM_PUBLIC rvvm_reg_dev_t* mtd_ram_init_blk(rvvm_machine_t* machine, /**/
                                             rvvm_addr_t     addr,    /**/
                                             rvvm_blk_dev_t* blk);

static inline rvvm_reg_dev_t* mtd_physmap_init(rvvm_machine_t* machine, /**/
                                               const char*     image,   /**/
                                               rvvm_addr_t     addr,    /**/
                                               bool            fw)
{
    (void)fw;
    rvvm_blk_dev_t* blk = rvvm_blk_open(image, NULL, RVVM_BLK_RW);
    if (blk) {
        return mtd_ram_init_blk(machine, addr, blk);
    }
    return NULL;
}

static inline rvvm_reg_dev_t* mtd_physmap_init_auto(rvvm_machine_t* machine, const char* image, bool fw)
{
    return mtd_physmap_init(machine, image, 0x04000000UL, fw);
}

#endif
