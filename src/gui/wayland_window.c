/*
wayland-window.c - Wayland GUI Window
Copyright (C) 2023  0xCatPKG <0xCatPKG@rvvm.dev>
              2025  LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

// Must be included before system headers
#include "feature_test.h" // IWYU pragma: keep

#include "compiler.h"
#include "gui_window.h"

PUSH_OPTIMIZATION_SIZE

#if !defined(HOST_TARGET_POSIX) || HOST_TARGET_POSIX < 200112L
#undef USE_WAYLAND
#endif

#if defined(USE_WAYLAND) && !CHECK_INCLUDE(wayland-client-core.h, 0)
#undef USE_WAYLAND
#pragma message("Disabling Wayland support as <wayland-client-core.h> is unavailable")
#endif

#if defined(USE_WAYLAND) && !CHECK_INCLUDE(xkbcommon/xkbcommon.h, 0)
#undef USE_WAYLAND
#pragma message("Disabling Wayland support as <xkbcommon/xkbcommon.h> is unavailable")
#endif

#if defined(USE_WAYLAND)

#include "spinlock.h"
#include "threading.h"
#include "utils.h"
#include "vector.h"
#include "vma_ops.h"

#include <sys/mman.h>
#include <sys/select.h>
#include <unistd.h>
#include <wayland-client-core.h>
#include <xkbcommon/xkbcommon.h>

#if defined(USE_LIBS_PROBE)

// Resolve symbols at runtime
#include "dlib.h"

#define WAYLAND_DLIB_SYM(sym) static __typeof__(sym)* MACRO_CONCAT(sym, _dlib) = NULL;

// Functions
WAYLAND_DLIB_SYM(wl_display_cancel_read)
WAYLAND_DLIB_SYM(wl_display_connect)
WAYLAND_DLIB_SYM(wl_display_disconnect)
WAYLAND_DLIB_SYM(wl_display_dispatch_pending)
WAYLAND_DLIB_SYM(wl_display_flush)
WAYLAND_DLIB_SYM(wl_display_get_fd)
WAYLAND_DLIB_SYM(wl_display_prepare_read)
WAYLAND_DLIB_SYM(wl_display_read_events)
WAYLAND_DLIB_SYM(wl_display_roundtrip)
WAYLAND_DLIB_SYM(wl_proxy_get_version)
WAYLAND_DLIB_SYM(wl_proxy_marshal_flags)
WAYLAND_DLIB_SYM(wl_proxy_add_listener)
WAYLAND_DLIB_SYM(wl_proxy_set_user_data)
WAYLAND_DLIB_SYM(wl_proxy_get_user_data)
WAYLAND_DLIB_SYM(wl_proxy_destroy)

#define wl_display_cancel_read      wl_display_cancel_read_dlib
#define wl_display_connect          wl_display_connect_dlib
#define wl_display_disconnect       wl_display_disconnect_dlib
#define wl_display_dispatch_pending wl_display_dispatch_pending_dlib
#define wl_display_flush            wl_display_flush_dlib
#define wl_display_get_fd           wl_display_get_fd_dlib
#define wl_display_prepare_read     wl_display_prepare_read_dlib
#define wl_display_read_events      wl_display_read_events_dlib
#define wl_display_roundtrip        wl_display_roundtrip_dlib
#define wl_proxy_get_version        wl_proxy_get_version_dlib
#define wl_proxy_marshal_flags      wl_proxy_marshal_flags_dlib
#define wl_proxy_add_listener       wl_proxy_add_listener_dlib
#define wl_proxy_set_user_data      wl_proxy_set_user_data_dlib
#define wl_proxy_get_user_data      wl_proxy_get_user_data_dlib
#define wl_proxy_destroy            wl_proxy_destroy_dlib

WAYLAND_DLIB_SYM(xkb_context_new)
WAYLAND_DLIB_SYM(xkb_context_unref)
WAYLAND_DLIB_SYM(xkb_keymap_new_from_string)
WAYLAND_DLIB_SYM(xkb_state_new)
WAYLAND_DLIB_SYM(xkb_state_key_get_one_sym)
WAYLAND_DLIB_SYM(xkb_state_unref)
WAYLAND_DLIB_SYM(xkb_keymap_unref)

#define xkb_context_new            xkb_context_new_dlib
#define xkb_context_unref          xkb_context_unref_dlib
#define xkb_keymap_new_from_string xkb_keymap_new_from_string_dlib
#define xkb_state_new              xkb_state_new_dlib
#define xkb_state_key_get_one_sym  xkb_state_key_get_one_sym_dlib
#define xkb_state_unref            xkb_state_unref_dlib
#define xkb_keymap_unref           xkb_keymap_unref_dlib

#endif

// Protocol autogen
#include "wayland_client_protocols.h"

// From <linux/input-event-codes.h>
#define WL_BTN_LEFT   0x110
#define WL_BTN_RIGHT  0x111
#define WL_BTN_MIDDLE 0x112

/*
 * Data structures
 */

// Per-keyboard data, stores currently entered surface & XKB state
typedef struct {
    struct wl_surface* surface;
    struct xkb_state*  xkb_state;
} wl_kb_data_t;

// Per-pointer data, stores currently entered surface
typedef struct pointer_data {
    struct wl_surface* surface;
} wl_ptr_data_t;

// Per-seat data, kept as vector for input grab & cleanup
typedef struct seat_data {
    // Keyboard/Pointer userdata
    wl_kb_data_t  kb_data;
    wl_ptr_data_t ptr_data;

    // Wayland seat handle
    struct wl_seat* seat;
    // Wayland keyboard handle
    struct wl_keyboard* keyboard;
    // Wayland pointer handle
    struct wl_pointer* pointer;
    // Wayland relative pointer handle;
    struct zwp_relative_pointer_v1* relative;
} wl_seat_data_t;

