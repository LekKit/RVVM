/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

package lekkit.rvvm;

public class HIDKeyboard {
    private RVVMMachine machine;
    private final long hid_keyboard;

    // Keyboard keycode definitions
    public static final byte HID_KEY_NONE = 0x00;

    // Typing keys
    public static final byte HID_KEY_A = 0x04;
    public static final byte HID_KEY_B = 0x05;
    public static final byte HID_KEY_C = 0x06;
    public static final byte HID_KEY_D = 0x07;
    public static final byte HID_KEY_E = 0x08;
    public static final byte HID_KEY_F = 0x09;
    public static final byte HID_KEY_G = 0x0a;
    public static final byte HID_KEY_H = 0x0b;
    public static final byte HID_KEY_I = 0x0c;
    public static final byte HID_KEY_J = 0x0d;
    public static final byte HID_KEY_K = 0x0e;
    public static final byte HID_KEY_L = 0x0f;
    public static final byte HID_KEY_M = 0x10;
    public static final byte HID_KEY_N = 0x11;
    public static final byte HID_KEY_O = 0x12;
    public static final byte HID_KEY_P = 0x13;
    public static final byte HID_KEY_Q = 0x14;
    public static final byte HID_KEY_R = 0x15;
    public static final byte HID_KEY_S = 0x16;
    public static final byte HID_KEY_T = 0x17;
    public static final byte HID_KEY_U = 0x18;
    public static final byte HID_KEY_V = 0x19;
    public static final byte HID_KEY_W = 0x1a;
    public static final byte HID_KEY_X = 0x1b;
    public static final byte HID_KEY_Y = 0x1c;
    public static final byte HID_KEY_Z = 0x1d;

    // Number keys
    public static final byte HID_KEY_1 = 0x1e;
    public static final byte HID_KEY_2 = 0x1f;
    public static final byte HID_KEY_3 = 0x20;
    public static final byte HID_KEY_4 = 0x21;
    public static final byte HID_KEY_5 = 0x22;
    public static final byte HID_KEY_6 = 0x23;
    public static final byte HID_KEY_7 = 0x24;
    public static final byte HID_KEY_8 = 0x25;
    public static final byte HID_KEY_9 = 0x26;
    public static final byte HID_KEY_0 = 0x27;

    // Control keys
    public static final byte HID_KEY_ENTER      = 0x28;
    public static final byte HID_KEY_ESC        = 0x29;
    public static final byte HID_KEY_BACKSPACE  = 0x2a;
    public static final byte HID_KEY_TAB        = 0x2b;
    public static final byte HID_KEY_SPACE      = 0x2c;
    public static final byte HID_KEY_MINUS      = 0x2d;
    public static final byte HID_KEY_EQUAL      = 0x2e;
    public static final byte HID_KEY_LEFTBRACE  = 0x2f; // Button [ {
    public static final byte HID_KEY_RIGHTBRACE = 0x30; // Button ] }
    public static final byte HID_KEY_BACKSLASH  = 0x31;
    public static final byte HID_KEY_HASHTILDE  = 0x32; // Button # ~ (Huh? Never seen one.)
    public static final byte HID_KEY_SEMICOLON  = 0x33; // Button ; :
    public static final byte HID_KEY_APOSTROPHE = 0x34; // Button ' "
    public static final byte HID_KEY_GRAVE      = 0x35; // Button ` ~ (For dummies: Quake console button)
    public static final byte HID_KEY_COMMA      = 0x36; // Button , <
    public static final byte HID_KEY_DOT        = 0x37; // Button . >
    public static final byte HID_KEY_SLASH      = 0x38;
    public static final byte HID_KEY_CAPSLOCK   = 0x39;

    public static final byte HID_KEY_LEFTCTRL   = (byte)0xe0;
    public static final byte HID_KEY_LEFTSHIFT  = (byte)0xe1;
    public static final byte HID_KEY_LEFTALT    = (byte)0xe2;
    public static final byte HID_KEY_LEFTMETA   = (byte)0xe3; // The one with the ugly Windows icon
    public static final byte HID_KEY_RIGHTCTRL  = (byte)0xe4;
    public static final byte HID_KEY_RIGHTSHIFT = (byte)0xe5;
    public static final byte HID_KEY_RIGHTALT   = (byte)0xe6;
    public static final byte HID_KEY_RIGHTMETA  = (byte)0xe7;

