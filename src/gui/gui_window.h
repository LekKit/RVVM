/*
gui_window.h - GUI Window
Copyright (C) 2021  LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#ifndef _GUI_WINDOW_INTERFACES_H
#define _GUI_WINDOW_INTERFACES_H

#include <rvvm/rvvm_fb.h>

// TODO: Rework HID API
#include "devices/hid_api.h"

#include "utils.h"

typedef struct gui_window_t gui_window_t;

// GUI Window callbacks
typedef struct {
    void (*free)(gui_window_t* win);
    void (*poll)(gui_window_t* win);
    void (*draw)(gui_window_t* win);
    void (*on_close)(gui_window_t* win);
    void (*on_focus_lost)(gui_window_t* win);
    bool (*on_resize)(gui_window_t* win, uint32_t w, uint32_t h);
    void (*on_input)(gui_window_t* win, const char* text);
    void (*on_key_press)(gui_window_t* win, hid_key_t key);
    void (*on_key_release)(gui_window_t* win, hid_key_t key);
    void (*on_mouse_press)(gui_window_t* win, hid_btns_t btns);
    void (*on_mouse_release)(gui_window_t* win, hid_btns_t btns);
    void (*on_mouse_place)(gui_window_t* win, int32_t x, int32_t y);
    void (*on_mouse_move)(gui_window_t* win, int32_t x, int32_t y);
    void (*on_mouse_scroll)(gui_window_t* win, int32_t offset);
} gui_event_cb_t;

// GUI Backend callbacks
typedef struct {
    // Base window features
    void (*free)(gui_window_t* win);
    void (*poll)(gui_window_t* win);
    void (*draw)(gui_window_t* win, const rvvm_fb_t* fb, uint32_t x, uint32_t y);
    void (*set_title)(gui_window_t* win, const char* title);

    // Usability features
    void (*grab_input)(gui_window_t* win, bool grab);
    void (*hide_cursor)(gui_window_t* win, bool hide);
    void (*hide_window)(gui_window_t* win, bool hide);
    void (*get_clipboard)(gui_window_t* win, char* buf, size_t size);

    // Positioning
    void (*set_win_size)(gui_window_t* win, uint32_t w, uint32_t h);
    void (*set_min_size)(gui_window_t* win, uint32_t w, uint32_t h);
    void (*get_position)(gui_window_t* win, int32_t* x, int32_t* y);
    void (*set_position)(gui_window_t* win, int32_t x, int32_t y);
    void (*get_scr_size)(gui_window_t* win, uint32_t* w, uint32_t* h);
    void (*set_fullscreen)(gui_window_t* win, bool fullscreen);
} gui_backend_cb_t;

// GUI Window handle
struct gui_window_t {
    // Framebuffer device handle
    rvvm_fbdev_t* fbdev;

    // Window private data
    void* data;

    // Window event callbacks
    const gui_event_cb_t* ev_cb;

    // Window backend private data
    void* bknd_data;

    // Window backend callbacks
    const gui_backend_cb_t* bknd_cb;

    // Last scanout context
    rvvm_fb_t fb;

    // Window dimensions
    uint32_t width;
    uint32_t height;
};

/*
 * GUI Window creation
 */

// Create a bare GUI window without a backend, fb specifies expected dimensions / format
gui_window_t* gui_window_init_bare(size_t vram_size, const rvvm_fb_t* fb);

// Create a GUI display window with a backend, fb specifies expected dimensions / format
// Passing non-NULL string into gui allows to pick a backend
gui_window_t* gui_window_init(size_t vram_size, const rvvm_fb_t* fb);

/*
 * GUI Window handling for non-attached fbdev
 */

// Redraw GUI window
void gui_window_draw(gui_window_t* win);

// Poll GUI window input
void gui_window_poll(gui_window_t* win);

// Free GUI window
void gui_window_free(gui_window_t* win);

/*
 * RVVM Display
 */

// Register RVVM display on a GUI window
void gui_rvvm_register(gui_window_t* win, rvvm_machine_t* machine);

// Create RVVM display GUI
RVVM_PUBLIC gui_window_t* gui_rvvm_init(size_t vram_size, const rvvm_fb_t* fb, rvvm_machine_t* machine);

