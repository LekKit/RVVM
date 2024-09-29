/*
sdl_window.c - SDL RVVM Window
Copyright (C) 2022  LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.

Alternatively, the contents of this file may be used under the terms
of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or any later version.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "gui_window.h"
#include "dlib.h"
#include "vma_ops.h"
#include "utils.h"
#include "compiler.h"

#if !defined(__EMSCRIPTEN__) && !defined(_MSC_VER)
#define SDL_DYNAMIC_LOADING
#endif

// Resolve symbols at runtime
#define SDL_DLIB_SYM(sym) static typeof(sym)* sym##_dlib = NULL;

// Check for SDL1 header presence
#if USE_SDL == 1 && !CHECK_INCLUDE(SDL/SDL.h)
#undef USE_SDL
#warning Disabling USE_SDL as <SDL/SDL.h> is unavailable
#endif

// Check for SDL2 header presence
#if USE_SDL == 2 && !CHECK_INCLUDE(SDL2/SDL.h)
#undef USE_SDL
#warning Disabling USE_SDL as <SDL2/SDL.h> is unavailable
#endif

#ifdef USE_SDL


#if USE_SDL == 2
#include <SDL2/SDL.h>

#define SDL_LIB_NAME "SDL2"

#ifdef SDL_DYNAMIC_LOADING
SDL_DLIB_SYM(SDL_UpdateWindowSurface)
SDL_DLIB_SYM(SDL_UpdateTexture)
SDL_DLIB_SYM(SDL_RenderCopy)
SDL_DLIB_SYM(SDL_RenderPresent)
SDL_DLIB_SYM(SDL_GetRelativeMouseState)
SDL_DLIB_SYM(SDL_SetWindowGrab)
SDL_DLIB_SYM(SDL_SetWindowKeyboardGrab)
SDL_DLIB_SYM(SDL_SetRelativeMouseMode)
SDL_DLIB_SYM(SDL_SetWindowTitle)
SDL_DLIB_SYM(SDL_DestroyTexture)
SDL_DLIB_SYM(SDL_DestroyRenderer)
SDL_DLIB_SYM(SDL_DestroyWindow)
SDL_DLIB_SYM(SDL_GetCurrentVideoDriver)
SDL_DLIB_SYM(SDL_SetHint)
SDL_DLIB_SYM(SDL_CreateWindow)
SDL_DLIB_SYM(SDL_GetWindowSurface)
SDL_DLIB_SYM(SDL_CreateRenderer)
SDL_DLIB_SYM(SDL_CreateTexture)

#define SDL_UpdateWindowSurface SDL_UpdateWindowSurface_dlib
#define SDL_UpdateTexture SDL_UpdateTexture_dlib
#define SDL_RenderCopy SDL_RenderCopy_dlib
#define SDL_RenderPresent SDL_RenderPresent_dlib
#define SDL_GetRelativeMouseState SDL_GetRelativeMouseState_dlib
#define SDL_SetWindowGrab SDL_SetWindowGrab_dlib
#define SDL_SetWindowKeyboardGrab SDL_SetWindowKeyboardGrab_dlib
#define SDL_SetRelativeMouseMode SDL_SetRelativeMouseMode_dlib
#define SDL_SetWindowTitle SDL_SetWindowTitle_dlib
#define SDL_DestroyTexture SDL_DestroyTexture_dlib
#define SDL_DestroyRenderer SDL_DestroyRenderer_dlib
#define SDL_DestroyWindow SDL_DestroyWindow_dlib
#define SDL_GetCurrentVideoDriver SDL_GetCurrentVideoDriver_dlib
#define SDL_SetHint SDL_SetHint_dlib
#define SDL_CreateWindow SDL_CreateWindow_dlib
#define SDL_GetWindowSurface SDL_GetWindowSurface_dlib
#define SDL_CreateRenderer SDL_CreateRenderer_dlib
#define SDL_CreateTexture SDL_CreateTexture_dlib
#endif

static const hid_key_t sdl_key_to_hid_byte_map[] = {
    [SDL_SCANCODE_A] = HID_KEY_A,
    [SDL_SCANCODE_B] = HID_KEY_B,
    [SDL_SCANCODE_C] = HID_KEY_C,
    [SDL_SCANCODE_D] = HID_KEY_D,
    [SDL_SCANCODE_E] = HID_KEY_E,
    [SDL_SCANCODE_F] = HID_KEY_F,
    [SDL_SCANCODE_G] = HID_KEY_G,
    [SDL_SCANCODE_H] = HID_KEY_H,
    [SDL_SCANCODE_I] = HID_KEY_I,
    [SDL_SCANCODE_J] = HID_KEY_J,
    [SDL_SCANCODE_K] = HID_KEY_K,
    [SDL_SCANCODE_L] = HID_KEY_L,
    [SDL_SCANCODE_M] = HID_KEY_M,
    [SDL_SCANCODE_N] = HID_KEY_N,
    [SDL_SCANCODE_O] = HID_KEY_O,
    [SDL_SCANCODE_P] = HID_KEY_P,
    [SDL_SCANCODE_Q] = HID_KEY_Q,
    [SDL_SCANCODE_R] = HID_KEY_R,
    [SDL_SCANCODE_S] = HID_KEY_S,
    [SDL_SCANCODE_T] = HID_KEY_T,
    [SDL_SCANCODE_U] = HID_KEY_U,
    [SDL_SCANCODE_V] = HID_KEY_V,
    [SDL_SCANCODE_W] = HID_KEY_W,
    [SDL_SCANCODE_X] = HID_KEY_X,
    [SDL_SCANCODE_Y] = HID_KEY_Y,
    [SDL_SCANCODE_Z] = HID_KEY_Z,
    [SDL_SCANCODE_0] = HID_KEY_0,
    [SDL_SCANCODE_1] = HID_KEY_1,
    [SDL_SCANCODE_2] = HID_KEY_2,
    [SDL_SCANCODE_3] = HID_KEY_3,
    [SDL_SCANCODE_4] = HID_KEY_4,
    [SDL_SCANCODE_5] = HID_KEY_5,
    [SDL_SCANCODE_6] = HID_KEY_6,
    [SDL_SCANCODE_7] = HID_KEY_7,
    [SDL_SCANCODE_8] = HID_KEY_8,
    [SDL_SCANCODE_9] = HID_KEY_9,
    [SDL_SCANCODE_RETURN] = HID_KEY_ENTER,
    [SDL_SCANCODE_ESCAPE] = HID_KEY_ESC,
    [SDL_SCANCODE_BACKSPACE] = HID_KEY_BACKSPACE,
    [SDL_SCANCODE_TAB] = HID_KEY_TAB,
    [SDL_SCANCODE_SPACE] = HID_KEY_SPACE,
    [SDL_SCANCODE_MINUS] = HID_KEY_MINUS,
    [SDL_SCANCODE_EQUALS] = HID_KEY_EQUAL,
    [SDL_SCANCODE_LEFTBRACKET] = HID_KEY_LEFTBRACE,
    [SDL_SCANCODE_RIGHTBRACKET] = HID_KEY_RIGHTBRACE,
    [SDL_SCANCODE_BACKSLASH] = HID_KEY_BACKSLASH,
    [SDL_SCANCODE_SEMICOLON] = HID_KEY_SEMICOLON,
    [SDL_SCANCODE_APOSTROPHE] = HID_KEY_APOSTROPHE,
    [SDL_SCANCODE_GRAVE] = HID_KEY_GRAVE,
    [SDL_SCANCODE_COMMA] = HID_KEY_COMMA,
    [SDL_SCANCODE_PERIOD] = HID_KEY_DOT,
    [SDL_SCANCODE_SLASH] = HID_KEY_SLASH,
    [SDL_SCANCODE_CAPSLOCK] = HID_KEY_CAPSLOCK,
    [SDL_SCANCODE_F1] = HID_KEY_F1,
    [SDL_SCANCODE_F2] = HID_KEY_F2,
    [SDL_SCANCODE_F3] = HID_KEY_F3,
    [SDL_SCANCODE_F4] = HID_KEY_F4,
    [SDL_SCANCODE_F5] = HID_KEY_F5,
    [SDL_SCANCODE_F6] = HID_KEY_F6,
    [SDL_SCANCODE_F7] = HID_KEY_F7,
    [SDL_SCANCODE_F8] = HID_KEY_F8,
    [SDL_SCANCODE_F9] = HID_KEY_F9,
    [SDL_SCANCODE_F10] = HID_KEY_F10,
    [SDL_SCANCODE_F11] = HID_KEY_F11,
    [SDL_SCANCODE_F12] = HID_KEY_F12,
    [SDL_SCANCODE_SYSREQ] = HID_KEY_SYSRQ,
    [SDL_SCANCODE_SCROLLLOCK] = HID_KEY_SCROLLLOCK,
    [SDL_SCANCODE_PAUSE] = HID_KEY_PAUSE,
    [SDL_SCANCODE_INSERT] = HID_KEY_INSERT,
    [SDL_SCANCODE_HOME] = HID_KEY_HOME,
    [SDL_SCANCODE_PAGEUP] = HID_KEY_PAGEUP,
    [SDL_SCANCODE_DELETE] = HID_KEY_DELETE,
    [SDL_SCANCODE_END] = HID_KEY_END,
    [SDL_SCANCODE_PAGEDOWN] = HID_KEY_PAGEDOWN,
    [SDL_SCANCODE_RIGHT] = HID_KEY_RIGHT,
    [SDL_SCANCODE_LEFT] = HID_KEY_LEFT,
    [SDL_SCANCODE_DOWN] = HID_KEY_DOWN,
    [SDL_SCANCODE_UP] = HID_KEY_UP,
    [SDL_SCANCODE_NUMLOCKCLEAR] = HID_KEY_NUMLOCK,
    [SDL_SCANCODE_KP_DIVIDE] = HID_KEY_KPSLASH,
    [SDL_SCANCODE_KP_MULTIPLY] = HID_KEY_KPASTERISK,
    [SDL_SCANCODE_KP_MINUS] = HID_KEY_KPMINUS,
    [SDL_SCANCODE_KP_PLUS] = HID_KEY_KPPLUS,
    [SDL_SCANCODE_KP_ENTER] = HID_KEY_KPENTER,
    [SDL_SCANCODE_KP_1] = HID_KEY_KP1,
    [SDL_SCANCODE_KP_2] = HID_KEY_KP2,
    [SDL_SCANCODE_KP_3] = HID_KEY_KP3,
    [SDL_SCANCODE_KP_4] = HID_KEY_KP4,
    [SDL_SCANCODE_KP_5] = HID_KEY_KP5,
    [SDL_SCANCODE_KP_6] = HID_KEY_KP6,
    [SDL_SCANCODE_KP_7] = HID_KEY_KP7,
    [SDL_SCANCODE_KP_8] = HID_KEY_KP8,
    [SDL_SCANCODE_KP_9] = HID_KEY_KP9,
    [SDL_SCANCODE_KP_0] = HID_KEY_KP0,
    [SDL_SCANCODE_KP_PERIOD] = HID_KEY_KPDOT,
    [SDL_SCANCODE_APPLICATION] = HID_KEY_COMPOSE,
    [SDL_SCANCODE_KP_EQUALS] = HID_KEY_KPEQUAL,
    [SDL_SCANCODE_INTERNATIONAL1] = HID_KEY_RO,
    [SDL_SCANCODE_INTERNATIONAL2] = HID_KEY_KATAKANAHIRAGANA,
    [SDL_SCANCODE_INTERNATIONAL3] = HID_KEY_YEN,
    [SDL_SCANCODE_INTERNATIONAL4] = HID_KEY_HENKAN,
    [SDL_SCANCODE_INTERNATIONAL5] = HID_KEY_MUHENKAN,
    [SDL_SCANCODE_INTERNATIONAL6] = HID_KEY_KPJPCOMMA,
    [SDL_SCANCODE_LANG1] = HID_KEY_HANGEUL,
    [SDL_SCANCODE_LANG2] = HID_KEY_HANJA,
    [SDL_SCANCODE_LANG3] = HID_KEY_KATAKANA,
    [SDL_SCANCODE_LANG4] = HID_KEY_HIRAGANA,
    [SDL_SCANCODE_LANG5] = HID_KEY_ZENKAKUHANKAKU,
    [SDL_SCANCODE_MENU] = HID_KEY_MENU,
    [SDL_SCANCODE_LCTRL] = HID_KEY_LEFTCTRL,
    [SDL_SCANCODE_LSHIFT] = HID_KEY_LEFTSHIFT,
    [SDL_SCANCODE_LALT] = HID_KEY_LEFTALT,
    [SDL_SCANCODE_LGUI] = HID_KEY_LEFTMETA,
    [SDL_SCANCODE_RCTRL] = HID_KEY_RIGHTCTRL,
    [SDL_SCANCODE_RSHIFT] = HID_KEY_RIGHTSHIFT,
    [SDL_SCANCODE_RALT] = HID_KEY_RIGHTALT,
    [SDL_SCANCODE_RGUI] = HID_KEY_RIGHTMETA,
};

#else
#include <SDL/SDL.h>

#define SDL_LIB_NAME "SDL"

#ifdef SDL_DYNAMIC_LOADING
SDL_DLIB_SYM(SDL_FreeSurface)
SDL_DLIB_SYM(SDL_Flip)
SDL_DLIB_SYM(SDL_WM_GrabInput)
SDL_DLIB_SYM(SDL_WM_SetCaption)
SDL_DLIB_SYM(SDL_SetVideoMode)

#define SDL_FreeSurface SDL_FreeSurface_dlib
#define SDL_Flip SDL_Flip_dlib
#define SDL_WM_GrabInput SDL_WM_GrabInput_dlib
#define SDL_WM_SetCaption SDL_WM_SetCaption_dlib
#define SDL_SetVideoMode SDL_SetVideoMode_dlib
#endif

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
#ifdef __EMSCRIPTEN__
    // I dunno why, I don't want to know why,
    // but some Emscripten SDL keycodes are plain wrong..
    [0xbb] = HID_KEY_EQUAL,
    [0xbd] = HID_KEY_MINUS,
#endif
};

#endif

#ifdef SDL_DYNAMIC_LOADING
SDL_DLIB_SYM(SDL_QuitSubSystem)
SDL_DLIB_SYM(SDL_LockSurface)
SDL_DLIB_SYM(SDL_UnlockSurface)
SDL_DLIB_SYM(SDL_PollEvent)
SDL_DLIB_SYM(SDL_Init)
SDL_DLIB_SYM(SDL_ShowCursor)

#define SDL_QuitSubSystem SDL_QuitSubSystem_dlib
#define SDL_LockSurface SDL_LockSurface_dlib
#define SDL_UnlockSurface SDL_UnlockSurface_dlib
#define SDL_PollEvent SDL_PollEvent_dlib
#define SDL_Init SDL_Init_dlib
#define SDL_ShowCursor SDL_ShowCursor_dlib
#endif

static hid_key_t sdl_key_to_hid(uint32_t sdl_key)
{
    if (sdl_key < sizeof(sdl_key_to_hid_byte_map)) {
        return sdl_key_to_hid_byte_map[sdl_key];
    }
    rvvm_warn("Unknown SDL keycode %d!", sdl_key);
    return HID_KEY_NONE;
}

static hid_key_t sdl_event_to_hid(const SDL_Event* event)
{
#if USE_SDL == 2
    return sdl_key_to_hid(event->key.keysym.scancode);
#else
    return sdl_key_to_hid(event->key.keysym.sym);
#endif
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

#if USE_SDL == 2
static SDL_Window* sdl_window = NULL;
static SDL_Renderer* sdl_renderer = NULL;
static SDL_Texture* sdl_texture = NULL;
#endif
static SDL_Surface* sdl_surface = NULL;
static bool sdl_grabbed = false;

static void sdl_window_draw(gui_window_t* win)
{
    if (sdl_surface && win->fb.buffer != sdl_surface->pixels) {
        // Copy the framebuffer onto a locking surface
        SDL_LockSurface(sdl_surface);
        memcpy(sdl_surface->pixels, win->fb.buffer, framebuffer_size(&win->fb));
        SDL_UnlockSurface(sdl_surface);
    }
#if USE_SDL == 2
    if (sdl_surface) {
        SDL_UpdateWindowSurface(sdl_window);
    } else {
        // Load the framebuffer into a texture and draw onto the screen
        SDL_UpdateTexture(sdl_texture, NULL, win->fb.buffer, framebuffer_stride(&win->fb));
        SDL_RenderCopy(sdl_renderer, sdl_texture, NULL, NULL);
        SDL_RenderPresent(sdl_renderer);
    }
#else
    SDL_Flip(sdl_surface);
#endif
}

static void sdl_window_poll(gui_window_t* win)
{
    SDL_Event event = {0};
    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_KEYDOWN:
                win->on_key_press(win, sdl_event_to_hid(&event));
                break;
            case SDL_KEYUP:
                win->on_key_release(win, sdl_event_to_hid(&event));
                break;
            case SDL_MOUSEMOTION:
                if (sdl_grabbed) {
                    win->on_mouse_move(win, event.motion.xrel, event.motion.yrel);
                } else {
                    win->on_mouse_place(win, event.motion.x, event.motion.y);
                }
                break;
#if USE_SDL == 2
            case SDL_MOUSEWHEEL:
                win->on_mouse_scroll(win, event.wheel.y);
                break;
#endif
            case SDL_MOUSEBUTTONDOWN:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    win->on_mouse_press(win, HID_BTN_LEFT);
                } else if (event.button.button == SDL_BUTTON_MIDDLE) {
                    win->on_mouse_press(win, HID_BTN_MIDDLE);
                } else if (event.button.button == SDL_BUTTON_RIGHT) {
                    win->on_mouse_press(win, HID_BTN_RIGHT);
#if USE_SDL == 1
                } else if (event.button.button == SDL_BUTTON_WHEELUP) {
                    win->on_mouse_scroll(win, HID_SCROLL_UP);
                } else if (event.button.button == SDL_BUTTON_WHEELDOWN) {
                    win->on_mouse_scroll(win, HID_SCROLL_DOWN);
#endif
                }
                break;
            case SDL_MOUSEBUTTONUP:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    win->on_mouse_release(win, HID_BTN_LEFT);
                } else if (event.button.button == SDL_BUTTON_MIDDLE) {
                    win->on_mouse_release(win, HID_BTN_MIDDLE);
                } else if (event.button.button == SDL_BUTTON_RIGHT) {
                    win->on_mouse_release(win, HID_BTN_RIGHT);
                }
                break;
            case SDL_QUIT:
                win->on_close(win);
                break;
#if USE_SDL == 2
            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                    win->on_focus_lost(win);
                }
                break;
#else
            case SDL_ACTIVEEVENT:
                if (event.active.state == SDL_APPINPUTFOCUS && !event.active.gain) {
                    win->on_focus_lost(win);
                }
                break;
#endif
        }
    }
}

static void sdl_window_grab_input(gui_window_t* win, bool grab)
{
    UNUSED(win);
    sdl_grabbed = grab;
#if USE_SDL == 2
    SDL_SetWindowGrab(sdl_window, grab);
    SDL_SetWindowKeyboardGrab(sdl_window, grab);
    SDL_SetRelativeMouseMode(grab);
#else
    SDL_WM_GrabInput(grab ? SDL_GRAB_ON : SDL_GRAB_OFF);
#endif
}

static void sdl_window_set_title(gui_window_t* win, const char* title)
{
    UNUSED(win);
#if USE_SDL == 2
    SDL_SetWindowTitle(sdl_window, title);
#else
    SDL_WM_SetCaption(title, NULL);
#endif
}

static void sdl_window_remove(gui_window_t* win)
{
    if (sdl_surface == NULL || win->fb.buffer != sdl_surface->pixels) {
        vma_free(win->fb.buffer, framebuffer_size(&win->fb));
    }
    sdl_window_grab_input(win, false);
#if USE_SDL == 2
    if (sdl_texture) SDL_DestroyTexture(sdl_texture);
    if (sdl_renderer) SDL_DestroyRenderer(sdl_renderer);
    if (sdl_window) SDL_DestroyWindow(sdl_window);
    sdl_window = NULL;
    sdl_renderer = NULL;
    sdl_texture = NULL;
#else
    SDL_FreeSurface(sdl_surface);
#endif
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    sdl_surface = NULL;
}

#define SDL_DLIB_RESOLVE(lib, sym) \
do { \
    sym = dlib_resolve(lib, #sym);\
    if (sym == NULL) return false; \
} while (0)

static bool sdl_init_libs(void)
{
#ifdef SDL_DYNAMIC_LOADING
    dlib_ctx_t* libsdl = dlib_open(SDL_LIB_NAME, DLIB_NAME_PROBE);
#if USE_SDL == 2
    SDL_DLIB_RESOLVE(libsdl, SDL_UpdateWindowSurface);
    SDL_DLIB_RESOLVE(libsdl, SDL_UpdateTexture);
    SDL_DLIB_RESOLVE(libsdl, SDL_RenderCopy);
    SDL_DLIB_RESOLVE(libsdl, SDL_RenderPresent);
    SDL_DLIB_RESOLVE(libsdl, SDL_GetRelativeMouseState);
    SDL_DLIB_RESOLVE(libsdl, SDL_SetWindowGrab);
    SDL_DLIB_RESOLVE(libsdl, SDL_SetWindowKeyboardGrab);
    SDL_DLIB_RESOLVE(libsdl, SDL_SetRelativeMouseMode);
    SDL_DLIB_RESOLVE(libsdl, SDL_SetWindowTitle);
    SDL_DLIB_RESOLVE(libsdl, SDL_DestroyTexture);
    SDL_DLIB_RESOLVE(libsdl, SDL_DestroyRenderer);
    SDL_DLIB_RESOLVE(libsdl, SDL_DestroyWindow);
    SDL_DLIB_RESOLVE(libsdl, SDL_GetCurrentVideoDriver);
    SDL_DLIB_RESOLVE(libsdl, SDL_SetHint);
    SDL_DLIB_RESOLVE(libsdl, SDL_CreateWindow);
    SDL_DLIB_RESOLVE(libsdl, SDL_GetWindowSurface);
    SDL_DLIB_RESOLVE(libsdl, SDL_CreateRenderer);
    SDL_DLIB_RESOLVE(libsdl, SDL_CreateTexture);
#else
    SDL_DLIB_RESOLVE(libsdl, SDL_FreeSurface);
    SDL_DLIB_RESOLVE(libsdl, SDL_Flip);
    SDL_DLIB_RESOLVE(libsdl, SDL_WM_GrabInput);
    SDL_DLIB_RESOLVE(libsdl, SDL_WM_SetCaption);
    SDL_DLIB_RESOLVE(libsdl, SDL_SetVideoMode);
#endif
    SDL_DLIB_RESOLVE(libsdl, SDL_QuitSubSystem);
    SDL_DLIB_RESOLVE(libsdl, SDL_LockSurface);
    SDL_DLIB_RESOLVE(libsdl, SDL_UnlockSurface);
    SDL_DLIB_RESOLVE(libsdl, SDL_PollEvent);
    SDL_DLIB_RESOLVE(libsdl, SDL_Init);
    SDL_DLIB_RESOLVE(libsdl, SDL_ShowCursor);
#endif
    return true;
}

bool sdl_window_init(gui_window_t* win)
{
    static bool libsdl_avail = false;
    DO_ONCE(libsdl_avail = sdl_init_libs());
    if (!libsdl_avail) {
        rvvm_info("Failed to load libSDL!");
        return false;
    }

#ifndef _WIN32
    DO_ONCE(setenv("SDL_DEBUG", "1", false));
#endif

    if (sdl_surface) {
        // SDL_PollEvent is very inconvenient to use, SDL1 doesn't support multiwindow at all
        rvvm_error("SDL doesn't support multiple windows");
        return false;
    }
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        rvvm_error("Failed to initialize SDL");
        return false;
    }
#if USE_SDL == 2
    if (rvvm_strcmp(SDL_GetCurrentVideoDriver(), "x11")) {
        // Prevent messing with the compositor
        SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
        // Force software flipping (Reduces idle CPU use, prevents issues on messy hosts)
        SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0");
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "software");
    }
    sdl_window = SDL_CreateWindow("RVVM", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                  win->fb.width, win->fb.height, SDL_WINDOW_SHOWN);
    if (sdl_window == NULL) {
        rvvm_error("SDL_CreateWindow() failed!");
        return false;
    }
    sdl_surface = SDL_GetWindowSurface(sdl_window);
    if (sdl_surface == NULL) {
        rvvm_info("No SDL framebuffer surface, using SDL renderer. Expect higher CPU use.");
        sdl_renderer = SDL_CreateRenderer(sdl_window, -1, 0);
        sdl_texture = SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_ARGB8888,
                                        SDL_TEXTUREACCESS_STREAMING, win->fb.width, win->fb.height);
        win->fb.buffer = vma_alloc(NULL, framebuffer_size(&win->fb), VMA_RDWR);
    }
#else
    sdl_surface = SDL_SetVideoMode(win->fb.width, win->fb.height,
                    rgb_format_bpp(win->fb.format), SDL_ANYFORMAT);
    if (sdl_surface == NULL) {
        rvvm_error("SDL_SetVideoMode() failed!");
        return false;
    }
    SDL_WM_SetCaption("RVVM", NULL);
#endif
    SDL_ShowCursor(SDL_DISABLE);
    if (sdl_surface) {
        win->fb.format = sdl_get_rgb_format(sdl_surface->format);
        if (SDL_MUSTLOCK(sdl_surface)) {
            rvvm_info("SDL surface is locking. Expect higher CPU use.");
            win->fb.buffer = vma_alloc(NULL, framebuffer_size(&win->fb), VMA_RDWR);
        } else {
            // Direct framebuffer surface, like XShm
            if (((size_t)sdl_surface->pixels) & 0xFFF) {
                rvvm_info("SDL surface is misaligned. Expect higher CPU use.");
            }
            win->fb.buffer = sdl_surface->pixels;
        }
    }

    win->draw = sdl_window_draw;
    win->poll = sdl_window_poll;
    win->remove = sdl_window_remove;
    win->grab_input = sdl_window_grab_input;
    win->set_title = sdl_window_set_title;

    return true;
}

#else

bool sdl_window_init(gui_window_t* win)
{
    UNUSED(win);
    return false;
}

#endif
