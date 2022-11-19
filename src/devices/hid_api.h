/*
hid_api.h - Human Interface Devices API
Copyright (C) 2022  LekKit <github.com/LekKit>

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
 
#ifndef RVVM_HID_API_H
#define RVVM_HID_API_H

#include "rvvmlib.h"

typedef uint8_t hid_key_t;
typedef uint8_t hid_btns_t; // Button states bitfield
typedef struct hid_keyboard hid_keyboard_t;
typedef struct hid_mouse    hid_mouse_t;

// Automatically initialize & attach HID devices to the machine
// After rvvm_machine_free(), devices are cleaned up too,
// and the handles are no longer valid
PUBLIC hid_keyboard_t* hid_keyboard_init_auto(rvvm_machine_t* machine);
PUBLIC hid_mouse_t*    hid_mouse_init_auto(rvvm_machine_t* machine);

// These may be called from GUI or whatever
PUBLIC void hid_keyboard_press(hid_keyboard_t* kb, hid_key_t key);
PUBLIC void hid_keyboard_release(hid_keyboard_t* kb, hid_key_t key);

// TODO: Keymap API to translate system keycodes transparently?
// Sounds like we can simply use a hashmap internally, but can our users?

PUBLIC void hid_mouse_press(hid_mouse_t* mouse, hid_btns_t btns);
PUBLIC void hid_mouse_release(hid_mouse_t* mouse, hid_btns_t btns);

// Mouse wheel scrolling
PUBLIC void hid_mouse_scroll(hid_mouse_t* mouse, int32_t offset);

// Relative movement
PUBLIC void hid_mouse_move(hid_mouse_t* mouse, int32_t x, int32_t y);

// Absolute movement (Seamless cursor integration for tablets, etc)
PUBLIC void hid_mouse_place(hid_mouse_t* mouse, int32_t x, int32_t y);

// Mouse definitions
#define HID_BTN_NONE    0x0
#define HID_BTN_LEFT    0x1
#define HID_BTN_RIGHT   0x2
#define HID_BTN_MIDDLE  0x4

#define HID_SCROLL_UP   -1
#define HID_SCROLL_DOWN 1

// Keyboard keycode definitions
#define HID_KEY_NONE 0x00

// Typing keys
#define HID_KEY_A 0x04
#define HID_KEY_B 0x05
#define HID_KEY_C 0x06
#define HID_KEY_D 0x07
#define HID_KEY_E 0x08
#define HID_KEY_F 0x09
#define HID_KEY_G 0x0a
#define HID_KEY_H 0x0b
#define HID_KEY_I 0x0c
#define HID_KEY_J 0x0d
#define HID_KEY_K 0x0e
#define HID_KEY_L 0x0f
#define HID_KEY_M 0x10
#define HID_KEY_N 0x11
#define HID_KEY_O 0x12
#define HID_KEY_P 0x13
#define HID_KEY_Q 0x14
#define HID_KEY_R 0x15
#define HID_KEY_S 0x16
#define HID_KEY_T 0x17
#define HID_KEY_U 0x18
#define HID_KEY_V 0x19
#define HID_KEY_W 0x1a
#define HID_KEY_X 0x1b
#define HID_KEY_Y 0x1c
#define HID_KEY_Z 0x1d

// Number keys
#define HID_KEY_1 0x1e
#define HID_KEY_2 0x1f
#define HID_KEY_3 0x20
#define HID_KEY_4 0x21
#define HID_KEY_5 0x22
#define HID_KEY_6 0x23
#define HID_KEY_7 0x24
#define HID_KEY_8 0x25
#define HID_KEY_9 0x26
#define HID_KEY_0 0x27

// Control keys
#define HID_KEY_ENTER      0x28
#define HID_KEY_ESC        0x29
#define HID_KEY_BACKSPACE  0x2a
#define HID_KEY_TAB        0x2b
#define HID_KEY_SPACE      0x2c
#define HID_KEY_MINUS      0x2d
#define HID_KEY_EQUAL      0x2e
#define HID_KEY_LEFTBRACE  0x2f // Button [ {
#define HID_KEY_RIGHTBRACE 0x30 // Button ] }
#define HID_KEY_BACKSLASH  0x31
#define HID_KEY_HASHTILDE  0x32 // Button # ~ (Huh? Never seen one.)
#define HID_KEY_SEMICOLON  0x33 // Button ; :
#define HID_KEY_APOSTROPHE 0x34 // Button ' "
#define HID_KEY_GRAVE      0x35 // Button ` ~ (For dummies: Quake console button)
#define HID_KEY_COMMA      0x36 // Button , <
#define HID_KEY_DOT        0x37 // Button . >
#define HID_KEY_SLASH      0x38
#define HID_KEY_CAPSLOCK   0x39

#define HID_KEY_LEFTCTRL   0xe0
#define HID_KEY_LEFTSHIFT  0xe1
#define HID_KEY_LEFTALT    0xe2
#define HID_KEY_LEFTMETA   0xe3 // The one with the ugly Windows icon
#define HID_KEY_RIGHTCTRL  0xe4
#define HID_KEY_RIGHTSHIFT 0xe5
#define HID_KEY_RIGHTALT   0xe6
#define HID_KEY_RIGHTMETA  0xe7

// Function keys
#define HID_KEY_F1  0x3a
#define HID_KEY_F2  0x3b
#define HID_KEY_F3  0x3c
#define HID_KEY_F4  0x3d
#define HID_KEY_F5  0x3e
#define HID_KEY_F6  0x3f
#define HID_KEY_F7  0x40
#define HID_KEY_F8  0x41
#define HID_KEY_F9  0x42
#define HID_KEY_F10 0x43
#define HID_KEY_F11 0x44
#define HID_KEY_F12 0x45

#define HID_KEY_F13 0x68
#define HID_KEY_F14 0x69
#define HID_KEY_F15 0x6a
#define HID_KEY_F16 0x6b
#define HID_KEY_F17 0x6c
#define HID_KEY_F18 0x6d
#define HID_KEY_F19 0x6e
#define HID_KEY_F20 0x6f
#define HID_KEY_F21 0x70
#define HID_KEY_F22 0x71
#define HID_KEY_F23 0x72
#define HID_KEY_F24 0x73

// Editing keys
#define HID_KEY_SYSRQ      0x46 // Print Screen (REISUB, anyone?)
#define HID_KEY_SCROLLLOCK 0x47
#define HID_KEY_PAUSE      0x48
#define HID_KEY_INSERT     0x49
#define HID_KEY_HOME       0x4a
#define HID_KEY_PAGEUP     0x4b
#define HID_KEY_DELETE     0x4c
#define HID_KEY_END        0x4d
#define HID_KEY_PAGEDOWN   0x4e
#define HID_KEY_RIGHT      0x4f // Right Arrow
#define HID_KEY_LEFT       0x50 // Left Arrow
#define HID_KEY_DOWN       0x51 // Down Arrow
#define HID_KEY_UP         0x52 // Up Arrow

// Numpad keys
#define HID_KEY_NUMLOCK    0x53
#define HID_KEY_KPSLASH    0x54
#define HID_KEY_KPASTERISK 0x55 // Button *
#define HID_KEY_KPMINUS    0x56
#define HID_KEY_KPPLUS     0x57
#define HID_KEY_KPENTER    0x58
#define HID_KEY_KP1        0x59
#define HID_KEY_KP2        0x5a
#define HID_KEY_KP3        0x5b
#define HID_KEY_KP4        0x5c
#define HID_KEY_KP5        0x5d
#define HID_KEY_KP6        0x5e
#define HID_KEY_KP7        0x5f
#define HID_KEY_KP8        0x60
#define HID_KEY_KP9        0x61
#define HID_KEY_KP0        0x62
#define HID_KEY_KPDOT      0x63

// Special keys
#define HID_KEY_POWER      0x66
#define HID_KEY_MENU       0x76
#define HID_KEY_MUTE       0x7f
#define HID_KEY_VOLUMEUP   0x80
#define HID_KEY_VOLUMEDOWN 0x81

#endif