// Window data
typedef struct {
    // Wayland surface
    struct wl_surface* surface;

    // Shared memory pool backed by vram_fd
    struct wl_shm_pool* shm_pool;

    // Frame buffer residing inside shm_pool
    struct wl_buffer* buffer;

    // XDG namespace
    struct xdg_surface*  xdg_surface;
    struct xdg_toplevel* xdg_toplevel;

    // Window decorations
    struct zxdg_toplevel_decoration_v1* xdg_decoration;

    // Pointer grab handle
    struct zwp_locked_pointer_v1* ptr_lock;

    // Keyboard grab handle
    struct zwp_keyboard_shortcuts_inhibitor_v1* kbd_inhibit;

    // Last scanout context
    rvvm_fb_t fb;

    // XDG Surface configured
    bool xdg_configured;
} wl_window_t;

/*
 * Global data
 */

// Required globals
static struct wl_display*    wl_display    = NULL;
static struct wl_registry*   wl_registry   = NULL;
static struct wl_compositor* wl_compositor = NULL;
static struct wl_shm*        wl_shm        = NULL;
static struct xdg_wm_base*   xdg_wm_base   = NULL;

// Optional globals
static struct zxdg_decoration_manager_v1*                xdg_decor_mgr      = NULL;
static struct zwp_relative_pointer_manager_v1*           wl_relative_mgr    = NULL;
static struct zwp_pointer_constraints_v1*                wl_ptr_constraints = NULL;
static struct zwp_keyboard_shortcuts_inhibit_manager_v1* wl_kbd_inhibit_mgr = NULL;

// Wayland seats
static vector_t(wl_seat_data_t*) wl_seats = ZERO_INIT;

// XKB context for keymap handling
struct xkb_context* xkb_context = NULL;

// Event thread
static uint32_t      wl_windows = 0;
static spinlock_t    wl_lock    = ZERO_INIT;
static thread_ctx_t* wl_thread  = NULL;

/*
 * Linux evdev keycode → HID usage
 *
 * Wayland's wl_keyboard.key event delivers `key` as a Linux input
 * event keycode (evdev). That's a STABLE physical-key identifier —
 * it doesn't depend on which keyboard layout the user has loaded.
 * Going via xkb_state_key_get_one_sym() instead would apply the user's
 * current layout (Workman, Dvorak, Colemak, AZERTY, ...) and the
 * emulator would receive layout-translated letters rather than
 * physical key positions. For a hardware emulator that wants its own
 * keyboard layout (or, like the ZX Spectrum, no layout — just key
 * positions) that's the wrong behaviour.
 *
 * Indexed by evdev keycode, valued in HID usage codes. */
static const hid_key_t evdev_to_hid[256] = {
    [1]   = HID_KEY_ESC,
    [2]   = HID_KEY_1,         [3]  = HID_KEY_2,         [4]  = HID_KEY_3,
    [5]   = HID_KEY_4,         [6]  = HID_KEY_5,         [7]  = HID_KEY_6,
    [8]   = HID_KEY_7,         [9]  = HID_KEY_8,         [10] = HID_KEY_9,
    [11]  = HID_KEY_0,
    [12]  = HID_KEY_MINUS,     [13] = HID_KEY_EQUAL,
    [14]  = HID_KEY_BACKSPACE, [15] = HID_KEY_TAB,
    [16]  = HID_KEY_Q,         [17] = HID_KEY_W,         [18] = HID_KEY_E,
    [19]  = HID_KEY_R,         [20] = HID_KEY_T,         [21] = HID_KEY_Y,
    [22]  = HID_KEY_U,         [23] = HID_KEY_I,         [24] = HID_KEY_O,
    [25]  = HID_KEY_P,
    [26]  = HID_KEY_LEFTBRACE, [27] = HID_KEY_RIGHTBRACE,
    [28]  = HID_KEY_ENTER,
    [29]  = HID_KEY_LEFTCTRL,
    [30]  = HID_KEY_A,         [31] = HID_KEY_S,         [32] = HID_KEY_D,
    [33]  = HID_KEY_F,         [34] = HID_KEY_G,         [35] = HID_KEY_H,
    [36]  = HID_KEY_J,         [37] = HID_KEY_K,         [38] = HID_KEY_L,
    [39]  = HID_KEY_SEMICOLON, [40] = HID_KEY_APOSTROPHE,
    [41]  = HID_KEY_GRAVE,     [42] = HID_KEY_LEFTSHIFT,
    [43]  = HID_KEY_BACKSLASH,
    [44]  = HID_KEY_Z,         [45] = HID_KEY_X,         [46] = HID_KEY_C,
    [47]  = HID_KEY_V,         [48] = HID_KEY_B,         [49] = HID_KEY_N,
    [50]  = HID_KEY_M,
    [51]  = HID_KEY_COMMA,     [52] = HID_KEY_DOT,       [53] = HID_KEY_SLASH,
    [54]  = HID_KEY_RIGHTSHIFT,
    [55]  = HID_KEY_KPASTERISK,
    [56]  = HID_KEY_LEFTALT,
    [57]  = HID_KEY_SPACE,
    [58]  = HID_KEY_CAPSLOCK,
    [59]  = HID_KEY_F1,        [60] = HID_KEY_F2,        [61] = HID_KEY_F3,
    [62]  = HID_KEY_F4,        [63] = HID_KEY_F5,        [64] = HID_KEY_F6,
    [65]  = HID_KEY_F7,        [66] = HID_KEY_F8,        [67] = HID_KEY_F9,
    [68]  = HID_KEY_F10,
    [69]  = HID_KEY_NUMLOCK,   [70] = HID_KEY_SCROLLLOCK,
    [71]  = HID_KEY_KP7,       [72] = HID_KEY_KP8,       [73] = HID_KEY_KP9,
    [74]  = HID_KEY_KPMINUS,
    [75]  = HID_KEY_KP4,       [76] = HID_KEY_KP5,       [77] = HID_KEY_KP6,
    [78]  = HID_KEY_KPPLUS,
    [79]  = HID_KEY_KP1,       [80] = HID_KEY_KP2,       [81] = HID_KEY_KP3,
    [82]  = HID_KEY_KP0,       [83] = HID_KEY_KPDOT,
    [86]  = HID_KEY_102ND,
    [87]  = HID_KEY_F11,       [88] = HID_KEY_F12,
    [96]  = HID_KEY_KPENTER,
    [97]  = HID_KEY_RIGHTCTRL,
    [98]  = HID_KEY_KPSLASH,
    [99]  = HID_KEY_SYSRQ,
    [100] = HID_KEY_RIGHTALT,
    [102] = HID_KEY_HOME,      [103] = HID_KEY_UP,
    [104] = HID_KEY_PAGEUP,
    [105] = HID_KEY_LEFT,      [106] = HID_KEY_RIGHT,
    [107] = HID_KEY_END,       [108] = HID_KEY_DOWN,
    [109] = HID_KEY_PAGEDOWN,
    [110] = HID_KEY_INSERT,    [111] = HID_KEY_DELETE,
    [117] = HID_KEY_KPEQUAL,
    [119] = HID_KEY_PAUSE,
    [125] = HID_KEY_LEFTMETA,  [126] = HID_KEY_RIGHTMETA,
    [127] = HID_KEY_COMPOSE,
};

