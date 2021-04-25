/*
ps2-keyboard.h - PS2 Keyboard
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

#ifndef PS2_KEYBOARD_H
#define PS2_KEYBOARD_H
#include "ps2-altera.h"

/* size of one key, in bytes */
#define KEY_SIZE 8

struct key {
	uint8_t keycode[KEY_SIZE];
	uint8_t len;
};

struct ps2_device ps2_keyboard_create();
void ps2_handle_keyboard(struct ps2_device *ps2keyboard, struct key *key, bool pressed);
#endif
