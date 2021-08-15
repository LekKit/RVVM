/*
x11keymap.h - X11 keycodes to PS2 keycodes mapping
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

#ifndef KEYMAP_H
#define KEYMAP_H

#include "hashmap.h"
#include "ps2-keyboard.h"

static hashmap_t keymap;

static void init_keymap()
{
    hashmap_init(&keymap, 64);
}

static void init_keycode(size_t keysym, size_t keycode, uint8_t len)
{
    hashmap_put(&keymap, keysym, (keycode << 8) | len);
}

static struct key keysym2makecode(size_t keysym)
{
    if (keysym == KEYMAP_PAUSE) {
        struct key k = {{0xE1, 0x14, 0x77, 0xE1, 0xF0, 0x14, 0xF0, 0x77}, 8};
        return k;
    } else if (keysym == KEYMAP_PRINT) {
        struct key k = {{0xE0, 0x12, 0xE0, 0x7C}, 4};
        return k;
    }
    struct key k = { 0 };
    size_t val = hashmap_get(&keymap, keysym);
    k.len = val & 0xFF;
    k.keycode[0] = (val >> 8) & 0xFF;
    k.keycode[1] = (val >> 16) & 0xFF;
    k.keycode[3] = (val >> 24) & 0xFF;
    return k;
}

#endif
