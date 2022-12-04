/*
sdl_window.c - SDL RVVM Window
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

#include "fb_window.h"
#include "utils.h"

#include <SDL/SDL.h>

static const hid_key_t sdl_key_to_hid_byte_map[] = {
    [SDLK_a] = HID_KEY_A,
    [SDLK_b] = HID_KEY_B,
    [SDLK_c] = HID_KEY_C,
    [SDLK_d] = HID_KEY_D,
    [SDLK_e] = HID_KEY_E,
    [SDLK_f] = HID_KEY_F,
    [SDLK_g] = HID_KEY_G,
    [SDLK_h] = HID_KEY_H,
    [SDLK_i] = HID_KEY_I,
    [SDLK_j] = HID_KEY_J,
    [SDLK_k] = HID_KEY_K,
    [SDLK_l] = HID_KEY_L,
    [SDLK_m] = HID_KEY_M,
    [SDLK_n] = HID_KEY_N,
    [SDLK_o] = HID_KEY_O,
    [SDLK_p] = HID_KEY_P,
    [SDLK_q] = HID_KEY_Q,
    [SDLK_r] = HID_KEY_R,
    [SDLK_s] = HID_KEY_S,
    [SDLK_t] = HID_KEY_T,
    [SDLK_u] = HID_KEY_U,
    [SDLK_v] = HID_KEY_V,
    [SDLK_w] = HID_KEY_W,
    [SDLK_x] = HID_KEY_X,
    [SDLK_y] = HID_KEY_Y,
    [SDLK_z] = HID_KEY_Z,
    [SDLK_0] = HID_KEY_0,
    [SDLK_1] = HID_KEY_1,
    [SDLK_2] = HID_KEY_2,
    [SDLK_3] = HID_KEY_3,
    [SDLK_4] = HID_KEY_4,
    [SDLK_5] = HID_KEY_5,
    [SDLK_6] = HID_KEY_6,
    [SDLK_7] = HID_KEY_7,
    [SDLK_8] = HID_KEY_8,
    [SDLK_9] = HID_KEY_9,
    [SDLK_RETURN] = HID_KEY_ENTER,
    [SDLK_ESCAPE] = HID_KEY_ESC,
    [SDLK_BACKSPACE] = HID_KEY_BACKSPACE,
    [SDLK_TAB] = HID_KEY_TAB,
    [SDLK_SPACE] = HID_KEY_SPACE,
    [SDLK_MINUS] = HID_KEY_MINUS,
    [SDLK_EQUALS] = HID_KEY_EQUAL,
    [SDLK_LEFTBRACKET] = HID_KEY_LEFTBRACE,
    [SDLK_RIGHTBRACKET] = HID_KEY_RIGHTBRACE,
    [SDLK_BACKSLASH] = HID_KEY_BACKSLASH,
    [SDLK_SEMICOLON] = HID_KEY_SEMICOLON,
    [SDLK_QUOTE] = HID_KEY_APOSTROPHE,
    [SDLK_BACKQUOTE] = HID_KEY_GRAVE,
    [SDLK_COMMA] = HID_KEY_COMMA,
    [SDLK_PERIOD] = HID_KEY_DOT,
    [SDLK_SLASH] = HID_KEY_SLASH,
    [SDLK_CAPSLOCK] = HID_KEY_CAPSLOCK,
    [SDLK_LCTRL] = HID_KEY_LEFTCTRL,
    [SDLK_LSHIFT] = HID_KEY_LEFTSHIFT,
    [SDLK_LALT] = HID_KEY_LEFTALT,
    [SDLK_LMETA] = HID_KEY_LEFTMETA,
    [SDLK_RCTRL] = HID_KEY_RIGHTCTRL,
    [SDLK_RSHIFT] = HID_KEY_RIGHTSHIFT,
    [SDLK_RALT] = HID_KEY_RIGHTALT,
    [SDLK_RMETA] = HID_KEY_RIGHTMETA,
    [SDLK_F1] = HID_KEY_F1,
    [SDLK_F2] = HID_KEY_F2,
    [SDLK_F3] = HID_KEY_F3,
    [SDLK_F4] = HID_KEY_F4,
    [SDLK_F5] = HID_KEY_F5,
    [SDLK_F6] = HID_KEY_F6,
    [SDLK_F7] = HID_KEY_F7,
    [SDLK_F8] = HID_KEY_F8,
    [SDLK_F9] = HID_KEY_F9,
    [SDLK_F10] = HID_KEY_F10,
    [SDLK_F11] = HID_KEY_F11,
    [SDLK_F12] = HID_KEY_F12,
    [SDLK_SYSREQ] = HID_KEY_SYSRQ,
    [SDLK_SCROLLOCK] = HID_KEY_SCROLLLOCK,
    [SDLK_PAUSE] = HID_KEY_PAUSE,
    [SDLK_INSERT] = HID_KEY_INSERT,
    [SDLK_HOME] = HID_KEY_HOME,
    [SDLK_PAGEUP] = HID_KEY_PAGEUP,
    [SDLK_DELETE] = HID_KEY_DELETE,
    [SDLK_END] = HID_KEY_END,
    [SDLK_PAGEDOWN] = HID_KEY_PAGEDOWN,
    [SDLK_RIGHT] = HID_KEY_RIGHT,
    [SDLK_LEFT] = HID_KEY_LEFT,
    [SDLK_DOWN] = HID_KEY_DOWN,
    [SDLK_UP] = HID_KEY_UP,
    [SDLK_NUMLOCK] = HID_KEY_NUMLOCK,
    [SDLK_KP_DIVIDE] = HID_KEY_KPSLASH,
    [SDLK_KP_MULTIPLY] = HID_KEY_KPASTERISK,
    [SDLK_KP_MINUS] = HID_KEY_KPMINUS,
    [SDLK_KP_PLUS] = HID_KEY_KPPLUS,
    [SDLK_KP_ENTER] = HID_KEY_KPENTER,
    [SDLK_KP1] = HID_KEY_KP1,
    [SDLK_KP2] = HID_KEY_KP2,
    [SDLK_KP3] = HID_KEY_KP3,
    [SDLK_KP4] = HID_KEY_KP4,
    [SDLK_KP5] = HID_KEY_KP5,
    [SDLK_KP6] = HID_KEY_KP6,
    [SDLK_KP7] = HID_KEY_KP7,
    [SDLK_KP8] = HID_KEY_KP8,
    [SDLK_KP9] = HID_KEY_KP9,
    [SDLK_KP0] = HID_KEY_KP0,
    [SDLK_KP_PERIOD] = HID_KEY_KPDOT,
    [SDLK_MENU] = HID_KEY_MENU,
};

static hid_key_t sdl_key_to_hid(uint32_t sdl_key)
{
    if (sdl_key < sizeof(sdl_key_to_hid_byte_map)) {
        return sdl_key_to_hid_byte_map[sdl_key];
    }
    return HID_KEY_NONE;
}

static rgb_fmt_t sdl_get_rgb_format(const SDL_PixelFormat* format)
{
    switch (format->BitsPerPixel) {
        case 16: return RGB_FMT_R5G6B5;
        case 24: return RGB_FMT_R8G8B8;
        case 32:
            if (format->Rmask & 0xFF) {
                return RGB_FMT_A8B8G8R8;
            } else {
                return RGB_FMT_A8R8G8B8;
            }
    }
    return RGB_FMT_INVALID;
}

static SDL_Surface* sdl_surface = NULL;

bool fb_window_create(fb_window_t* win)
{
    // Only one SDL window may exist at the same time
    if (sdl_surface == NULL) {
        if (SDL_Init(SDL_INIT_VIDEO) < 0) {
            rvvm_error("Failed to initialize SDL");
            return false;
        }
        sdl_surface = SDL_SetVideoMode(win->fb.width, win->fb.height,
                        rgb_format_bpp(win->fb.format), SDL_ANYFORMAT);
        if (sdl_surface == NULL) return false;
        SDL_WM_SetCaption("RVVM", NULL);
        SDL_ShowCursor(SDL_DISABLE);
        win->fb.format = sdl_get_rgb_format(sdl_surface->format);
        if (SDL_MUSTLOCK(sdl_surface)) {
            win->fb.buffer = safe_calloc(framebuffer_size(&win->fb), 1);
        } else {
            win->fb.buffer = sdl_surface->pixels;
        }
        return true;
    }
    return false;
}

void fb_window_close(fb_window_t* win)
{
    if (win->fb.buffer != sdl_surface->pixels) free(win->fb.buffer);
    SDL_FreeSurface(sdl_surface);
    SDL_Quit();
    sdl_surface = NULL;
}

void fb_window_update(fb_window_t* win)
{
    SDL_Event event;
    if (win->fb.buffer != sdl_surface->pixels) {
        SDL_LockSurface(sdl_surface);
        memcpy(sdl_surface->pixels, win->fb.buffer, framebuffer_size(&win->fb));
        SDL_UnlockSurface(sdl_surface);
    }
    SDL_Flip(sdl_surface);
    while(SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_KEYDOWN:
                hid_keyboard_press(win->keyboard, sdl_key_to_hid(event.key.keysym.sym));
                break;
            case SDL_KEYUP:
                hid_keyboard_release(win->keyboard, sdl_key_to_hid(event.key.keysym.sym));
                break;
            case SDL_MOUSEMOTION:
                hid_mouse_place(win->mouse, event.motion.x, event.motion.y);
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    hid_mouse_press(win->mouse, HID_BTN_LEFT);
                } else if (event.button.button == SDL_BUTTON_MIDDLE) {
                    hid_mouse_press(win->mouse, HID_BTN_MIDDLE);
                } else if (event.button.button == SDL_BUTTON_RIGHT) {
                    hid_mouse_press(win->mouse, HID_BTN_RIGHT);
                } else if (event.button.button == SDL_BUTTON_WHEELUP) {
                    hid_mouse_scroll(win->mouse, HID_SCROLL_UP);
                } else if (event.button.button == SDL_BUTTON_WHEELDOWN) {
                    hid_mouse_scroll(win->mouse, HID_SCROLL_DOWN);
                }
                break;
            case SDL_MOUSEBUTTONUP:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    hid_mouse_release(win->mouse, HID_BTN_LEFT);
                } else if (event.button.button == SDL_BUTTON_MIDDLE) {
                    hid_mouse_release(win->mouse, HID_BTN_MIDDLE);
                } else if (event.button.button == SDL_BUTTON_RIGHT) {
                    hid_mouse_release(win->mouse, HID_BTN_RIGHT);
                }
                break;
            case SDL_QUIT:
                // Power down the machine that owns this window
                rvvm_reset_machine(win->machine, false);
                break;
        }
    }
}
