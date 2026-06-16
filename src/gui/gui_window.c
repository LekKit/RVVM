/*
gui_window.c - GUI Window
Copyright (C) 2021  LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include "gui_window.h"
#include "utils.h"

PUSH_OPTIMIZATION_SIZE

#if defined(USE_GUI)

/*
 * GUI backend
 */

static void gui_backend_free(gui_window_t* win)
{
    if (win) {
        if (win->bknd_cb) {
            if (win->bknd_cb->free) {
                win->bknd_cb->free(win);
            }
        }
        win->bknd_data = NULL;
    }
}

static bool gui_backend_init(gui_window_t* win)
{
    const char* gui = rvvm_getarg("gui");
#if defined(USE_WIN32_GUI)
    if ((!gui || rvvm_strcmp(gui, "win32")) && win32_window_init(win)) {
        return true;
    }
    gui_backend_free(win);
#endif
#if defined(USE_HAIKU_GUI)
    if ((!gui || rvvm_strcmp(gui, "haiku")) && haiku_window_init(win)) {
        return true;
    }
    gui_backend_free(win);
#endif
#if defined(USE_COCOA_GUI)
    if ((!gui || rvvm_strcmp(gui, "cocoa")) && cocoa_window_init(win)) {
        return true;
    }
    gui_backend_free(win);
#endif
#if defined(USE_WAYLAND)
    if ((!gui || rvvm_strcmp(gui, "wayland")) && wayland_window_init(win)) {
        return true;
    }
    gui_backend_free(win);
#endif
#if defined(USE_X11)
    if ((!gui || rvvm_strcmp(gui, "x11")) && x11_window_init(win)) {
        return true;
    }
    gui_backend_free(win);
#endif
#if defined(USE_SDL)
    if ((!gui || rvvm_strcmp(gui, "sdl")) && sdl_window_init(win)) {
        return true;
    }
    gui_backend_free(win);
#endif
    rvvm_error("No suitable windowing backends found!");
    UNUSED(win);
    UNUSED(gui);
    return false;
}

/*
 * GUI Window
 */

static void gui_display_draw(rvvm_fbdev_t* fbdev)
{
    gui_window_t* win = rvvm_fbdev_get_display_data(fbdev);
    gui_window_draw(win);
}

static void gui_display_poll(rvvm_fbdev_t* fbdev)
{
    gui_window_t* win = rvvm_fbdev_get_display_data(fbdev);
    gui_window_poll(win);
}

static void gui_display_free(rvvm_fbdev_t* fbdev)
{
    gui_window_t* win = rvvm_fbdev_get_display_data(fbdev);
    gui_window_free(win);
}

static const rvvm_display_cb_t gui_display_cb = {
    .draw = gui_display_draw,
    .poll = gui_display_poll,
    .free = gui_display_free,
};

static const gui_event_cb_t gui_dummy_event_cb = ZERO_INIT;

static const gui_backend_cb_t gui_dummy_bknd_cb = ZERO_INIT;

gui_window_t* gui_window_init_bare(size_t vram_size, const rvvm_fb_t* fb)
{
    rvvm_fb_t tmp = {
        .width  = 640,
        .height = 480,
        .format = RVVM_RGB_XRGB8888,
    };
    gui_window_t* win = safe_new_obj(gui_window_t);

    // Normalize inputs
    if (rvvm_fb_size(fb)) {
        tmp = *fb;
    }
    if (vram_size < rvvm_fb_size(&tmp)) {
        vram_size = rvvm_fb_size(&tmp);
    }

    // Create fbdev
    win->fbdev = rvvm_fbdev_init();
    rvvm_fbdev_register_display(win->fbdev, &gui_display_cb);
    rvvm_fbdev_set_display_data(win->fbdev, win);
    rvvm_fbdev_set_vram(win->fbdev, NULL, vram_size);
    rvvm_fbdev_set_scanout(win->fbdev, &tmp);

    // Register dummy callbacks
    gui_window_register(win, &gui_dummy_event_cb);
    gui_backend_register(win, &gui_dummy_bknd_cb);

    // Initial window size
    gui_backend_on_resize(win, rvvm_fb_width(&tmp), rvvm_fb_height(&tmp));
    return win;
}