static hid_key_t wayland_evdev_keycode_to_hid(uint32_t key)
{
    if (key < sizeof(evdev_to_hid) / sizeof(evdev_to_hid[0])) {
        hid_key_t hid = evdev_to_hid[key];
        if (hid != HID_KEY_NONE) return hid;
    }
    rvvm_warn("Unmapped evdev keycode %u", (uint32_t)key);
    return HID_KEY_NONE;
}

/*
 * Wayland keyboard event callbacks
 */

static void wl_keyboard_on_keymap(void* data, struct wl_keyboard* keyboard, //
                                  uint32_t format, int32_t fd, uint32_t size)
{
    wl_kb_data_t* kb_data = data;
    UNUSED(keyboard);
    if (kb_data && format) {
        void* map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (map != MAP_FAILED) {
            struct xkb_keymap* keymap = xkb_keymap_new_from_string(xkb_context, map,          //
                                                                   XKB_KEYMAP_FORMAT_TEXT_V1, //
                                                                   XKB_KEYMAP_COMPILE_NO_FLAGS);
            if (keymap) {
                if (kb_data->xkb_state) {
                    xkb_state_unref(kb_data->xkb_state);
                }
                kb_data->xkb_state = xkb_state_new(keymap);
                xkb_keymap_unref(keymap);
            }
            munmap(map, size);
        }
        close(fd);
    }
}

static void wl_keyboard_on_enter(void* data, struct wl_keyboard* keyboard, uint32_t serial, //
                                 struct wl_surface* surface, struct wl_array* keys)
{
    wl_kb_data_t* kb_data = data;
    UNUSED(keyboard && serial && keys);
    if (kb_data) {
        kb_data->surface = surface;
    }
}

static void wl_keyboard_on_leave(void* data, struct wl_keyboard* keyboard, //
                                 uint32_t serial, struct wl_surface* surface)
{
    wl_kb_data_t* kb_data = data;
    UNUSED(keyboard && serial);
    if (kb_data && surface) {
        gui_backend_on_focus_lost(wl_surface_get_user_data(surface));
        kb_data->surface = NULL;
    }
}

static void wl_keyboard_on_key(void* data, struct wl_keyboard* keyboard, uint32_t serial, //
                               uint32_t time, uint32_t key, uint32_t state)
{
    wl_kb_data_t* kb_data = data;
    UNUSED(keyboard && serial && time);
    if (kb_data && kb_data->surface) {
        /* Map evdev keycode → HID directly. Bypassing xkb here makes
         * the emulator see physical key positions (USB-HID convention)
         * regardless of which keyboard layout the user has loaded on
         * the host — Workman / Dvorak / Colemak / AZERTY all behave
         * the same and the emulated machine sees a US-QWERTY-style
         * physical keyboard. */
        gui_window_t* win     = wl_surface_get_user_data(kb_data->surface);
        hid_key_t     hid_key = wayland_evdev_keycode_to_hid(key);
        if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
            gui_backend_on_key_press(win, hid_key);
        } else if (state == WL_KEYBOARD_KEY_STATE_RELEASED) {
            gui_backend_on_key_release(win, hid_key);
        }
    }
}

static void wl_keyboard_on_modifiers(void* data, struct wl_keyboard* keyboard, uint32_t serial, //
                                     uint32_t mods_depressed, uint32_t mods_latched,            //
                                     uint32_t mods_locked, uint32_t group)
{
    UNUSED(data && keyboard && serial && mods_depressed && mods_latched && mods_locked && group);
}

static void wl_keyboard_on_repeat_info(void* data, struct wl_keyboard* keyboard, //
                                       int32_t rate, int32_t delay)
{
    UNUSED(data && keyboard && rate && delay);
}

static const struct wl_keyboard_listener keyboard_listener = {
    .keymap      = wl_keyboard_on_keymap,
    .enter       = wl_keyboard_on_enter,
    .leave       = wl_keyboard_on_leave,
    .key         = wl_keyboard_on_key,
    .modifiers   = wl_keyboard_on_modifiers,
    .repeat_info = wl_keyboard_on_repeat_info,
};

/*
 * Wayland pointer event callbacks
 */

static void wl_pointer_on_enter(void* data, struct wl_pointer* pointer, uint32_t serial, //
                                struct wl_surface* surface, wl_fixed_t x, wl_fixed_t y)
{
    wl_ptr_data_t* ptr_data = data;
    UNUSED(data && x && y);
    if (ptr_data && pointer) {
        // Store surface entered by pointer
        ptr_data->surface = surface;
        // Hide cursor
        wl_pointer_set_cursor(pointer, serial, NULL, 0, 0);
    }
}

static void wl_pointer_on_leave(void* data, struct wl_pointer* pointer, //
                                uint32_t serial, struct wl_surface* surface)
{
    wl_ptr_data_t* ptr_data = data;
    UNUSED(data && pointer && serial && surface);
    if (ptr_data) {
        ptr_data->surface = NULL;
    }
}