/*
 * GUI User handling
 */

// Register GUI event callbacks
static inline void gui_window_register(gui_window_t* win, const gui_event_cb_t* cb)
{
    if (win && cb) {
        win->ev_cb = cb;
    }
}

// Set GUI window private data
static inline void gui_window_set_data(gui_window_t* win, void* data)
{
    if (win) {
        win->data = data;
    }
}

// Get GUI window private data
static inline void* gui_window_get_data(gui_window_t* win)
{
    return win ? win->data : NULL;
}

// Get GUI window framebuffer device
static inline rvvm_fbdev_t* gui_window_get_fbdev(gui_window_t* win)
{
    return win ? win->fbdev : NULL;
}

/*
 * GUI Window handling
 */

static inline uint32_t gui_window_width(const gui_window_t* win)
{
    return win ? win->width : 0;
}

static inline uint32_t gui_window_height(const gui_window_t* win)
{
    return win ? win->height : 0;
}

static inline bool gui_window_set_title(gui_window_t* win, const char* title)
{
    if (win && title && win->bknd_cb->set_title) {
        win->bknd_cb->set_title(win, title);
        return true;
    }
    return false;
}

static inline bool gui_window_grab_input(gui_window_t* win, bool grab)
{
    if (win && win->bknd_cb->grab_input) {
        win->bknd_cb->grab_input(win, grab);
        return true;
    }
    return false;
}

static inline bool gui_window_hide_cursor(gui_window_t* win, bool hide)
{
    if (win && win->bknd_cb->hide_cursor) {
        win->bknd_cb->hide_cursor(win, hide);
        return true;
    }
    return false;
}

static inline bool gui_window_hide_window(gui_window_t* win, bool hide)
{
    if (win && win->bknd_cb->hide_window) {
        win->bknd_cb->hide_window(win, hide);
        return true;
    }
    return false;
}

static inline size_t gui_window_get_clipboard(gui_window_t* win, char* buf, size_t size)
{
    if (win && buf && size && win->bknd_cb->get_clipboard) {
        buf[0] = 0;
        win->bknd_cb->get_clipboard(win, buf, size);
        return rvvm_strlen(buf);
    }
    return 0;
}

static inline bool gui_window_set_size(gui_window_t* win, uint32_t w, uint32_t h)
{
    if (win && win->bknd_cb->set_win_size) {
        win->bknd_cb->set_win_size(win, w, h);
        win->width  = w;
        win->height = h;
        return true;
    }
    return false;
}

static inline bool gui_window_set_min_size(gui_window_t* win, uint32_t w, uint32_t h)
{
    if (win && win->bknd_cb->set_min_size) {
        win->bknd_cb->set_min_size(win, w, h);
        return true;
    }
    return false;
}

static inline bool gui_window_get_position(gui_window_t* win, int32_t* x, int32_t* y)
{
    if (win && win->bknd_cb->get_position && x && y) {
        win->bknd_cb->get_position(win, x, y);
        return true;
    }
    return false;
}

static inline bool gui_window_set_position(gui_window_t* win, uint32_t x, uint32_t y)
{
    if (win && win->bknd_cb->set_position) {
        win->bknd_cb->set_position(win, x, y);
        return true;
    }
    return false;
}

static inline bool gui_window_get_scr_size(gui_window_t* win, uint32_t* w, uint32_t* h)
{
    if (win && win->bknd_cb->get_scr_size && w && h) {
        win->bknd_cb->get_scr_size(win, w, h);
        return true;
    }
    return false;
}

static inline bool gui_window_set_fullscreen(gui_window_t* win, bool fullscreen)
{
    if (win && win->bknd_cb->set_fullscreen) {
        win->bknd_cb->set_fullscreen(win, fullscreen);
        return true;
    }
    return false;
}

/*
 * GUI Backend interfaces
 */

// Register GUI backend callbacks
static inline void gui_backend_register(gui_window_t* win, const gui_backend_cb_t* cb)
{
    if (win && cb) {
        win->bknd_cb = cb;
    }
}

