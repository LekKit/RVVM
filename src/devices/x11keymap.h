/*
x11keymap.h - X11 keycodes to PS2 keycodes mapping
Copyright (C) 2021  cerg2010cerg2010 <github.com/cerg2010cerg2010>
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

#ifndef X11KEYMAP_H
#define X11KEYMAP_H

#include <X11/keysym.h>

#define KEYMAP_PAUSE XK_Pause
#define KEYMAP_PRINT XK_Print

#include "keymap.h"

static void init_keycodes()
{
    init_keymap();
    
    init_keycode(XK_a, 0x1C, 1);
    init_keycode(XK_b, 0x32, 1);
    init_keycode(XK_c, 0x21, 1);
    init_keycode(XK_d, 0x23, 1);
    init_keycode(XK_e, 0x24, 1);
    init_keycode(XK_f, 0x2B, 1);
    init_keycode(XK_g, 0x34, 1);
    init_keycode(XK_h, 0x33, 1);
    init_keycode(XK_i, 0x43, 1);
    init_keycode(XK_j, 0x3B, 1);
    init_keycode(XK_k, 0x42, 1);
    init_keycode(XK_l, 0x4B, 1);
    init_keycode(XK_m, 0x3A, 1);
    init_keycode(XK_n, 0x31, 1);
    init_keycode(XK_o, 0x44, 1);
    init_keycode(XK_p, 0x4D, 1);
    init_keycode(XK_q, 0x15, 1);
    init_keycode(XK_r, 0x2D, 1);
    init_keycode(XK_s, 0x1B, 1);
    init_keycode(XK_t, 0x2C, 1);
    init_keycode(XK_u, 0x3C, 1);
    init_keycode(XK_v, 0x2A, 1);
    init_keycode(XK_w, 0x1D, 1);
    init_keycode(XK_x, 0x22, 1);
    init_keycode(XK_y, 0x35, 1);
    init_keycode(XK_z, 0x1A, 1);
    init_keycode(XK_0, 0x45, 1);
    init_keycode(XK_1, 0x16, 1);
    init_keycode(XK_2, 0x1E, 1);
    init_keycode(XK_3, 0x26, 1);
    init_keycode(XK_4, 0x25, 1);
    init_keycode(XK_5, 0x2E, 1);
    init_keycode(XK_6, 0x36, 1);
    init_keycode(XK_7, 0x3D, 1);
    init_keycode(XK_8, 0x3E, 1);
    init_keycode(XK_9, 0x46, 1);
    init_keycode(XK_grave, 0x0E, 1);
    init_keycode(XK_minus, 0x4E, 1);
    init_keycode(XK_equal, 0x55, 1);
    init_keycode(XK_backslash, 0x5D, 1);
    init_keycode(XK_BackSpace, 0x66, 1);
    init_keycode(XK_space, 0x29, 1);
    init_keycode(XK_Tab, 0x0D, 1);
    init_keycode(XK_Caps_Lock, 0x58, 1);
    init_keycode(XK_Shift_L, 0x12, 1);
    init_keycode(XK_Control_L, 0x14, 1);
    init_keycode(XK_Super_L, 0x1FE0, 2);
    init_keycode(XK_Alt_L, 0x11, 1);
    init_keycode(XK_Shift_R, 0x59, 1);
    init_keycode(XK_Control_R, 0x14E0, 2);
    init_keycode(XK_Super_R, 0x27E0, 2);
    init_keycode(XK_Alt_R, 0x11E0, 2);
    init_keycode(XK_Menu, 0x2FE0, 2);
    init_keycode(XK_Return, 0x5A, 1);
    init_keycode(XK_Escape, 0x76, 1);
    init_keycode(XK_F1, 0x05, 1);
    init_keycode(XK_F2, 0x06, 1);
    init_keycode(XK_F3, 0x04, 1);
    init_keycode(XK_F4, 0x0C, 1);
    init_keycode(XK_F5, 0x03, 1);
    init_keycode(XK_F6, 0x0B, 1);
    init_keycode(XK_F7, 0x83, 1);
    init_keycode(XK_F8, 0x0A, 1);
    init_keycode(XK_F9, 0x01, 1);
    init_keycode(XK_F10, 0x09, 1);
    init_keycode(XK_F11, 0x78, 1);
    init_keycode(XK_F12, 0x07, 1);
    /* XK_Print is too big, handled separately */
    init_keycode(XK_Scroll_Lock, 0x7E, 1);
    /* XK_Pause is too big, handled separately */
    init_keycode(XK_bracketleft, 0x54, 1);
    init_keycode(XK_Insert, 0x70E0, 2);
    init_keycode(XK_Home, 0x6CE0, 2);
    init_keycode(XK_Page_Up, 0x7DE0, 2);
    init_keycode(XK_Delete, 0x71E0, 2);
    init_keycode(XK_End, 0x69E0, 2);
    init_keycode(XK_Page_Down, 0x7AE0, 2);
    init_keycode(XK_Up, 0x75E0, 2);
    init_keycode(XK_Left, 0x6BE0, 2);
    init_keycode(XK_Down, 0x72E0, 2);
    init_keycode(XK_Right, 0x74E0, 2);
    init_keycode(XK_Num_Lock, 0x77, 1);
    init_keycode(XK_KP_Divide, 0x4AE0, 2);
    init_keycode(XK_KP_Multiply, 0x7C, 1);
    init_keycode(XK_KP_Subtract, 0x7B, 1);
    init_keycode(XK_KP_Add, 0x79, 1);
    init_keycode(XK_KP_Enter, 0x5AE0, 2);
    init_keycode(XK_KP_Decimal, 0x71, 1); init_keycode(XK_KP_Delete, 0x71, 1);
    init_keycode(XK_KP_0, 0x70, 1); init_keycode(XK_KP_Insert, 0x70, 1);
    init_keycode(XK_KP_1, 0x69, 1); init_keycode(XK_KP_End, 0x69, 1);
    init_keycode(XK_KP_2, 0x72, 1); init_keycode(XK_KP_Down, 0x72, 1);
    init_keycode(XK_KP_3, 0x7A, 1); init_keycode(XK_KP_Page_Down, 0x7A, 1);
    init_keycode(XK_KP_4, 0x6B, 1); init_keycode(XK_KP_Left, 0x6B, 1);
    init_keycode(XK_KP_5, 0x73, 1); init_keycode(XK_KP_Begin, 0x73, 1);
    init_keycode(XK_KP_6, 0x74, 1); init_keycode(XK_KP_Right, 0x74, 1);
    init_keycode(XK_KP_7, 0x6C, 1); init_keycode(XK_KP_Home, 0x6C, 1);
    init_keycode(XK_KP_8, 0x75, 1); init_keycode(XK_KP_Up, 0x75, 1);
    init_keycode(XK_KP_9, 0x7D, 1); init_keycode(XK_KP_Page_Up, 0x7D, 1);
    init_keycode(XK_bracketright, 0x5B, 1);
    init_keycode(XK_semicolon, 0x4C, 1);
    init_keycode(XK_apostrophe, 0x52, 1);
    init_keycode(XK_comma, 0x41, 1);
    init_keycode(XK_period, 0x49, 1);
    init_keycode(XK_slash, 0x4A, 1);
}

#endif