static void wl_pointer_on_motion(void* data, struct wl_pointer* pointer, //
                                 uint32_t time, wl_fixed_t x, wl_fixed_t y)
{
    wl_ptr_data_t* ptr_data = data;
    UNUSED(pointer && time);
    if (ptr_data && ptr_data->surface) {
        gui_window_t* win = wl_surface_get_user_data(ptr_data->surface);
        wl_window_t*  wl  = gui_backend_get_data(win);
        if (!wl->ptr_lock) {
            gui_backend_on_mouse_place(win, wl_fixed_to_int(x), wl_fixed_to_int(y));
        }
    }
}

static void wl_pointer_on_button(void* data, struct wl_pointer* pointer, uint32_t serial, //
                                 uint32_t time, uint32_t button, uint32_t state)
{
    wl_ptr_data_t* ptr_data = data;
    UNUSED(pointer && serial && time);
    if (ptr_data && ptr_data->surface) {
        gui_window_t* win  = wl_surface_get_user_data(ptr_data->surface);
        hid_btns_t    btns = 0;
        if (button == WL_BTN_LEFT) {
            btns |= HID_BTN_LEFT;
        } else if (button == WL_BTN_RIGHT) {
            btns |= HID_BTN_RIGHT;
        } else if (button == WL_BTN_MIDDLE) {
            btns |= HID_BTN_MIDDLE;
        }
        if (state == WL_POINTER_BUTTON_STATE_PRESSED) {
            gui_backend_on_mouse_press(win, btns);
        } else if (state == WL_POINTER_BUTTON_STATE_RELEASED) {
            gui_backend_on_mouse_release(win, btns);
        }
    }
}

static void wl_pointer_on_axis(void* data, struct wl_pointer* pointer, //
                               uint32_t time, uint32_t axis, wl_fixed_t value)
{
    wl_ptr_data_t* ptr_data = data;
    UNUSED(pointer && time);
    if (ptr_data && ptr_data->surface && !axis) {
        gui_window_t* win = wl_surface_get_user_data(ptr_data->surface);
        gui_backend_on_mouse_scroll(win, EVAL_MAX(EVAL_MIN(wl_fixed_to_int(value), 1), -1));
    }
}

static void wl_pointer_on_frame(void* data, struct wl_pointer* pointer)
{
    UNUSED(data && pointer);
}

static void wl_pointer_on_axis_source(void* data, struct wl_pointer* pointer, uint32_t source)
{
    UNUSED(data && pointer && source);
}

static void wl_pointer_on_axis_u32(void* data, struct wl_pointer* pointer, uint32_t a, uint32_t b)
{
    UNUSED(data && pointer && a && b);
}

static void wl_pointer_on_axis_i32(void* data, struct wl_pointer* pointer, uint32_t a, int32_t b)
{
    UNUSED(data && pointer && a && b);
}

static const struct wl_pointer_listener pointer_listener = {
    .enter                   = wl_pointer_on_enter,
    .leave                   = wl_pointer_on_leave,
    .motion                  = wl_pointer_on_motion,
    .button                  = wl_pointer_on_button,
    .axis                    = wl_pointer_on_axis,
    .frame                   = wl_pointer_on_frame,
    .axis_source             = wl_pointer_on_axis_source,
    .axis_stop               = wl_pointer_on_axis_u32,
    .axis_discrete           = wl_pointer_on_axis_i32,
    .axis_value120           = wl_pointer_on_axis_i32,
    .axis_relative_direction = wl_pointer_on_axis_u32,
};

/*
 * Wayland zwp_relative_pointer_v1 relative pointer event callbacks
 */

static void relative_pointer_on_relative_motion(void* data, struct zwp_relative_pointer_v1* relative_pointer,
                                                uint32_t utime_hi, uint32_t utime_lo, wl_fixed_t dx, wl_fixed_t dy,
                                                wl_fixed_t dx_unaccel, wl_fixed_t dy_unaccel)
{
    wl_ptr_data_t* ptr_data = data;
    UNUSED(relative_pointer && utime_hi && utime_lo && dx && dy);
    if (ptr_data && ptr_data->surface) {
        gui_window_t* win = wl_surface_get_user_data(ptr_data->surface);
        wl_window_t*  wl  = gui_backend_get_data(win);
        if (wl->ptr_lock) {
            gui_backend_on_mouse_move(win, wl_fixed_to_int(dx_unaccel), wl_fixed_to_int(dy_unaccel));
        }
    }
}

static const struct zwp_relative_pointer_v1_listener relative_pointer_listener = {
    .relative_motion = relative_pointer_on_relative_motion,
};

/*
 * Wayland seat callbacks
 */

static void wl_seat_on_capabilities(void* data, struct wl_seat* seat, uint32_t capabilities)
{
    wl_seat_data_t* seat_data = data;
    if (seat_data) {
        scoped_spin_lock (&wl_lock) {
            if (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) {
                seat_data->keyboard = wl_seat_get_keyboard(seat);
                if (seat_data->keyboard) {
                    wl_keyboard_add_listener(seat_data->keyboard, &keyboard_listener, &seat_data->kb_data);
                }
            } else if (seat_data->keyboard) {
                wl_keyboard_destroy(seat_data->keyboard);
                seat_data->keyboard = NULL;
            }
            if (capabilities & WL_SEAT_CAPABILITY_POINTER) {
                seat_data->pointer = wl_seat_get_pointer(seat);
                if (seat_data->pointer) {
                    wl_pointer_add_listener(seat_data->pointer, &pointer_listener, &seat_data->ptr_data);
                    if (wl_relative_mgr) {
                        seat_data->relative = zwp_relative_pointer_manager_v1_get_relative_pointer( //
                            wl_relative_mgr, seat_data->pointer);
                        zwp_relative_pointer_v1_add_listener( //
                            seat_data->relative, &relative_pointer_listener, &seat_data->ptr_data);
                    }
                }
            } else if (seat_data->pointer) {
                if (seat_data->relative) {
                    zwp_relative_pointer_v1_destroy(seat_data->relative);
                    seat_data->relative = NULL;
                }
                wl_pointer_destroy(seat_data->pointer);
                seat_data->pointer = NULL;
            }
        }
    }
}

static void wl_seat_on_name(void* data, struct wl_seat* seat, const char* name)
{
    UNUSED(data && seat && name);
}

static const struct wl_seat_listener seat_listener = {
    .capabilities = wl_seat_on_capabilities,
    .name         = wl_seat_on_name,
};