// Set GUI backend private data
static inline void gui_backend_set_data(gui_window_t* win, void* data)
{
    if (win) {
        win->bknd_data = data;
    }
}

// Get GUI backend private data
static inline void* gui_backend_get_data(gui_window_t* win)
{
    return win ? win->bknd_data : NULL;
}

// Set VRAM buffer (XShm / wl_shm), GUI API user must place scanout inside VRAM
static inline void gui_backend_set_vram(gui_window_t* win, void* vram, size_t vram_size)
{
    if (win) {
        rvvm_fbdev_set_vram(win->fbdev, vram, vram_size);
    }
}

// Get VRAM buffer
static inline void* gui_backend_get_vram(gui_window_t* win)
{
    return rvvm_fbdev_get_vram(win->fbdev, NULL);
}

// Get requested VRAM size
static inline size_t gui_backend_get_vram_size(gui_window_t* win)
{
    size_t vram_size = 0;
    rvvm_fbdev_get_vram(win->fbdev, &vram_size);
    return vram_size;
}

static inline void gui_backend_on_close(gui_window_t* win)
{
    if (win && win->ev_cb->on_close) {
        win->ev_cb->on_close(win);
    }
}

static inline void gui_backend_on_focus_lost(gui_window_t* win)
{
    if (win && win->ev_cb->on_focus_lost) {
        win->ev_cb->on_focus_lost(win);
    }
}

static inline void gui_backend_on_resize(gui_window_t* win, uint32_t w, uint32_t h)
{
    if (win) {
        if (w != win->width || h != win->height) {
            rvvm_fb_t fb = ZERO_INIT;
            // Soft dirty the framebuffer
            rvvm_fbdev_get_scanout(win->fbdev, &fb);
            rvvm_fbdev_set_scanout(win->fbdev, &fb);
            if (win->ev_cb->on_resize) {
                win->ev_cb->on_resize(win, w, h);
            }
            win->width  = w;
            win->height = h;
        }
    }
}

static inline void gui_backend_on_input(gui_window_t* win, const char* text)
{
    if (win && text && win->ev_cb->on_input) {
        win->ev_cb->on_input(win, text);
    }
}

static inline void gui_backend_on_key_press(gui_window_t* win, hid_key_t key)
{
    if (win && win->ev_cb->on_key_press && key) {
        win->ev_cb->on_key_press(win, key);
    }
}

static inline void gui_backend_on_key_release(gui_window_t* win, hid_key_t key)
{
    if (win && win->ev_cb->on_key_release && key) {
        win->ev_cb->on_key_release(win, key);
    }
}

static inline void gui_backend_on_mouse_press(gui_window_t* win, hid_btns_t btns)
{
    if (win && win->ev_cb->on_mouse_press && btns) {
        win->ev_cb->on_mouse_press(win, btns);
    }
}

static inline void gui_backend_on_mouse_release(gui_window_t* win, hid_btns_t btns)
{
    if (win && win->ev_cb->on_mouse_release && btns) {
        win->ev_cb->on_mouse_release(win, btns);
    }
}

static inline void gui_backend_on_mouse_place(gui_window_t* win, int32_t x, int32_t y)
{
    if (win && win->ev_cb->on_mouse_place) {
        win->ev_cb->on_mouse_place(win, x, y);
    }
}

static inline void gui_backend_on_mouse_move(gui_window_t* win, int32_t x, int32_t y)
{
    if (win && win->ev_cb->on_mouse_move && (x || y)) {
        win->ev_cb->on_mouse_move(win, x, y);
    }
}

static inline void gui_backend_on_mouse_scroll(gui_window_t* win, int32_t offset)
{
    if (win && win->ev_cb->on_mouse_scroll && offset) {
        win->ev_cb->on_mouse_scroll(win, offset);
    }
}

/*
 * GUI backends entry points; Internal use only!
 */

bool win32_window_init(gui_window_t* win);
bool haiku_window_init(gui_window_t* win);
bool cocoa_window_init(gui_window_t* win);
bool wayland_window_init(gui_window_t* win);
bool x11_window_init(gui_window_t* win);
bool sdl_window_init(gui_window_t* win);

#endif
