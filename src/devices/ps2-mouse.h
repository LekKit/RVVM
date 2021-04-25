/*
ps2-mouse.h - PS2 Mouse
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

#ifndef PS2_MOUSE_H
#define PS2_MOUSE_H
#include "ps2-altera.h"

struct mouse_btns {
	bool left : 1;
	bool right : 1;
	bool middle : 1;
	/* maybe will add more later */
};

struct ps2_device ps2_mouse_create();
void ps2_handle_mouse(struct ps2_device *ps2mouse, int x, int y, struct mouse_btns *btns);
#endif