/*
 * XDG Window Manager base callbacks
 */

static void xdg_wm_base_ping(void* data, struct xdg_wm_base* base, uint32_t serial)
{
    // Must be handled, otherwise window is assumed to be "Not responding"
    xdg_wm_base_pong(base, serial);
    UNUSED(data);
}

static const struct xdg_wm_base_listener wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

/*
 * XDG Surface callbacks
 */

static void xdg_surface_on_configure(void* data, struct xdg_surface* xdg_surface, uint32_t serial)
{
    gui_window_t* win = data;
    wl_window_t*  wl  = gui_backend_get_data(win);
    if (wl && xdg_surface) {
        scoped_spin_lock (&wl_lock) {
            // I have no idea what this does... Makes surface opaque?
            struct wl_region* region = wl_compositor_create_region(wl_compositor);
            wl_region_add(region, 0, 0, rvvm_fb_width(&wl->fb), rvvm_fb_height(&wl->fb));
            wl_surface_set_opaque_region(wl->surface, region);
            wl_surface_commit(wl->surface);
            wl_region_destroy(region);
            // Must be called, otherwise window isn't visible
            xdg_surface_ack_configure(xdg_surface, serial);
            wl->xdg_configured = true;
        }
    }
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_on_configure,
};

/*
 * XDG Toplevel callbacks
 */

static void xdg_toplevel_on_configure(void* data, struct xdg_toplevel* xdg_toplevel, //
                                      int32_t width, int32_t height, struct wl_array* states)
{
    UNUSED(data && xdg_toplevel && width && height && states);
}

static void xdg_toplevel_on_close(void* data, struct xdg_toplevel* xdg_toplevel)
{
    UNUSED(xdg_toplevel);
    gui_backend_on_close(data);
}

static void xdg_toplevel_on_configure_bounds(void* data, struct xdg_toplevel* xdg_toplevel, //
                                             int32_t width, int32_t height)
{
    UNUSED(data && xdg_toplevel && width && height);
}

static void xdg_toplevel_on_wm_capabilities(void* data, struct xdg_toplevel* xdg_toplevel, //
                                            struct wl_array* capabilities)
{
    UNUSED(data && xdg_toplevel && capabilities);
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure        = xdg_toplevel_on_configure,
    .close            = xdg_toplevel_on_close,
    .configure_bounds = xdg_toplevel_on_configure_bounds,
    .wm_capabilities  = xdg_toplevel_on_wm_capabilities,
};

/*
 * Wayland Registry callbacks
 */

static void wl_registry_on_global(void* data, struct wl_registry* registry, //
                                  uint32_t name, const char* interface, uint32_t version)
{
    UNUSED(registry && data);
    scoped_spin_lock (&wl_lock) {
        if (rvvm_strcmp(interface, "wl_compositor")) {
            wl_compositor = wl_registry_bind(registry, name, &wl_compositor_interface, version);
        } else if (rvvm_strcmp(interface, "wl_shm")) {
            wl_shm = wl_registry_bind(registry, name, &wl_shm_interface, version);
        } else if (rvvm_strcmp(interface, "wl_seat")) {
            struct wl_seat* seat      = wl_registry_bind(registry, name, &wl_seat_interface, version);
            wl_seat_data_t* seat_data = safe_new_obj(wl_seat_data_t);
            seat_data->seat           = seat;
            vector_push_back(wl_seats, seat_data);
            wl_seat_add_listener(seat, &seat_listener, seat_data);
        } else if (rvvm_strcmp(interface, xdg_wm_base_interface.name) && version >= 2) {
            xdg_wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, version);
            xdg_wm_base_add_listener(xdg_wm_base, &wm_base_listener, NULL);
        } else if (rvvm_strcmp(interface, zxdg_decoration_manager_v1_interface.name)) {
            xdg_decor_mgr = wl_registry_bind(registry, name, &zxdg_decoration_manager_v1_interface, version);
        } else if (rvvm_strcmp(interface, zwp_keyboard_shortcuts_inhibit_manager_v1_interface.name)) {
            wl_kbd_inhibit_mgr
                = wl_registry_bind(registry, name, &zwp_keyboard_shortcuts_inhibit_manager_v1_interface, version);
        } else if (rvvm_strcmp(interface, zwp_pointer_constraints_v1_interface.name)) {
            wl_ptr_constraints = wl_registry_bind(registry, name, &zwp_pointer_constraints_v1_interface, version);
        } else if (rvvm_strcmp(interface, zwp_relative_pointer_manager_v1_interface.name)) {
            wl_relative_mgr = wl_registry_bind(registry, name, &zwp_relative_pointer_manager_v1_interface, version);
        }
    }
}

static void wl_registry_on_global_remove(void* data, struct wl_registry* registry, uint32_t name)
{
    UNUSED(data && registry && name);
}

static const struct wl_registry_listener registry_listener = {
    .global        = wl_registry_on_global,
    .global_remove = wl_registry_on_global_remove,
};

/*
 * Wayland libraries probing
 */

#if defined(USE_LIBS_PROBE)