gui_window_t* gui_window_init(size_t vram_size, const rvvm_fb_t* fb)
{
    gui_window_t* win = gui_window_init_bare(vram_size, fb);
    if (gui_backend_init(win)) {
        // Initialize scanout in VRAM
        rvvm_fb_t tmp = ZERO_INIT;
        rvvm_fbdev_get_scanout(win->fbdev, &tmp);
        tmp.buffer = rvvm_fbdev_get_vram(win->fbdev, NULL);
        rvvm_fbdev_set_scanout(win->fbdev, &tmp);
        return win;
    }
    gui_window_free(win);
    return NULL;
}

void gui_window_free(gui_window_t* win)
{
    if (win) {
        if (!rvvm_fbdev_dec_ref(win->fbdev)) {
            gui_backend_free(win);
            if (win->ev_cb->free) {
                win->ev_cb->free(win);
            }
            safe_free(win);
        }
    }
}

void gui_window_poll(gui_window_t* win)
{
    if (win) {
        if (win->ev_cb->poll) {
            win->ev_cb->poll(win);
        }
        if (win->bknd_cb->poll) {
            win->bknd_cb->poll(win);
        }
    }
}

void gui_window_draw(gui_window_t* win)
{
    if (win) {
        rvvm_fb_t fb = ZERO_INIT;
        rvvm_fbdev_get_scanout(win->fbdev, &fb);
        if (!rvvm_fb_same_res(&win->fb, &fb)) {
            uint32_t width = rvvm_fb_width(&fb), height = rvvm_fb_height(&fb);
            /*
             * Change window size if:
             * - It currently matches the previous video mode
             * - It is currently smaller than the new mode, and shrinking is not allowed
             */
            bool match = gui_window_width(win) == rvvm_fb_width(&win->fb) //
                      && gui_window_height(win) == rvvm_fb_height(&win->fb);
            bool small = gui_window_width(win) < width || gui_window_height(win) < height;
            bool resiz = match || small;
            if (resiz) {
                uint32_t w = 1920, h = 1080;
                int32_t  x = 0, y = 0;
                gui_window_get_scr_size(win, &w, &h);
                // Clamp the new window size to fit onto host workspace
                width  = EVAL_MIN(width, w);
                height = EVAL_MIN(height, h);
                // Reposition window to match previous center
                if (gui_window_get_position(win, &x, &y)) {
                    bool neg_x = x < 0;
                    bool neg_y = y < 0;
                    // Prevent window header from falling below bottom of the host workspace
                    x = EVAL_MIN(x - ((int32_t)(width - gui_window_width(win)) / 2), (int32_t)w - 128);
                    y = EVAL_MIN(y - ((int32_t)(height - gui_window_height(win)) / 2), (int32_t)h - 128);
                    // Prevent window header from going above the top of the host workspace
                    if (!neg_x) {
                        x = EVAL_MAX(x, 0);
                    }
                    if (!neg_y) {
                        y = EVAL_MAX(y, 0);
                    }
                    gui_window_set_position(win, x, y);
                }
            }
            // Update minimum window size
            gui_window_set_min_size(win, width, height);
            if (resiz) {
                gui_window_set_size(win, width, height);
            }
        }
        win->fb = fb;
        if (win->ev_cb->draw) {
            win->ev_cb->draw(win);
        }
        if (win->bknd_cb->draw) {
            uint32_t x = EVAL_MAX((int)gui_window_width(win) - (int)rvvm_fb_width(&fb), 0) >> 1;
            uint32_t y = EVAL_MAX((int)gui_window_height(win) - (int)rvvm_fb_height(&fb), 0) >> 1;
            win->bknd_cb->draw(win, &fb, x, y);
        }
    }
}

#else

gui_window_t* gui_window_init_bare(size_t vram_size, const rvvm_fb_t* fb)
{
    UNUSED(vram_size && fb);
    return NULL;
}

gui_window_t* gui_window_init(size_t vram_size, const rvvm_fb_t* fb)
{
    UNUSED(vram_size && fb);
    return NULL;
}

void gui_window_free(gui_window_t* win)
{
    UNUSED(win);
}

void gui_window_poll(gui_window_t* win)
{
    UNUSED(win);
}

void gui_window_draw(gui_window_t* win)
{
    UNUSED(win);
}

#endif

POP_OPTIMIZATION_SIZE
