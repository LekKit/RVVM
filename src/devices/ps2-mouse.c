/*
ps2-mouse.c - PS2 Mouse
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
#include "spinlock.h"
#include "utils.h"

#define PS2_CMD_RESET 0xFF
#define PS2_CMD_RESEND 0xFE
#define PS2_CMD_SET_DEFAULTS 0xF6
#define PS2_CMD_DISABLE_DATA_REPORTING 0xF5
#define PS2_CMD_ENABLE_DATA_REPORTING 0xF4
#define PS2_CMD_SET_SAMPLE_RATE 0xF3
#define PS2_CMD_GET_DEV_ID 0xF2
#define PS2_CMD_SET_REMOTE_MODE 0xF0
#define PS2_CMD_SET_WRAP_MODE 0xEE
#define PS2_CMD_RESET_WRAP_MODE 0xEC
#define PS2_CMD_READ_DATA 0xEB
#define PS2_CMD_SET_STREAM_MODE 0xEA
#define PS2_CMD_STATUS_REQ 0xE9
#define PS2_CMD_SET_RESOLUTION 0xE8
#define PS2_CMD_SET_SCALING_2_1 0xE7
#define PS2_CMD_SET_SCALING_1_1 0xE6

#define PS2_RSP_ACK 0xFA
#define PS2_RSP_NAK 0xFE

#define PS2_STATE_CMD             0x0
#define PS2_STATE_SET_SAMPLE_RATE 0x1
#define PS2_STATE_WRAP            0x2
#define PS2_STATE_SET_RESOLUTION  0x3

#define PS2_MODE_STREAM 0x0
#define PS2_MODE_REMOTE 0x1

#define PS2_MOUSE_GENERIC 0x0
#define PS2_MOUSE_WHEEL   0x3

struct hid_mouse {
    chardev_t chardev;
    spinlock_t lock;
    hid_btns_t btns; // Pressed buttons bitmask
    bool res_init;   // Validate hid_mouse_resolution() was called
    // Absolute position
    int32_t x;
    int32_t y;
    // Movement counters - these are actually 9-bit
    int16_t xctr;
    int16_t yctr;
    // Counters' overflow flags
    bool xoverflow;
    bool yoverflow;
    
    int32_t scroll;     // Scroll axis value
    
    uint8_t mode;
    uint8_t state;      // The mouse is a state machine
    uint8_t resolution; // In pow2, e.g. 2 means multiply by 4
    uint8_t rate;       // In samples per second
    uint8_t whl_detect; // Stage of detecting an Intellimouse extension
    bool reporting;     // Data reporting enabled; needed for STATUS command

    ringbuf_t cmdbuf;
};

static void ps2_mouse_defaults(hid_mouse_t* mice)
{
    mice->mode = PS2_MODE_STREAM;
    mice->state = PS2_STATE_CMD;
    mice->reporting = false;
    mice->resolution = 2;
}

static void ps2_mouse_flush(hid_mouse_t* mice)
{
    mice->xctr = 0;
    mice->yctr = 0;
    mice->xoverflow = 0;
    mice->yoverflow = 0;
    mice->scroll = 0;
}

static void ps2_mouse_move_pkt(hid_mouse_t* mice)
{
    int8_t x   = mice->xctr & 0xff;
    bool xsign = mice->xctr < 0;
    int8_t y   = mice->yctr & 0xff;
    bool ysign = mice->yctr < 0;

    ringbuf_put_u8(&mice->cmdbuf, ((mice->btns & HID_BTN_LEFT) ? 1 : 0)
                                | ((mice->btns & HID_BTN_RIGHT) ? 2 : 0)
                                | ((mice->btns & HID_BTN_MIDDLE) ? 4 : 0)
                                | 1 << 3
                                | xsign << 4
                                | ysign << 5
                                | mice->xoverflow << 6
                                | mice->yoverflow << 7);
    ringbuf_put_u8(&mice->cmdbuf, x);
    ringbuf_put_u8(&mice->cmdbuf, y);
    
    if (mice->whl_detect == 3) {
        // Push scroll axis byte
        ringbuf_put_u8(&mice->cmdbuf, mice->scroll);
    }
    
    ps2_mouse_flush(mice);
    chardev_notify(&mice->chardev, CHARDEV_RX);
}

static bool ps2_mouse_cmd(hid_mouse_t* mice, uint8_t cmd)
{
    switch (cmd) {
        case PS2_CMD_RESET:
            ps2_mouse_defaults(mice);
            ringbuf_put_u8(&mice->cmdbuf, PS2_RSP_ACK);
            ringbuf_put_u8(&mice->cmdbuf, 0xAA);
            ringbuf_put_u8(&mice->cmdbuf, 0x00);
            return true;
        case PS2_CMD_RESEND:
            // Unimplemented
            return false;
        case PS2_CMD_SET_DEFAULTS:
            ps2_mouse_defaults(mice);
            ringbuf_put_u8(&mice->cmdbuf, PS2_RSP_ACK);
            return true;
        case PS2_CMD_DISABLE_DATA_REPORTING:
            mice->reporting = false;
            ringbuf_put_u8(&mice->cmdbuf, PS2_RSP_ACK);
            return true;
        case PS2_CMD_ENABLE_DATA_REPORTING:
            mice->reporting = true;
            ringbuf_put_u8(&mice->cmdbuf, PS2_RSP_ACK);
            return true;
        case PS2_CMD_SET_SAMPLE_RATE:
            mice->state = PS2_STATE_SET_SAMPLE_RATE;
            ringbuf_put_u8(&mice->cmdbuf, PS2_RSP_ACK);
            return true;
        case PS2_CMD_GET_DEV_ID:
            ringbuf_put_u8(&mice->cmdbuf, PS2_RSP_ACK);
            if (mice->whl_detect == 3) {
                ringbuf_put_u8(&mice->cmdbuf, PS2_MOUSE_WHEEL);
            } else {
                ringbuf_put_u8(&mice->cmdbuf, PS2_MOUSE_GENERIC);
            }
            return true;
        case PS2_CMD_SET_REMOTE_MODE:
            ps2_mouse_flush(mice);
            mice->mode = PS2_MODE_REMOTE;
            ringbuf_put_u8(&mice->cmdbuf, PS2_RSP_ACK);
            return true;
        case PS2_CMD_SET_WRAP_MODE:
            ps2_mouse_flush(mice);
            mice->state = PS2_STATE_WRAP;
            ringbuf_put_u8(&mice->cmdbuf, PS2_RSP_ACK);
            return true;
        case PS2_CMD_RESET_WRAP_MODE:
            ps2_mouse_flush(mice);
            mice->state = PS2_STATE_CMD;
            ringbuf_put_u8(&mice->cmdbuf, PS2_RSP_ACK);
            return true;
        case PS2_CMD_READ_DATA:
            ringbuf_put_u8(&mice->cmdbuf, PS2_RSP_ACK);
            ps2_mouse_move_pkt(mice);
            return true;
        case PS2_CMD_SET_STREAM_MODE:
            ps2_mouse_flush(mice);
            mice->mode = PS2_MODE_STREAM;
            ringbuf_put_u8(&mice->cmdbuf, PS2_RSP_ACK);
            return true;
        case PS2_CMD_STATUS_REQ:
            ringbuf_put_u8(&mice->cmdbuf, PS2_RSP_ACK);
            ringbuf_put_u8(&mice->cmdbuf, ((mice->btns & HID_BTN_RIGHT) ? 0x1 : 0)
                    | ((mice->btns & HID_BTN_MIDDLE) ? 0x2 : 0)
                    | ((mice->btns & HID_BTN_LEFT) ? 0x4 : 0)
                    | (mice->reporting ? 0x20 : 0)
                    | ((mice->mode == PS2_MODE_REMOTE) ? 0x40 : 0));
            ringbuf_put_u8(&mice->cmdbuf, mice->resolution);
            ringbuf_put_u8(&mice->cmdbuf, mice->rate);
            return true;
        case PS2_CMD_SET_RESOLUTION:
            mice->state = PS2_STATE_SET_RESOLUTION;
            ringbuf_put_u8(&mice->cmdbuf, PS2_RSP_ACK);
            return true;
        case PS2_CMD_SET_SCALING_1_1:
        case PS2_CMD_SET_SCALING_2_1:
            // Ignored, we don't want acceleration of guest cursor
            ringbuf_put_u8(&mice->cmdbuf, PS2_RSP_ACK);
            return true;
        default:
            ringbuf_put_u8(&mice->cmdbuf, PS2_RSP_NAK);
            return true;
    }
}

static size_t ps2_mouse_read(chardev_t* dev, void* buf, size_t size)
{
    hid_mouse_t* mice = dev->data;
    spin_lock(&mice->lock);
    size_t ret = ringbuf_read(&mice->cmdbuf, buf, size);
    spin_unlock(&mice->lock);
    return ret;
}

static size_t ps2_mouse_write(chardev_t* dev, const void* buf, size_t size)
{
    hid_mouse_t* mice = dev->data;
    spin_lock(&mice->lock);
    for (size_t i=0; i<size; ++i) {
        uint8_t val = ((const uint8_t*)buf)[i];

        switch (mice->state) {
            case PS2_STATE_CMD:
                ps2_mouse_cmd(mice, val);
                break;
            case PS2_STATE_SET_SAMPLE_RATE:
                mice->rate = val;
                // Magical sequence for detecting Intellimouse extension
                // See https://wiki.osdev.org/PS/2_Mouse
                if (mice->whl_detect == 0 && mice->rate == 200) {
                    mice->whl_detect = 1;
                } else if (mice->whl_detect == 1 && mice->rate == 100) {
                    mice->whl_detect = 2;
                } else if (mice->whl_detect == 2 && mice->rate == 80) {
                    mice->whl_detect = 3;
                } else if (mice->whl_detect < 3) {
                    mice->whl_detect = 0;
                }
                mice->state = PS2_STATE_CMD;
                ringbuf_put_u8(&mice->cmdbuf, PS2_RSP_ACK);
                break;
            case PS2_STATE_WRAP:
                if (val != PS2_CMD_RESET_WRAP_MODE && val != PS2_CMD_RESET) {
                    ringbuf_put_u8(&mice->cmdbuf, val);
                }
                break;
            case PS2_STATE_SET_RESOLUTION:
                mice->resolution = val;
                mice->state = PS2_STATE_CMD;
                ringbuf_put_u8(&mice->cmdbuf, PS2_RSP_ACK);
                break;
        }
    }
    spin_unlock(&mice->lock);
    chardev_notify(&mice->chardev, CHARDEV_RX);
    return size;
}

static void ps2_mouse_remove(chardev_t* dev)
{
    hid_mouse_t* mice = dev->data;
    ringbuf_destroy(&mice->cmdbuf);
    free(mice);
}

PUBLIC hid_mouse_t* hid_mouse_init_auto_ps2(rvvm_machine_t* machine)
{
    plic_ctx_t* plic = rvvm_get_plic(machine);
    rvvm_addr_t addr = rvvm_mmio_zone_auto(machine, 0x20000000, ALTPS2_MMIO_SIZE);
    hid_mouse_t* mice = safe_calloc(sizeof(hid_mouse_t), 1);

    mice->chardev.read = ps2_mouse_read;
    mice->chardev.write = ps2_mouse_write;
    mice->chardev.remove = ps2_mouse_remove;
    mice->chardev.data = mice;
    
    ps2_mouse_defaults(mice);
    
    ringbuf_create(&mice->cmdbuf, 1024);
    ringbuf_put_u8(&mice->cmdbuf, 0xAA);
    ringbuf_put_u8(&mice->cmdbuf, 0x00);
    
    altps2_init(machine, addr, plic, plic_alloc_irq(plic), &mice->chardev);
    return mice;
}

PUBLIC void hid_mouse_press_ps2(hid_mouse_t* mouse, hid_btns_t btns)
{
    if (mouse == NULL) return;
    spin_lock(&mouse->lock);
    bool pressed = mouse->btns != (mouse->btns | btns);
    mouse->btns |= btns;
    if (pressed && mouse->mode == PS2_MODE_STREAM && mouse->reporting) {
        ps2_mouse_move_pkt(mouse);
    }
    spin_unlock(&mouse->lock);
}

PUBLIC void hid_mouse_release_ps2(hid_mouse_t* mouse, hid_btns_t btns)
{
    if (mouse == NULL) return;
    spin_lock(&mouse->lock);
    bool released = mouse->btns != (mouse->btns & ~btns);
    mouse->btns &= ~btns;
    if (released && mouse->mode == PS2_MODE_STREAM && mouse->reporting) {
        ps2_mouse_move_pkt(mouse);
    }
    spin_unlock(&mouse->lock);
}

PUBLIC void hid_mouse_scroll_ps2(hid_mouse_t* mouse, int32_t offset)
{
    if (mouse == NULL) return;
    spin_lock(&mouse->lock);
    mouse->scroll += offset;
    if (mouse->mode == PS2_MODE_STREAM && mouse->reporting) {
        ps2_mouse_move_pkt(mouse);
    }
    spin_unlock(&mouse->lock);
}

static void ps2_mouse_move(hid_mouse_t* mouse, int32_t x, int32_t y)
{
    int shift = 3 - mouse->resolution;
    int32_t newx, newy;
    mouse->x += x;
    mouse->y += y;
    if (shift >= 0) {
        newx = mouse->xctr + (x >> shift);
        newy = mouse->yctr - (y >> shift);
    } else {
        newx = mouse->xctr + (x << -shift);
        newy = mouse->yctr - (y << -shift);
    }
    if (newx > 255 || newx < -512) {
        mouse->xoverflow = true;
        newx = (int8_t)newx;
    }
    if (newy > 255 || newy < -512) {
        mouse->yoverflow = true;
        newy = (int8_t)newy;
    }

    mouse->xctr = newx;
    mouse->yctr = newy;
    if (mouse->mode == PS2_MODE_STREAM && mouse->reporting) {
        ps2_mouse_move_pkt(mouse);
    }
}

PUBLIC void hid_mouse_resolution_ps2(hid_mouse_t* mouse, uint32_t x, uint32_t y)
{
    if (mouse == NULL) return;
    spin_lock(&mouse->lock);
    mouse->res_init = x != 0 && y != 0;
    spin_unlock(&mouse->lock);
}

PUBLIC void hid_mouse_move_ps2(hid_mouse_t* mouse, int32_t x, int32_t y)
{
    if (mouse == NULL) return;
    spin_lock(&mouse->lock);
    ps2_mouse_move(mouse, x, y);
    spin_unlock(&mouse->lock);
}

PUBLIC void hid_mouse_place_ps2(hid_mouse_t* mouse, int32_t x, int32_t y)
{
    if (mouse == NULL) return;
    spin_lock(&mouse->lock);
    if (!mouse->res_init) rvvm_warn("hid_mouse_resolution() was not called!");
    ps2_mouse_move(mouse, x - mouse->x, y - mouse->y);
    spin_unlock(&mouse->lock);
}