#define WAYLAND_DLIB_RESOLVE(lib, sym)                                                                                 \
    do {                                                                                                               \
        sym     = dlib_resolve(lib, #sym);                                                                             \
        success = success && !!sym;                                                                                    \
    } while (0)

static bool wayland_init_libs(void)
{
    bool        success      = true;
    dlib_ctx_t* libwayland   = dlib_open("wayland-client", DLIB_NAME_PROBE);
    dlib_ctx_t* libxkbcommon = dlib_open("xkbcommon", DLIB_NAME_PROBE);

    WAYLAND_DLIB_RESOLVE(libwayland, wl_display_cancel_read);
    WAYLAND_DLIB_RESOLVE(libwayland, wl_display_connect);
    WAYLAND_DLIB_RESOLVE(libwayland, wl_display_disconnect);
    WAYLAND_DLIB_RESOLVE(libwayland, wl_display_dispatch_pending);
    WAYLAND_DLIB_RESOLVE(libwayland, wl_display_flush);
    WAYLAND_DLIB_RESOLVE(libwayland, wl_display_get_fd);
    WAYLAND_DLIB_RESOLVE(libwayland, wl_display_prepare_read);
    WAYLAND_DLIB_RESOLVE(libwayland, wl_display_read_events);
    WAYLAND_DLIB_RESOLVE(libwayland, wl_display_roundtrip);
    WAYLAND_DLIB_RESOLVE(libwayland, wl_proxy_get_version);
    WAYLAND_DLIB_RESOLVE(libwayland, wl_proxy_marshal_flags);
    WAYLAND_DLIB_RESOLVE(libwayland, wl_proxy_add_listener);
    WAYLAND_DLIB_RESOLVE(libwayland, wl_proxy_set_user_data);
    WAYLAND_DLIB_RESOLVE(libwayland, wl_proxy_get_user_data);
    WAYLAND_DLIB_RESOLVE(libwayland, wl_proxy_destroy);

    WAYLAND_DLIB_RESOLVE(libxkbcommon, xkb_context_new);
    WAYLAND_DLIB_RESOLVE(libxkbcommon, xkb_context_unref);
    WAYLAND_DLIB_RESOLVE(libxkbcommon, xkb_keymap_new_from_string);
    WAYLAND_DLIB_RESOLVE(libxkbcommon, xkb_keymap_unref);
    WAYLAND_DLIB_RESOLVE(libxkbcommon, xkb_state_new);
    WAYLAND_DLIB_RESOLVE(libxkbcommon, xkb_state_unref);
    WAYLAND_DLIB_RESOLVE(libxkbcommon, xkb_state_key_get_one_sym);

    dlib_close(libwayland);
    dlib_close(libxkbcommon);
    return success;
}

#endif

/*
 * Wayland handling code (Finally)
 */

// Wayland event thread worker
static void* wl_event_worker(void* arg)
{
    // This is a thread-safe variant of wl_display_dispatch()
    // See https://wayland.freedesktop.org/docs/html/apb.html
    int wl_fd = wl_display_get_fd(wl_display);
    while (atomic_load_uint32(&wl_windows)) {
        fd_set fds = ZERO_INIT;
        if (wl_fd < FD_SETSIZE) {
            FD_SET(wl_fd, &fds);
        } else {
            rvvm_warn("Wayland FD is above FD_SETSIZE!");
        }
        // Dispatch & Flush under Wayland event lock
        while (wl_display_prepare_read(wl_display)) {
            wl_display_dispatch_pending(wl_display);
        }
        wl_display_flush(wl_display);
        if (select(wl_fd + 1, &fds, NULL, NULL, NULL) > 0) {
            wl_display_read_events(wl_display);
        } else {
            wl_display_cancel_read(wl_display);
        }
    }
    return arg;
}

// Perform global Wayland initialization
// NOTE: Must be called when wl_windows != 0
static bool wayland_global_init(void)
{
#if defined(USE_LIBS_PROBE)
    static bool wl_avail = false;
    DO_ONCE_SCOPED {
        wl_avail = wayland_init_libs();
        if (!wl_avail) {
            rvvm_info("Failed to load libwayland-client or libxkbcommon");
        }
    }
    if (!wl_avail) {
        return false;
    }
#endif

    // Create XKB keymap context
    xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!xkb_context) {
        rvvm_error("xkb_context_new() failed");
        return false;
    }

    // Connect to Wayland display
    wl_display = wl_display_connect(NULL);
    if (!wl_display) {
        rvvm_info("Failed to connect to Wayland display");
        return false;
    }

    // Listen for Wayland registry
    wl_registry = wl_display_get_registry(wl_display);
    if (wl_registry) {
        wl_registry_add_listener(wl_registry, &registry_listener, NULL);
    } else {
        rvvm_error("Failed to get Wayland registry");
        return false;
    }

    // Obtain required globals
    wl_display_roundtrip(wl_display);
    if (!wl_compositor) {
        rvvm_error("Wayland wl_compositor is never advertised");
        return false;
    }
    if (!wl_shm) {
        rvvm_error("Wayland wl_shm is never advertised");
        return false;
    }
    if (!xdg_wm_base) {
        rvvm_error("Wayland xdg_wm_base >= 2 is never advertised");
        return false;
    }

    // Launch the event thread
    wl_thread = thread_create(wl_event_worker, NULL);

    return !!wl_thread;
}

// Perform global Wayland deinitialization
// NOTE: Must be called when wl_windows == 0
static void wayland_global_free(void)
{
    // Shut down the event thread
    if (wl_display && wl_thread) {
        struct wl_callback* sync = wl_display_sync(wl_display);
        wl_display_flush(wl_display);
        if (sync) {
            wl_callback_destroy(sync);
        }
        thread_join(wl_thread);
        wl_thread = NULL;
    }

    // Clean up per-seat handles & data
    vector_foreach (wl_seats, i) {
        wl_seat_data_t* seat_data = vector_at(wl_seats, i);
        if (seat_data->seat) {
            wl_seat_release(seat_data->seat);
        }
        if (seat_data->keyboard) {
            wl_keyboard_release(seat_data->keyboard);
        }
        if (seat_data->pointer) {
            wl_pointer_release(seat_data->pointer);
        }
        if (seat_data->relative) {
            zwp_relative_pointer_v1_destroy(seat_data->relative);
        }
        if (seat_data->kb_data.xkb_state) {
            xkb_state_unref(seat_data->kb_data.xkb_state);
        }
        free(seat_data);
    }
    vector_free(wl_seats);

    // Clean up all Wayland globals (Registry, compositor, etc)
    if (wl_registry) {
        wl_registry_destroy(wl_registry);
        wl_registry = NULL;
    }
    if (wl_compositor) {
        wl_compositor_destroy(wl_compositor);
        wl_compositor = NULL;
    }
    if (wl_shm) {
        wl_shm_destroy(wl_shm);
        wl_shm = NULL;
    }
    if (xdg_wm_base) {
        xdg_wm_base_destroy(xdg_wm_base);
        xdg_wm_base = NULL;
    }
    if (xdg_decor_mgr) {
        zxdg_decoration_manager_v1_destroy(xdg_decor_mgr);
        xdg_decor_mgr = NULL;
    }
    if (wl_relative_mgr) {
        zwp_relative_pointer_manager_v1_destroy(wl_relative_mgr);
        wl_relative_mgr = NULL;
    }
    if (wl_ptr_constraints) {
        zwp_pointer_constraints_v1_destroy(wl_ptr_constraints);
        wl_ptr_constraints = NULL;
    }
    if (wl_kbd_inhibit_mgr) {
        zwp_keyboard_shortcuts_inhibit_manager_v1_destroy(wl_kbd_inhibit_mgr);
        wl_kbd_inhibit_mgr = NULL;
    }

    // Disconnect from Wayland display
    // NOTE: MUST come last in deinitialization order
    if (wl_display) {
        wl_display_disconnect(wl_display);
        wl_display = NULL;
    }

    // Free XKB context
    if (xkb_context) {
        xkb_context_unref(xkb_context);
        xkb_context = NULL;
    }
}