    // Function keys
    public static final byte HID_KEY_F1  = 0x3a;
    public static final byte HID_KEY_F2  = 0x3b;
    public static final byte HID_KEY_F3  = 0x3c;
    public static final byte HID_KEY_F4  = 0x3d;
    public static final byte HID_KEY_F5  = 0x3e;
    public static final byte HID_KEY_F6  = 0x3f;
    public static final byte HID_KEY_F7  = 0x40;
    public static final byte HID_KEY_F8  = 0x41;
    public static final byte HID_KEY_F9  = 0x42;
    public static final byte HID_KEY_F10 = 0x43;
    public static final byte HID_KEY_F11 = 0x44;
    public static final byte HID_KEY_F12 = 0x45;

    public static final byte HID_KEY_F13 = 0x68;
    public static final byte HID_KEY_F14 = 0x69;
    public static final byte HID_KEY_F15 = 0x6a;
    public static final byte HID_KEY_F16 = 0x6b;
    public static final byte HID_KEY_F17 = 0x6c;
    public static final byte HID_KEY_F18 = 0x6d;
    public static final byte HID_KEY_F19 = 0x6e;
    public static final byte HID_KEY_F20 = 0x6f;
    public static final byte HID_KEY_F21 = 0x70;
    public static final byte HID_KEY_F22 = 0x71;
    public static final byte HID_KEY_F23 = 0x72;
    public static final byte HID_KEY_F24 = 0x73;

    // Editing keys
    public static final byte HID_KEY_SYSRQ      = 0x46; // Print Screen (REISUB, anyone?)
    public static final byte HID_KEY_SCROLLLOCK = 0x47;
    public static final byte HID_KEY_PAUSE      = 0x48;
    public static final byte HID_KEY_INSERT     = 0x49;
    public static final byte HID_KEY_HOME       = 0x4a;
    public static final byte HID_KEY_PAGEUP     = 0x4b;
    public static final byte HID_KEY_DELETE     = 0x4c;
    public static final byte HID_KEY_END        = 0x4d;
    public static final byte HID_KEY_PAGEDOWN   = 0x4e;
    public static final byte HID_KEY_RIGHT      = 0x4f; // Right Arrow
    public static final byte HID_KEY_LEFT       = 0x50; // Left Arrow
    public static final byte HID_KEY_DOWN       = 0x51; // Down Arrow
    public static final byte HID_KEY_UP         = 0x52; // Up Arrow

    // Numpad keys
    public static final byte HID_KEY_NUMLOCK    = 0x53;
    public static final byte HID_KEY_KPSLASH    = 0x54;
    public static final byte HID_KEY_KPASTERISK = 0x55; // Button *
    public static final byte HID_KEY_KPMINUS    = 0x56;
    public static final byte HID_KEY_KPPLUS     = 0x57;
    public static final byte HID_KEY_KPENTER    = 0x58;
    public static final byte HID_KEY_KP1        = 0x59;
    public static final byte HID_KEY_KP2        = 0x5a;
    public static final byte HID_KEY_KP3        = 0x5b;
    public static final byte HID_KEY_KP4        = 0x5c;
    public static final byte HID_KEY_KP5        = 0x5d;
    public static final byte HID_KEY_KP6        = 0x5e;
    public static final byte HID_KEY_KP7        = 0x5f;
    public static final byte HID_KEY_KP8        = 0x60;
    public static final byte HID_KEY_KP9        = 0x61;
    public static final byte HID_KEY_KP0        = 0x62;
    public static final byte HID_KEY_KPDOT      = 0x63;

    // Special keys
    public static final byte HID_KEY_POWER      = 0x66;
    public static final byte HID_KEY_MENU       = 0x76;
    public static final byte HID_KEY_MUTE       = 0x7f;
    public static final byte HID_KEY_VOLUMEUP   = (byte)0x80;
    public static final byte HID_KEY_VOLUMEDOWN = (byte)0x81;

    public HIDKeyboard(RVVMMachine machine) {
        if (machine.isValid()) {
            this.machine = machine;
            this.hid_keyboard = RVVMNative.hid_keyboard_init_auto(machine.machine);
        } else {
            this.machine = null;
            this.hid_keyboard = 0;
        }
    }

    public void press(byte key) {
        if (hid_keyboard != 0) RVVMNative.hid_keyboard_press(hid_keyboard, key);
    }
    public void release(byte key) {
        if (hid_keyboard != 0) RVVMNative.hid_keyboard_release(hid_keyboard, key);
    }
}

