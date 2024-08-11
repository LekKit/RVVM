/*
ps2-keyboard.c - PS2 Keyboard
Copyright (C) 2021  LekKit <github.com/LekKit>
                    cerg2010cerg2010 <github.com/cerg2010cerg2010>

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

#include "ps2-altera.h"
#include "hid_api.h"
#include "ringbuf.h"
#include "rvtimer.h"
#include "spinlock.h"
#include "mem_ops.h"
#include "utils.h"

#define PS2_CMD_RESET 0xFF
#define PS2_CMD_RESEND 0xFE
#define PS2_CMD_SET_DEFAULTS 0xF6
#define PS2_CMD_DISABLE_DATA_REPORTING 0xF5
#define PS2_CMD_ENABLE_DATA_REPORTING 0xF4
#define PS2_CMD_SET_SAMPLE_RATE 0xF3
#define PS2_CMD_GET_DEV_ID 0xF2
#define PS2_CMD_SET_SCAN_CODE_SET 0xF0
#define PS2_CMD_ECHO 0xEE
#define PS2_CMD_LEDS 0xED

#define PS2_RSP_ACK 0xFA
#define PS2_RSP_NAK 0xFE

#define PS2_STATE_CMD               0x0
#define PS2_STATE_SET_SAMPLE_RATE   0x1
#define PS2_STATE_SET_SCAN_CODE_SET 0x2
#define PS2_STATE_SET_LEDS          0x3

struct hid_keyboard {
    chardev_t chardev;
    spinlock_t lock;
    uint8_t key_state[32]; // State of all keys to prevent spurious repeat

    uint8_t state;  // The keyboard is a state machine
    uint8_t rate;   // Typematic rate in command encoding
    uint8_t delay;  // Typematic delay in command encoding (0.25sec * (delay + 1))
    bool reporting; // Data reporting enabled; needed for STATUS command

    // Last key pressed, used for typematic input
    const uint8_t* lastkey;
    size_t lastkey_size;

    // Used in IRQ handling for typematic (repeated) input
    rvtimer_t sample_timer;
    uint64_t sample_timecmp;

    ringbuf_t cmdbuf;
};

static const uint8_t hid_to_ps2_byte_map[] = {
    [HID_KEY_A] = 0x1C,
    [HID_KEY_B] = 0x32,
    [HID_KEY_C] = 0x21,
    [HID_KEY_D] = 0x23,
    [HID_KEY_E] = 0x24,
    [HID_KEY_F] = 0x2B,
    [HID_KEY_G] = 0x34,
    [HID_KEY_H] = 0x33,
    [HID_KEY_I] = 0x43,
    [HID_KEY_J] = 0x3B,
    [HID_KEY_K] = 0x42,
    [HID_KEY_L] = 0x4B,
    [HID_KEY_M] = 0x3A,
    [HID_KEY_N] = 0x31,
    [HID_KEY_O] = 0x44,
    [HID_KEY_P] = 0x4D,
    [HID_KEY_Q] = 0x15,
    [HID_KEY_R] = 0x2D,
    [HID_KEY_S] = 0x1B,
    [HID_KEY_T] = 0x2C,
    [HID_KEY_U] = 0x3C,
    [HID_KEY_V] = 0x2A,
    [HID_KEY_W] = 0x1D,
    [HID_KEY_X] = 0x22,
    [HID_KEY_Y] = 0x35,
    [HID_KEY_Z] = 0x1A,

    [HID_KEY_1] = 0x16,
    [HID_KEY_2] = 0x1E,
    [HID_KEY_3] = 0x26,
    [HID_KEY_4] = 0x25,
    [HID_KEY_5] = 0x2E,
    [HID_KEY_6] = 0x36,
    [HID_KEY_7] = 0x3D,
    [HID_KEY_8] = 0x3E,
    [HID_KEY_9] = 0x46,
    [HID_KEY_0] = 0x45,

    [HID_KEY_ENTER]      = 0x5A,
    [HID_KEY_ESC]        = 0x76,
    [HID_KEY_BACKSPACE]  = 0x66,
    [HID_KEY_TAB]        = 0x0D,
    [HID_KEY_SPACE]      = 0x29,
    [HID_KEY_MINUS]      = 0x4E,
    [HID_KEY_EQUAL]      = 0x55,
    [HID_KEY_LEFTBRACE]  = 0x54,
    [HID_KEY_RIGHTBRACE] = 0x5B,
    [HID_KEY_BACKSLASH]  = 0x5D,
    [HID_KEY_SEMICOLON]  = 0x4C,
    [HID_KEY_APOSTROPHE] = 0x52,
    [HID_KEY_GRAVE]      = 0x0E,
    [HID_KEY_COMMA]      = 0x41,
    [HID_KEY_DOT]        = 0x49,
    [HID_KEY_SLASH]      = 0x4A,
    [HID_KEY_CAPSLOCK]   = 0x58,

    [HID_KEY_LEFTCTRL]   = 0x14,
    [HID_KEY_LEFTSHIFT]  = 0x12,
    [HID_KEY_LEFTALT]    = 0x11,
    [HID_KEY_RIGHTSHIFT] = 0x59,

    [HID_KEY_F1]  = 0x05,
    [HID_KEY_F2]  = 0x06,
    [HID_KEY_F3]  = 0x04,
    [HID_KEY_F4]  = 0x0C,
    [HID_KEY_F5]  = 0x03,
    [HID_KEY_F6]  = 0x0B,
    [HID_KEY_F7]  = 0x83,
    [HID_KEY_F8]  = 0x0A,
    [HID_KEY_F9]  = 0x01,
    [HID_KEY_F10] = 0x09,
    [HID_KEY_F11] = 0x78,
    [HID_KEY_F12] = 0x07,

    [HID_KEY_SCROLLLOCK] = 0x7E,

    [HID_KEY_NUMLOCK]    = 0x77,
    [HID_KEY_KPASTERISK] = 0x7C,
    [HID_KEY_KPMINUS]    = 0x7B,
    [HID_KEY_KPPLUS]     = 0x79,
    [HID_KEY_KP1] = 0x69,
    [HID_KEY_KP2] = 0x72,
    [HID_KEY_KP3] = 0x7A,
    [HID_KEY_KP4] = 0x6B,
    [HID_KEY_KP5] = 0x73,
    [HID_KEY_KP6] = 0x74,
    [HID_KEY_KP7] = 0x6C,
    [HID_KEY_KP8] = 0x75,
    [HID_KEY_KP9] = 0x7D,
    [HID_KEY_KP0] = 0x70,
    [HID_KEY_KPDOT] = 0x71,
};

static void ps2_keyboard_set_rate(hid_keyboard_t* kb, uint8_t rate)
{
    kb->rate = rate & 0x1f;
    kb->delay = rate & 3;

    rvtimer_init(&kb->sample_timer, 1000);
    kb->sample_timecmp = (kb->delay + 1) * 250;
}

static void ps2_keyboard_defaults(hid_keyboard_t* kb)
{
    memset(&kb->key_state, 0, sizeof(kb->key_state));
    kb->state = PS2_STATE_CMD;
    kb->rate = 20;
    kb->delay = 1;
}

static bool ps2_keyboard_cmd(hid_keyboard_t* kb, uint8_t cmd)
{
    switch (cmd) {
        case PS2_CMD_RESET:
            ps2_keyboard_defaults(kb);
            ringbuf_put_u8(&kb->cmdbuf, PS2_RSP_ACK);
            ringbuf_put_u8(&kb->cmdbuf, 0xAA);
            return true;
        case PS2_CMD_RESEND:
            // Unimplemented
            return false;
        case PS2_CMD_SET_DEFAULTS:
            ps2_keyboard_defaults(kb);
            ringbuf_put_u8(&kb->cmdbuf, PS2_RSP_ACK);
            return true;
        case PS2_CMD_DISABLE_DATA_REPORTING:
            kb->reporting = false;
            ps2_keyboard_defaults(kb);
            ringbuf_put_u8(&kb->cmdbuf, PS2_RSP_ACK);
            return true;
        case PS2_CMD_ENABLE_DATA_REPORTING:
            kb->reporting = true;
            ringbuf_put_u8(&kb->cmdbuf, PS2_RSP_ACK);
            return true;
        case PS2_CMD_SET_SAMPLE_RATE:
            kb->state = PS2_STATE_SET_SAMPLE_RATE;
            ringbuf_put_u8(&kb->cmdbuf, PS2_RSP_ACK);
            return true;
        case PS2_CMD_GET_DEV_ID:
            ringbuf_put_u8(&kb->cmdbuf, PS2_RSP_ACK);
            ringbuf_put_u8(&kb->cmdbuf, 0xAB);
            ringbuf_put_u8(&kb->cmdbuf, 0x83);
            return true;
        case PS2_CMD_SET_SCAN_CODE_SET:
            kb->state = PS2_STATE_SET_SCAN_CODE_SET;
            ringbuf_put_u8(&kb->cmdbuf, PS2_RSP_ACK);
            return true;
        case PS2_CMD_ECHO:
            ringbuf_put_u8(&kb->cmdbuf, 0xEE);
            return true;
        case PS2_CMD_LEDS:
            kb->state = PS2_STATE_SET_LEDS;
            ringbuf_put_u8(&kb->cmdbuf, PS2_RSP_ACK);
            return true;
        default:
            ringbuf_put_u8(&kb->cmdbuf, PS2_RSP_NAK);
            return true;
    }
}

static size_t ps2_keyboard_read(chardev_t* dev, void* buf, size_t size)
{
    hid_keyboard_t* kb = dev->data;
    spin_lock(&kb->lock);
    size_t ret = ringbuf_read(&kb->cmdbuf, buf, size);
    spin_unlock(&kb->lock);
    return ret;
}

static size_t ps2_keyboard_write(chardev_t* dev, const void* buf, size_t size)
{
    hid_keyboard_t* kb = dev->data;
    spin_lock(&kb->lock);
    for (size_t i=0; i<size; ++i) {
        uint8_t val = ((const uint8_t*)buf)[i];

        switch (kb->state) {
            case PS2_STATE_CMD:
                ps2_keyboard_cmd(kb, val);
                break;
            case PS2_STATE_SET_SAMPLE_RATE:
                ps2_keyboard_set_rate(kb, val);
                kb->state = PS2_STATE_CMD;
                ringbuf_put_u8(&kb->cmdbuf, PS2_RSP_ACK);
                break;
            case PS2_STATE_SET_SCAN_CODE_SET:
                if (val == 0) {
                    ringbuf_put_u8(&kb->cmdbuf, PS2_RSP_ACK);
                    ringbuf_put_u8(&kb->cmdbuf, 2);
                } else if (val == 2) {
                    ringbuf_put_u8(&kb->cmdbuf, PS2_RSP_ACK);
                } else {
                    ringbuf_put_u8(&kb->cmdbuf, PS2_RSP_NAK);
                }
                kb->state = PS2_STATE_CMD;
                ringbuf_put_u8(&kb->cmdbuf, PS2_RSP_ACK);
                break;
            case PS2_STATE_SET_LEDS:
                // leds are ignored
                kb->state = PS2_STATE_CMD;
                ringbuf_put_u8(&kb->cmdbuf, PS2_RSP_ACK);
                break;
        }
    }
    spin_unlock(&kb->lock);
    chardev_notify(&kb->chardev, CHARDEV_RX);
    return size;
}

static void ps2_keyboard_remove(chardev_t* dev)
{
    hid_keyboard_t* kb = dev->data;
    ringbuf_destroy(&kb->cmdbuf);
    free(kb);
}

static const uint16_t ps2kb_rate2realrate[32] = {
    [0]  = 300,
    [1]  = 267,
    [2]  = 240,
    [3]  = 218,
    [4]  = 200,
    [5]  = 185,
    [6]  = 171,
    [7]  = 160,
    [8]  = 150,
    [9]  = 133,
    [10] = 120,
    [11] = 109,
    [12] = 100,
    [13] = 92,
    [14] = 86,
    [15] = 80,
    [16] = 75,
    [17] = 67,
    [18] = 60,
    [19] = 55,
    [20] = 50,
    [21] = 46,
    [22] = 43,
    [23] = 40,
    [24] = 37,
    [25] = 33,
    [26] = 30,
    [27] = 28,
    [28] = 25,
    [29] = 23,
    [30] = 21,
    [31] = 20,
};

static void ps2_keyboard_update(chardev_t* dev)
{
    // Handle typematic
    hid_keyboard_t* kb = dev->data;
    spin_lock(&kb->lock);
    if (kb->reporting && kb->lastkey_size && rvtimer_get(&kb->sample_timer) >= kb->sample_timecmp) {
        rvtimer_init(&kb->sample_timer, ps2kb_rate2realrate[kb->rate]);
        kb->sample_timecmp = 10;
        ringbuf_put(&kb->cmdbuf, kb->lastkey, kb->lastkey_size);
        chardev_notify(&kb->chardev, CHARDEV_RX);
    }
    spin_unlock(&kb->lock);
}

PUBLIC hid_keyboard_t* hid_keyboard_init_auto_ps2(rvvm_machine_t* machine)
{
    plic_ctx_t* plic = rvvm_get_plic(machine);
    rvvm_addr_t addr = rvvm_mmio_zone_auto(machine, 0x20001000, ALTPS2_MMIO_SIZE);
    hid_keyboard_t* kb = safe_new_obj(hid_keyboard_t);

    kb->chardev.read = ps2_keyboard_read;
    kb->chardev.write = ps2_keyboard_write;
    kb->chardev.remove = ps2_keyboard_remove;
    kb->chardev.update = ps2_keyboard_update;
    kb->chardev.data = kb;

    ringbuf_create(&kb->cmdbuf, 1024);
    ringbuf_put_u8(&kb->cmdbuf, 0xAA);

    altps2_init(machine, addr, plic, plic_alloc_irq(plic), &kb->chardev);
    return kb;
}

static const uint8_t* hid_to_ps2_keycode(hid_key_t key, size_t* size)
{
    if (key < sizeof(hid_to_ps2_byte_map) && hid_to_ps2_byte_map[key]) {
        // Convert small & common keycodes using a table, fallback to switch
        *size = 1;
        return &hid_to_ps2_byte_map[key];
    } else {
        switch (key) {
            case HID_KEY_LEFTMETA:
                *size = 2;
                return (const uint8_t*)"\xE0\x1F";
            case HID_KEY_RIGHTCTRL:
                *size = 2;
                return (const uint8_t*)"\xE0\x14";
            case HID_KEY_RIGHTALT:
                *size = 2;
                return (const uint8_t*)"\xE0\x11";
            case HID_KEY_RIGHTMETA:
                *size = 2;
                return (const uint8_t*)"\xE0\x27";
            case HID_KEY_SYSRQ:
                *size = 4;
                return (const uint8_t*)"\xE0\x12\xE0\x7C";
            case HID_KEY_PAUSE:
                *size = 8;
                return (const uint8_t*)"\xE1\x14\x77\xE1\xF0\x14\xF0\x77";
            case HID_KEY_INSERT:
                *size = 2;
                return (const uint8_t*)"\xE0\x70";
            case HID_KEY_HOME:
                *size = 2;
                return (const uint8_t*)"\xE0\x6C";
            case HID_KEY_PAGEUP:
                *size = 2;
                return (const uint8_t*)"\xE0\x7D";
            case HID_KEY_DELETE:
                *size = 2;
                return (const uint8_t*)"\xE0\x71";
            case HID_KEY_END:
                *size = 2;
                return (const uint8_t*)"\xE0\x69";
            case HID_KEY_PAGEDOWN:
                *size = 2;
                return (const uint8_t*)"\xE0\x7A";
            case HID_KEY_RIGHT:
                *size = 2;
                return (const uint8_t*)"\xE0\x74";
            case HID_KEY_LEFT:
                *size = 2;
                return (const uint8_t*)"\xE0\x6B";
            case HID_KEY_DOWN:
                *size = 2;
                return (const uint8_t*)"\xE0\x72";
            case HID_KEY_UP:
                *size = 2;
                return (const uint8_t*)"\xE0\x75";
            case HID_KEY_MENU:
                *size = 2;
                return (const uint8_t*)"\xE0\x2F";
            case HID_KEY_KPSLASH:
                *size = 2;
                return (const uint8_t*)"\xE0\x4A";
            case HID_KEY_KPENTER:
                *size = 2;
                return (const uint8_t*)"\xE0\x5A";
            default:
                return NULL;
        }
    }
}

static void ps2_handle_keyboard(hid_keyboard_t* kb, hid_key_t key, bool pressed)
{
    spin_lock(&kb->lock);
    // Ignore repeated press/release events
    bool key_state = !!(kb->key_state[key >> 3] & (1 << (key & 0x7)));
    if (key != HID_KEY_NONE && key_state != pressed && kb->reporting) {
        size_t keycode_size = 0;
        const uint8_t* keycode = hid_to_ps2_keycode(key, &keycode_size);

        if (keycode) {
            // Send key event to the guest
            if (pressed) {
                kb->key_state[key >> 3] |= (1 << (key & 0x7));
                kb->lastkey = keycode;
                kb->lastkey_size = keycode_size;

                ringbuf_put(&kb->cmdbuf, keycode, keycode_size);
                rvtimer_init(&kb->sample_timer, 1000);
                kb->sample_timecmp = (kb->delay + 1) * 250;
            } else {
                uint8_t keycmd[8];
                uint8_t keylen = 0;
                kb->key_state[key >> 3] &= ~(1 << (key & 0x7));
                if (kb->lastkey == keycode) kb->lastkey_size = 0;

                if (keycode_size == 1) {
                    keycmd[0] = 0xF0;
                    keycmd[1] = keycode[0];
                    keylen = 2;
                } else if (keycode_size == 2 && keycode[0] == 0xE0) {
                    keycmd[0] = 0xE0;
                    keycmd[1] = 0xF0;
                    keycmd[2] = keycode[1];
                    keylen = 3;
                } else if (keycode_size == 4 && keycode[0] == 0xE0 && keycode[2] == 0xE0) {
                    // Print screen is special
                    keycmd[0] = 0xE0;
                    keycmd[1] = 0xF0;
                    keycmd[2] = keycode[3];
                    keycmd[3] = 0xE0;
                    keycmd[4] = 0xF0;
                    keycmd[5] = keycode[1];
                    keylen = 6;
                }
                ringbuf_put(&kb->cmdbuf, keycmd, keylen);
            }
            chardev_notify(&kb->chardev, CHARDEV_RX);
        }
    }
    spin_unlock(&kb->lock);
}

PUBLIC void hid_keyboard_press_ps2(hid_keyboard_t* kb, hid_key_t key)
{
    ps2_handle_keyboard(kb, key, true);
}

PUBLIC void hid_keyboard_release_ps2(hid_keyboard_t* kb, hid_key_t key)
{
    ps2_handle_keyboard(kb, key, false);
}