static void wayland_window_free(gui_window_t* win)
{
    wl_window_t* wl   = gui_backend_get_data(win);
    bool         last = atomic_sub_uint32(&wl_windows, 1) == 1;
    // Destroy frame buffer
    if (wl->buffer) {
        wl_buffer_destroy(wl->buffer);
    }
    // Destroy shared memory pool
    if (wl->shm_pool) {
        wl_shm_pool_destroy(wl->shm_pool);
    }
    // Unmap VRAM region
    if (gui_backend_get_vram(win)) {
        munmap(gui_backend_get_vram(win), gui_backend_get_vram_size(win));
    }
    // Clean up Wayland objects
    if (wl->xdg_decoration) {
        zxdg_toplevel_decoration_v1_destroy(wl->xdg_decoration);
    }
    if (wl->xdg_toplevel) {
        xdg_toplevel_destroy(wl->xdg_toplevel);
    }
    if (wl->xdg_surface) {
        xdg_surface_destroy(wl->xdg_surface);
    }
    if (wl->surface) {
        wl_surface_destroy(wl->surface);
    }
    // If this was a last window, perform global deinit
    if (last) {
        wayland_global_free();
    }
    safe_free(wl);
}

static int32_t wayland_window_format(const rvvm_fb_t* fb)
{
    switch (rvvm_fb_format(fb)) {
        case RVVM_RGB_XRGB1555: // NOTE: Doesn't actually work on KWin :c
            return WL_SHM_FORMAT_XRGB1555;
        case RVVM_RGB_RGB565: // NOTE: Doesn't actually work on KWin :c
            return WL_SHM_FORMAT_RGB565;
        case RVVM_RGB_RGB888:
            return WL_SHM_FORMAT_RGB888;
        case RVVM_RGB_XRGB8888:
            return WL_SHM_FORMAT_XRGB8888;
        case RVVM_RGB_BGR888:
            return WL_SHM_FORMAT_BGR888;
        case RVVM_RGB_XBGR8888: // NOTE: Doesn't actually work on KWin :c
            return WL_SHM_FORMAT_XBGR8888;
        case RVVM_RGB_BGRX8888: // NOTE: Doesn't actually work on KWin :c
            return WL_SHM_FORMAT_BGRX8888;
        case RVVM_RGB_RGBX8888: // NOTE: Doesn't actually work on KWin :c
            return WL_SHM_FORMAT_RGBX8888;
        case RVVM_RGB_XRGB2101010:
            return WL_SHM_FORMAT_XRGB2101010;
        case RVVM_RGB_XBGR2101010:
            return WL_SHM_FORMAT_XBGR2101010;
    }
    return -1;
}

// Check and resize wl_surface / recreate wl_buffer
static bool wayland_window_update_scanout(gui_window_t* win, const rvvm_fb_t* fb)
{
    wl_window_t* wl = gui_backend_get_data(win);
    if (rvvm_fb_buffer(fb)) {
        if (!rvvm_fb_same_res(fb, &wl->fb)) {
            // Resize window
            xdg_toplevel_set_min_size(wl->xdg_toplevel, rvvm_fb_width(fb), rvvm_fb_height(fb));
            xdg_toplevel_set_max_size(wl->xdg_toplevel, rvvm_fb_width(fb), rvvm_fb_height(fb));
        }
        if (!rvvm_fb_same_layout(fb, &wl->fb) || rvvm_fb_buffer(fb) != rvvm_fb_buffer(&wl->fb)) {
            // Recreate wl_buffer
            size_t  offset = ((size_t)rvvm_fb_buffer(fb)) - ((size_t)gui_backend_get_vram(win));
            int32_t format = wayland_window_format(fb);
            if (format < 0) {
                return false;
            }
            struct wl_buffer* new_buffer = wl_shm_pool_create_buffer( //
                wl->shm_pool, offset, rvvm_fb_width(fb), rvvm_fb_height(fb), rvvm_fb_stride(fb), format);
            if (new_buffer) {
                if (wl->buffer) {
                    wl_buffer_destroy(wl->buffer);
                }
                wl->buffer = new_buffer;
            } else {
                return false;
            }
        }
        // Store the scanout context for later checks
        wl->fb = *fb;
    }
    return true;
}

static void wayland_window_draw(gui_window_t* win, const rvvm_fb_t* fb, uint32_t x, uint32_t y)
{
    UNUSED(x && y);
    scoped_spin_lock (&wl_lock) {
        wl_window_t* wl = gui_backend_get_data(win);
        wayland_window_update_scanout(win, fb);
        if (wl->surface && wl->buffer && wl->xdg_configured) {
            wl_surface_attach(wl->surface, wl->buffer, 0, 0);
            wl_surface_damage_buffer(wl->surface, 0, 0, wl->fb.width, wl->fb.height);
            wl_surface_commit(wl->surface);
        }
    }
    wl_display_flush(wl_display);
}

static void wayland_window_set_title(gui_window_t* win, const char* title)
{
    scoped_spin_lock (&wl_lock) {
        wl_window_t* wl = gui_backend_get_data(win);
        if (wl->xdg_toplevel) {
            xdg_toplevel_set_title(wl->xdg_toplevel, title);
        }
    }
    wl_display_flush(wl_display);
}

static void wayland_window_grab_input(gui_window_t* win, bool grab)
{
    scoped_spin_lock (&wl_lock) {
        wl_window_t* wl = gui_backend_get_data(win);
        if (grab) {
            vector_foreach (wl_seats, i) {
                wl_seat_data_t* seat_data = vector_at(wl_seats, i);
                if (!wl->ptr_lock && wl_ptr_constraints && seat_data->pointer) {
                    wl->ptr_lock = zwp_pointer_constraints_v1_lock_pointer(        //
                        wl_ptr_constraints, wl->surface, seat_data->pointer, NULL, //
                        ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
                }
                if (!wl->kbd_inhibit && wl_kbd_inhibit_mgr && seat_data->keyboard) {
                    wl->kbd_inhibit = zwp_keyboard_shortcuts_inhibit_manager_v1_inhibit_shortcuts( //
                        wl_kbd_inhibit_mgr, wl->surface, seat_data->seat);
                }
            }
        } else {
            if (wl->ptr_lock) {
                zwp_locked_pointer_v1_destroy(wl->ptr_lock);
                wl->ptr_lock = NULL;
            }
            if (wl->kbd_inhibit) {
                zwp_keyboard_shortcuts_inhibitor_v1_destroy(wl->kbd_inhibit);
                wl->kbd_inhibit = NULL;
            }
        }
    }
    wl_display_flush(wl_display);
}

static void wayland_window_set_fullscreen(gui_window_t* win, bool fullscreen)
{
    scoped_spin_lock (&wl_lock) {
        wl_window_t* wl = gui_backend_get_data(win);
        if (wl->xdg_toplevel) {
            if (fullscreen) {
                xdg_toplevel_set_fullscreen(wl->xdg_toplevel, NULL);
            } else {
                xdg_toplevel_unset_fullscreen(wl->xdg_toplevel);
            }
        }
    }
    wl_display_flush(wl_display);
}

static const gui_backend_cb_t wayland_window_cb = {
    .free           = wayland_window_free,
    .draw           = wayland_window_draw,
    .set_title      = wayland_window_set_title,
    .grab_input     = wayland_window_grab_input,
    .set_fullscreen = wayland_window_set_fullscreen,
};

static bool wayland_is_x11_fallback_allowed(void)
{
    if (rvvm_has_arg("ignore_nodecor")) {
        return false;
    }

    const char* current_desktop = getenv("XDG_CURRENT_DESKTOP");

    // Certain compositors like niri don't provide full server-side decorations even with an X11-fallback trick
    if (current_desktop && rvvm_strcmp(current_desktop, "niri")) {
        return false;
    }

    return true;
}

bool wayland_window_init(gui_window_t* win)
{
    // Register Wayland backend
    wl_window_t* wl = safe_new_obj(wl_window_t);

    gui_backend_register(win, &wayland_window_cb);
    gui_backend_set_data(win, wl);

    if (!atomic_add_uint32(&wl_windows, 1) && !wayland_global_init()) {
        // Wayland global init failed
        wayland_global_free();
        return false;
    }

    // Create anonymous VRAM FD
    size_t vram_size = gui_backend_get_vram_size(win);
    int    vram_fd   = vma_anon_memfd(vram_size);
    if (vram_fd < 0) {
        rvvm_error("Failed to create vram_fd");
        return false;
    }

    // Create Wayland shared memory pool backed by vram_fd
    wl->shm_pool = wl_shm_create_pool(wl_shm, vram_fd, vram_size);
    if (!wl->shm_pool) {
        close(vram_fd);
        rvvm_error("Failed to create wl_shm_pool");
        return false;
    }

    // Pass VRAM region to GUI API user
    void* vram = mmap(NULL, vram_size, PROT_READ | PROT_WRITE, MAP_SHARED, vram_fd, 0);
    close(vram_fd);
    if (vram != MAP_FAILED) {
        gui_backend_set_vram(win, vram, vram_size);
    } else {
        rvvm_error("Failed to mmap() vram_fd");
        return false;
    }

    // Create surface
    wl->surface = wl_compositor_create_surface(wl_compositor);
    if (!wl->surface) {
        rvvm_error("Failed to create wl_surface");
        return false;
    }

    // Store gui_window_t* handle in wl_surface to retrieve it from event listeners
    wl_surface_set_user_data(wl->surface, win);

    // Obtain XDG surface
    wl->xdg_surface = xdg_wm_base_get_xdg_surface(xdg_wm_base, wl->surface);
    if (!wl->xdg_surface) {
        rvvm_error("Failed to get xdg_surface");
        return false;
    }

    // Obtain XDG toplevel
    wl->xdg_toplevel = xdg_surface_get_toplevel(wl->xdg_surface);
    if (!wl->xdg_toplevel) {
        rvvm_error("Failed to get xdg_toplevel");
        return false;
    }

    // Create XDG window decorations
    if (xdg_decor_mgr) {
        wl->xdg_decoration = zxdg_decoration_manager_v1_get_toplevel_decoration( //
            xdg_decor_mgr, wl->xdg_toplevel);
        zxdg_toplevel_decoration_v1_set_mode( //
            wl->xdg_decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    } else if (wayland_is_x11_fallback_allowed()) {
        // Probably running on Gnome or something, report an info message and fallback to X11
        rvvm_info("Your Wayland compositor doesn't support XDG decorations, ew");
        return false;
    }

    // Register XDG surface listener to handle configure events
    xdg_surface_add_listener(wl->xdg_surface, &xdg_surface_listener, win);

    // Register XDG toplevel listener to handle window close events
    xdg_toplevel_add_listener(wl->xdg_toplevel, &xdg_toplevel_listener, win);

    // Set app ID, otherwise window icon is missing
    xdg_toplevel_set_app_id(wl->xdg_toplevel, "rvvm");

    // Commit surface, otherwise nothing appears
    wl_surface_commit(wl->surface);

    return true;
}

#else

bool wayland_window_init(gui_window_t* win)
{
    UNUSED(win);
    return false;
}

#endif

POP_OPTIMIZATION_SIZE
