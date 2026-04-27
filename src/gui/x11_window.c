/*
x11_window.c - X11 GUI Window
Copyright (C) 2021  LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

// Must be included before <sys/ipc.h>
#include "feature_test.h"

#include "compiler.h"
#include "gui_window.h"

PUSH_OPTIMIZATION_SIZE

// Enable X11 on POSIX 1003.1b-1993 and higher
#if !defined(HOST_TARGET_POSIX) || HOST_TARGET_POSIX < 199309L
#undef USE_X11
#endif

// Enable XShm by default unless built with USE_NO_XSHM
#if defined(USE_X11) && !defined(USE_NO_XSHM)
#define USE_XSHM 1
#endif

#if defined(USE_X11) && !(CHECK_INCLUDE(X11/Xlib.h, 1) && CHECK_INCLUDE(X11/Xutil.h, 1) && CHECK_INCLUDE(X11/keysym.h, 1))
// No X11 headers found
#undef USE_X11
#pragma message("Disabling X11 support as <X11/Xlib.h> is unavailable")
#endif

#if defined(USE_XSHM) && (defined(HOST_TARGET_ANDROID) || defined(HOST_TARGET_SERENITY))
// There's no SysV SHM on Android, Serenity
#pragma message("Disabling XShm support on system without SysV SHM")
#undef USE_XSHM
#elif defined(USE_XSHM) && !CHECK_INCLUDE(X11/extensions/XShm.h, 1)
// No XShm headers found
#pragma message("Disabling XShm support as <X11/extensions/XShm.h> is unavailable")
#undef USE_XSHM
#elif defined(USE_XSHM) && !(CHECK_INCLUDE(sys/ipc.h, 1) && CHECK_INCLUDE(sys/shm.h, 1))
// No SysV SHM headers found
#pragma message("Disabling XShm support as <X11/extensions/XShm.h> is unavailable")
#undef USE_XSHM
#endif

#if defined(USE_X11)

#include "spinlock.h"
#include "threading.h"
#include "utils.h"
#include "vector.h"
#include "vma_ops.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <setjmp.h>
#include <sys/select.h>

#if defined(USE_LIBS_PROBE)

// Resolve symbols at runtime
#include "dlib.h"

#define X11_DLIB_SYM(sym) static __typeof__(sym)* MACRO_CONCAT(sym, _dlib) = NULL;

X11_DLIB_SYM(XCloseDisplay)
X11_DLIB_SYM(XCreateBitmapFromData)
X11_DLIB_SYM(XCreateFontCursor)
X11_DLIB_SYM(XCreateGC)
X11_DLIB_SYM(XCreatePixmapCursor)
X11_DLIB_SYM(XCreateWindow)
X11_DLIB_SYM(XDefineCursor)
X11_DLIB_SYM(XDestroyWindow)
X11_DLIB_SYM(XDisplayKeycodes)
X11_DLIB_SYM(XFillRectangle)
X11_DLIB_SYM(XFlush)
X11_DLIB_SYM(XFree)
X11_DLIB_SYM(XFreeCursor)
X11_DLIB_SYM(XFreeGC)
X11_DLIB_SYM(XFreePixmap)
X11_DLIB_SYM(XGetKeyboardMapping)
X11_DLIB_SYM(XGrabKeyboard)
X11_DLIB_SYM(XGrabPointer)
X11_DLIB_SYM(XInternAtom)
X11_DLIB_SYM(XMapWindow)
X11_DLIB_SYM(XMoveWindow)
X11_DLIB_SYM(XNextEvent)
X11_DLIB_SYM(XOpenDisplay)
X11_DLIB_SYM(XPeekEvent)
X11_DLIB_SYM(XPending)
X11_DLIB_SYM(XPutImage)
X11_DLIB_SYM(XQueryPointer)
X11_DLIB_SYM(XResizeWindow)
X11_DLIB_SYM(XSetErrorHandler)
X11_DLIB_SYM(XSetIOErrorHandler)
X11_DLIB_SYM(XSetWMNormalHints)
X11_DLIB_SYM(XSetWMProtocols)
X11_DLIB_SYM(XStoreName)
X11_DLIB_SYM(XSync)
X11_DLIB_SYM(XUngrabKeyboard)
X11_DLIB_SYM(XUngrabPointer)
X11_DLIB_SYM(XWarpPointer)

#define XCloseDisplay         XCloseDisplay_dlib
#define XCreateBitmapFromData XCreateBitmapFromData_dlib
#define XCreateFontCursor     XCreateFontCursor_dlib
#define XCreateGC             XCreateGC_dlib
#define XCreatePixmapCursor   XCreatePixmapCursor_dlib
#define XCreateWindow         XCreateWindow_dlib
#define XDefineCursor         XDefineCursor_dlib
#define XDestroyWindow        XDestroyWindow_dlib
#define XDisplayKeycodes      XDisplayKeycodes_dlib
#define XFillRectangle        XFillRectangle_dlib
#define XFlush                XFlush_dlib
#define XFree                 XFree_dlib
#define XFreeCursor           XFreeCursor_dlib
#define XFreeGC               XFreeGC_dlib
#define XFreePixmap           XFreePixmap_dlib
#define XGetKeyboardMapping   XGetKeyboardMapping_dlib
#define XGrabKeyboard         XGrabKeyboard_dlib
#define XGrabPointer          XGrabPointer_dlib
#define XInternAtom           XInternAtom_dlib
#define XMapWindow            XMapWindow_dlib
#define XMoveWindow           XMoveWindow_dlib
#define XNextEvent            XNextEvent_dlib
#define XOpenDisplay          XOpenDisplay_dlib
#define XPeekEvent            XPeekEvent_dlib
#define XPending              XPending_dlib
#define XPutImage             XPutImage_dlib
#define XQueryPointer         XQueryPointer_dlib
#define XResizeWindow         XResizeWindow_dlib
#define XSetErrorHandler      XSetErrorHandler_dlib
#define XSetIOErrorHandler    XSetIOErrorHandler_dlib
#define XSetWMNormalHints     XSetWMNormalHints_dlib
#define XSetWMProtocols       XSetWMProtocols_dlib
#define XStoreName            XStoreName_dlib
#define XSync                 XSync_dlib
#define XUngrabKeyboard       XUngrabKeyboard_dlib
#define XUngrabPointer        XUngrabPointer_dlib
#define XWarpPointer          XWarpPointer_dlib

#endif

#if defined(USE_XSHM)

#include <X11/extensions/XShm.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#if defined(USE_LIBS_PROBE)
X11_DLIB_SYM(XShmAttach)
X11_DLIB_SYM(XShmDetach)
X11_DLIB_SYM(XShmPutImage)
X11_DLIB_SYM(XShmQueryExtension)

#define XShmAttach         XShmAttach_dlib
#define XShmDetach         XShmDetach_dlib
#define XShmPutImage       XShmPutImage_dlib
#define XShmQueryExtension XShmQueryExtension_dlib
#endif

#endif

/*
 * X11 Window state
 */

typedef struct {
    Window window;
    GC     gc;

#if defined(USE_XSHM)
    // XShm segment
    XShmSegmentInfo xshm;
#endif

    // Current window dimensions
    uint32_t width;
    uint32_t height;

    // Current window position
    uint32_t pos_x;
    uint32_t pos_y;

    // Original pointer grab position
    Window  grab_root;
    int32_t grab_ptr_x;
    int32_t grab_ptr_y;

    // Input grabbed
    bool grab;
} x11_window_t;

/*
 * X11 Global data
 */

// Global lock, must be used around any X11 call or structure
static spinlock_t x11_lock = ZERO_INIT;

// X11 Display connection, must be accessed under x11_lock
static Display* x11_display = NULL;

// X11 Windows list, must be accessed under x11_lock
static vector_t(gui_window_t*) x11_windows = ZERO_INIT;

// X11 error unwinding jmp_buf, must be accessed under x11_lock
static jmp_buf x11_unwind = ZERO_INIT;

// X11 last error code
static int x11_error_code = 0;

// X11 event thread
static thread_ctx_t* x11_thread = NULL;

// X11 keycode->keysym keycodemap, must be accessed under x11_lock
static KeySym* x11_keycodemap          = NULL;
static int     x11_min_keycode         = 0;
static int     x11_max_keycode         = 0;
static int     x11_keysyms_per_keycode = 0;

// WM_DELETE_WINDOW atom for handling window close
static Atom x11_wm_delete = 0;

/*
 * X11 KeySym to HID keycode mapping
 */

static hid_key_t x11_keysym_to_hid(KeySym keysym)
{
    // XK_ definitions are huge numbers, don't use a table
    // clang-format off
    switch (keysym) {
        case XK_a:            return HID_KEY_A;
        case XK_b:            return HID_KEY_B;
        case XK_c:            return HID_KEY_C;
        case XK_d:            return HID_KEY_D;
        case XK_e:            return HID_KEY_E;
        case XK_f:            return HID_KEY_F;
        case XK_g:            return HID_KEY_G;
        case XK_h:            return HID_KEY_H;
        case XK_i:            return HID_KEY_I;
        case XK_j:            return HID_KEY_J;
        case XK_k:            return HID_KEY_K;
        case XK_l:            return HID_KEY_L;
        case XK_m:            return HID_KEY_M;
        case XK_n:            return HID_KEY_N;
        case XK_o:            return HID_KEY_O;
        case XK_p:            return HID_KEY_P;
        case XK_q:            return HID_KEY_Q;
        case XK_r:            return HID_KEY_R;
        case XK_s:            return HID_KEY_S;
        case XK_t:            return HID_KEY_T;
        case XK_u:            return HID_KEY_U;
        case XK_v:            return HID_KEY_V;
        case XK_w:            return HID_KEY_W;
        case XK_x:            return HID_KEY_X;
        case XK_y:            return HID_KEY_Y;
        case XK_z:            return HID_KEY_Z;
        case XK_0:            return HID_KEY_0;
        case XK_1:            return HID_KEY_1;
        case XK_2:            return HID_KEY_2;
        case XK_3:            return HID_KEY_3;
        case XK_4:            return HID_KEY_4;
        case XK_5:            return HID_KEY_5;
        case XK_6:            return HID_KEY_6;
        case XK_7:            return HID_KEY_7;
        case XK_8:            return HID_KEY_8;
        case XK_9:            return HID_KEY_9;
        case XK_Return:       return HID_KEY_ENTER;
        case XK_Escape:       return HID_KEY_ESC;
        case XK_BackSpace:    return HID_KEY_BACKSPACE;
        case XK_Tab:          return HID_KEY_TAB;
        case XK_space:        return HID_KEY_SPACE;
        case XK_minus:        return HID_KEY_MINUS;
        case XK_equal:        return HID_KEY_EQUAL;
        case XK_bracketleft:  return HID_KEY_LEFTBRACE;
        case XK_bracketright: return HID_KEY_RIGHTBRACE;
        case XK_backslash:    return HID_KEY_BACKSLASH;
        case XK_semicolon:    return HID_KEY_SEMICOLON;
        case XK_apostrophe:   return HID_KEY_APOSTROPHE;
        case XK_grave:        return HID_KEY_GRAVE;
        case XK_comma:        return HID_KEY_COMMA;
        case XK_period:       return HID_KEY_DOT;
        case XK_slash:        return HID_KEY_SLASH;
        case XK_Caps_Lock:    return HID_KEY_CAPSLOCK;
        case XK_F1:           return HID_KEY_F1;
        case XK_F2:           return HID_KEY_F2;
        case XK_F3:           return HID_KEY_F3;
        case XK_F4:           return HID_KEY_F4;
        case XK_F5:           return HID_KEY_F5;
        case XK_F6:           return HID_KEY_F6;
        case XK_F7:           return HID_KEY_F7;
        case XK_F8:           return HID_KEY_F8;
        case XK_F9:           return HID_KEY_F9;
        case XK_F10:          return HID_KEY_F10;
        case XK_F11:          return HID_KEY_F11;
        case XK_F12:          return HID_KEY_F12;
        case XK_Print:        return HID_KEY_SYSRQ;
        case XK_Scroll_Lock:  return HID_KEY_SCROLLLOCK;
        case XK_Pause:        return HID_KEY_PAUSE;
        case XK_Insert:       return HID_KEY_INSERT;
        case XK_Home:         return HID_KEY_HOME;
        case XK_Prior:        return HID_KEY_PAGEUP;
        case XK_Delete:       return HID_KEY_DELETE;
        case XK_End:          return HID_KEY_END;
        case XK_Next:         return HID_KEY_PAGEDOWN;
        case XK_Right:        return HID_KEY_RIGHT;
        case XK_Left:         return HID_KEY_LEFT;
        case XK_Down:         return HID_KEY_DOWN;
        case XK_Up:           return HID_KEY_UP;
        case XK_Num_Lock:     return HID_KEY_NUMLOCK;
        case XK_KP_Divide:    return HID_KEY_KPSLASH;
        case XK_KP_Multiply:  return HID_KEY_KPASTERISK;
        case XK_KP_Subtract:  return HID_KEY_KPMINUS;
        case XK_KP_Add:       return HID_KEY_KPPLUS;
        case XK_KP_Enter:     return HID_KEY_KPENTER;
        case XK_KP_End:       return HID_KEY_KP1;
        case XK_KP_Down:      return HID_KEY_KP2;
        case XK_KP_Page_Down: return HID_KEY_KP3;
        case XK_KP_Left:      return HID_KEY_KP4;
        case XK_KP_Begin:     return HID_KEY_KP5;
        case XK_KP_Right:     return HID_KEY_KP6;
        case XK_KP_Home:      return HID_KEY_KP7;
        case XK_KP_Up:        return HID_KEY_KP8;
        case XK_KP_Page_Up:   return HID_KEY_KP9;
        case XK_KP_Insert:    return HID_KEY_KP0;
        case XK_KP_Delete:    return HID_KEY_KPDOT;
        case XK_less:         return HID_KEY_102ND;
        case XK_Multi_key:    return HID_KEY_COMPOSE;
        case XK_KP_Equal:     return HID_KEY_KPEQUAL;
        case XK_KP_Separator: return HID_KEY_KPCOMMA;
        case 0x04db:          return HID_KEY_RO;
        case 0xff27:          return HID_KEY_KATAKANAHIRAGANA;
        case XK_yen:          return HID_KEY_YEN;
        case 0xff23:          return HID_KEY_HENKAN;
        case 0xff22:          return HID_KEY_MUHENKAN;
        case 0x04a4:          return HID_KEY_KPJPCOMMA;
        case 0xff31:          return HID_KEY_HANGEUL;
        case 0xff34:          return HID_KEY_HANJA;
        case 0xff26:          return HID_KEY_KATAKANA;
        case 0xff25:          return HID_KEY_HIRAGANA;
        case 0xff2a:          return HID_KEY_ZENKAKUHANKAKU;
        case XK_Menu:         return HID_KEY_MENU;
        case XK_Control_L:    return HID_KEY_LEFTCTRL;
        case XK_Shift_L:      return HID_KEY_LEFTSHIFT;
        case XK_Alt_L:        return HID_KEY_LEFTALT;
        case XK_Super_L:      return HID_KEY_LEFTMETA;
        case XK_Control_R:    return HID_KEY_RIGHTCTRL;
        case XK_Shift_R:      return HID_KEY_RIGHTSHIFT;
        case XK_Alt_R:        return HID_KEY_RIGHTALT;
        case XK_Super_R:      return HID_KEY_RIGHTMETA;

        // Reported by Fn key on T480s, maps to XF86WakeUp
        case 0x1008ff2b:      return HID_KEY_NONE;
    }
    // clang-format on
    rvvm_warn("Unmapped X11 keycode %#x", (uint32_t)keysym);
    return HID_KEY_NONE;
}

/* Map an X11 keycode to a HID usage code WITHOUT going through the
 * keyboard-layout-translated keysym table. On Linux X11, keycodes are
 * Linux evdev keycodes + 8 (the X11 protocol's historical offset), so
 * subtracting 8 recovers the layout-independent physical key id. The
 * emulator wants physical positions, not "what letter the user's
 * Workman/Dvorak/AZERTY layout assigns to that physical key" — those
 * are the wrong abstraction for emulating actual hardware. The
 * x11_keycodemap-based path below is kept as a fallback for any
 * keycode outside the standard evdev range. */
extern const hid_key_t x11_evdev_to_hid[256];

static hid_key_t x11_keycode_to_hid(int keycode)
{
    int evdev = keycode - 8;
    if (evdev >= 0 && evdev < (int)(sizeof(x11_evdev_to_hid) / sizeof(x11_evdev_to_hid[0]))) {
        hid_key_t hid = x11_evdev_to_hid[evdev];
        if (hid != HID_KEY_NONE) return hid;
    }
    /* Fallback: layout-translated keysym lookup. */
    if (x11_keycodemap && keycode >= x11_min_keycode && keycode <= x11_max_keycode) {
        uint32_t entry = (keycode - x11_min_keycode) * x11_keysyms_per_keycode;
        return x11_keysym_to_hid(x11_keycodemap[entry]);
    }
    return HID_KEY_NONE;
}

/* Linux evdev keycode → HID usage. Indexed by evdev keycode (0..255). */
const hid_key_t x11_evdev_to_hid[256] = {
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

/*
 * Xlib error handling, try-catch via setjmp
 */

static int x11_io_error_handler(Display* display)
{
    if (x11_display == display) {
        rvvm_warn("Lost connection to the X11 Display!");
        x11_display = NULL;
        XCloseDisplay(display);
        longjmp(x11_unwind, 1);
    }
    return 0;
}

static int x11_error_handler(Display* display, XErrorEvent* error)
{
    if (x11_display == display) {
        x11_error_code = error->error_code;
    }
    return 0;
}

// Unwinds and returns true if X11 Display dies
// NOTE: Must be called under x11_lock
#define x11_catch_error() (!x11_display || setjmp(x11_unwind))

// Performs XSync(), gets the last X11 error code and clears it
// NOTE: Must be called under x11_lock and error unwinding
static int x11_consume_error_sync(void)
{
    XSync(x11_display, 0);
    int error_code = x11_error_code;
    x11_error_code = 0;
    return error_code;
}

/*
 * X11 event handling internals
 */

// Must be called under x11_lock
static gui_window_t* x11_find_window(Window window)
{
    vector_foreach (x11_windows, i) {
        gui_window_t* win = vector_at(x11_windows, i);
        x11_window_t* x11 = gui_backend_get_data(win);
        if (x11 && x11->window == window) {
            return win;
        }
    }
    return NULL;
}

// Must be called under x11_lock
static void x11_update_keymap(void)
{
    XDisplayKeycodes(x11_display, &x11_min_keycode, &x11_max_keycode);
    if (x11_min_keycode != x11_max_keycode) {
        KeySym* keycodemap = XGetKeyboardMapping(x11_display, x11_min_keycode,          //
                                                 x11_max_keycode - x11_min_keycode + 1, //
                                                 &x11_keysyms_per_keycode);
        if (keycodemap) {
            if (x11_keycodemap) {
                XFree(x11_keycodemap);
            }
            x11_keycodemap = keycodemap;
        }
    }
}

// Must be called under x11_lock, releases the lock before GUI callback
static void x11_handle_mouse_button(const XEvent* ev, gui_window_t* win)
{
    hid_btns_t btns = 0;
    spin_unlock(&x11_lock);
    switch (ev->xbutton.button) {
        case Button1:
            btns = HID_BTN_LEFT;
            break;
        case Button2:
            btns = HID_BTN_MIDDLE;
            break;
        case Button3:
            btns = HID_BTN_RIGHT;
            break;
        case Button4:
            if (ev->type == ButtonPress) {
                gui_backend_on_mouse_scroll(win, HID_SCROLL_UP);
            }
            return;
        case Button5:
            if (ev->type == ButtonPress) {
                gui_backend_on_mouse_scroll(win, HID_SCROLL_DOWN);
            }
            return;
    }
    if (ev->type == ButtonPress) {
        gui_backend_on_mouse_press(win, btns);
    } else {
        gui_backend_on_mouse_release(win, btns);
    }
}

// Must be called under x11_lock, releases the lock before GUI callback
static void x11_handle_mouse_motion(const XEvent* ev, gui_window_t* win)
{
    x11_window_t* x11 = gui_backend_get_data(win);
    if (x11) {
        int off_x = x11->width >> 1;
        int off_y = x11->height >> 1;
        // Calculate resulting mouse offset
        int x = ev->xmotion.x - (x11->grab ? off_x : 0);
        int y = ev->xmotion.y - (x11->grab ? off_y : 0);
        if (x11->grab && (x || y)) {
            XWarpPointer(x11_display, None, x11->window, 0, 0, 0, 0, off_x, off_y);
            XFlush(x11_display);
        }
        spin_unlock(&x11_lock);
        if (x11->grab) {
            gui_backend_on_mouse_move(win, x - (x / 3), y - (y / 3));
        } else {
            // Calculate view offset
            gui_backend_on_mouse_place(win, x, y);
        }
    } else {
        spin_unlock(&x11_lock);
    }
}

// Must be called under x11_lock, releases the lock before GUI callback
static void x11_handle_keyboard(const XEvent* ev, gui_window_t* win, size_t pending)
{
    hid_key_t key = x11_keycode_to_hid(ev->xkey.keycode);
    if (ev->type == KeyRelease && pending > 1) {
        XEvent tmp = {0};
        XPeekEvent(x11_display, &tmp);
        if (tmp.type == KeyPress              //
            && tmp.xkey.time == ev->xkey.time //
            && tmp.xkey.keycode == ev->xkey.keycode) {
            // Skip the repeated key release event
            spin_unlock(&x11_lock);
            return;
        }
    }
    spin_unlock(&x11_lock);
    if (ev->type == KeyPress) {
        gui_backend_on_key_press(win, key);
    } else {
        gui_backend_on_key_release(win, key);
    }
}

// Must be called under x11_lock, releases the lock
static void x11_handle_event(const XEvent* ev, size_t pending)
{
    gui_window_t* win = x11_find_window(ev->xany.window);
    switch (ev->type) {
        case ButtonPress:
        case ButtonRelease:
            x11_handle_mouse_button(ev, win);
            break;
        case MotionNotify:
            x11_handle_mouse_motion(ev, win);
            break;
        case KeyPress:
        case KeyRelease:
            x11_handle_keyboard(ev, win, pending);
            break;
        case MappingNotify:
            if (ev->xmapping.request == MappingKeyboard) {
                x11_update_keymap();
            }
            spin_unlock(&x11_lock);
            break;
        case ClientMessage:
            spin_unlock(&x11_lock);
            if (((Atom)ev->xclient.data.l[0]) == x11_wm_delete) {
                gui_backend_on_close(win);
            }
            break;
        case FocusOut:
            spin_unlock(&x11_lock);
            if (ev->xfocus.mode == NotifyNormal) {
                gui_backend_on_focus_lost(win);
            }
            break;
        case ConfigureNotify: {
            x11_window_t* x11 = gui_backend_get_data(win);
            if (x11) {
                x11->width  = ev->xconfigure.width;
                x11->height = ev->xconfigure.height;
                x11->pos_x  = ev->xconfigure.x;
                x11->pos_y  = ev->xconfigure.y;
            }
            spin_unlock(&x11_lock);
            gui_backend_on_resize(win, ev->xconfigure.width, ev->xconfigure.height);
            break;
        }
        default:
            spin_unlock(&x11_lock);
            break;
    }
}

// NOTE: Must be called under x11_lock, releases the lock
static void x11_pump_event(size_t pending)
{
    if (!pending || x11_catch_error()) {
        spin_unlock(&x11_lock);
    } else {
        XEvent ev = {0};
        XNextEvent(x11_display, &ev);
        x11_handle_event(&ev, pending);
    }
}

/*
 * X11 Event dispatch
 */

static bool x11_dispatch_events(void)
{
    spin_lock(&x11_lock);
    if (vector_size(x11_windows) && !x11_catch_error()) {
        size_t pending = XPending(x11_display);
        x11_pump_event(pending);
        for (; pending > 1; pending--) {
            spin_lock(&x11_lock);
            x11_pump_event(pending);
        }
        return true;
    }
    spin_unlock(&x11_lock);
    return false;
}

/*
 * X11 Event thread worker
 */

static void* x11_event_worker(void* arg)
{
    int fd = (int)(uint32_t)(size_t)arg;
    while (true) {
        fd_set fds = ZERO_INIT;
        FD_SET(fd, &fds);
        if (x11_dispatch_events()) {
            select(fd + 1, &fds, NULL, NULL, NULL);
        } else {
            break;
        }
    }
    return NULL;
}

/*
 * Dynamic probe of libX11 / libXext in runtime
 */

#if defined(USE_LIBS_PROBE)

#define X11_DLIB_RESOLVE(lib, sym)                                                                                     \
    do {                                                                                                               \
        sym     = dlib_resolve(lib, #sym);                                                                             \
        success = success && !!sym;                                                                                    \
    } while (0)

static bool x11_init_libs(void)
{
    bool success = true;

    // Probe libX11
    dlib_ctx_t* libx11 = dlib_open("X11", DLIB_NAME_PROBE);

    X11_DLIB_RESOLVE(libx11, XCloseDisplay);
    X11_DLIB_RESOLVE(libx11, XCreateBitmapFromData);
    X11_DLIB_RESOLVE(libx11, XCreateFontCursor);
    X11_DLIB_RESOLVE(libx11, XCreateGC);
    X11_DLIB_RESOLVE(libx11, XCreatePixmapCursor);
    X11_DLIB_RESOLVE(libx11, XCreateWindow);
    X11_DLIB_RESOLVE(libx11, XDefineCursor);
    X11_DLIB_RESOLVE(libx11, XDestroyWindow);
    X11_DLIB_RESOLVE(libx11, XDisplayKeycodes);
    X11_DLIB_RESOLVE(libx11, XFillRectangle);
    X11_DLIB_RESOLVE(libx11, XFlush);
    X11_DLIB_RESOLVE(libx11, XFree);
    X11_DLIB_RESOLVE(libx11, XFreeCursor);
    X11_DLIB_RESOLVE(libx11, XFreeGC);
    X11_DLIB_RESOLVE(libx11, XFreePixmap);
    X11_DLIB_RESOLVE(libx11, XGetKeyboardMapping);
    X11_DLIB_RESOLVE(libx11, XGrabKeyboard);
    X11_DLIB_RESOLVE(libx11, XGrabPointer);
    X11_DLIB_RESOLVE(libx11, XInternAtom);
    X11_DLIB_RESOLVE(libx11, XMapWindow);
    X11_DLIB_RESOLVE(libx11, XMoveWindow);
    X11_DLIB_RESOLVE(libx11, XNextEvent);
    X11_DLIB_RESOLVE(libx11, XOpenDisplay);
    X11_DLIB_RESOLVE(libx11, XPeekEvent);
    X11_DLIB_RESOLVE(libx11, XPending);
    X11_DLIB_RESOLVE(libx11, XPutImage);
    X11_DLIB_RESOLVE(libx11, XQueryPointer);
    X11_DLIB_RESOLVE(libx11, XResizeWindow);
    X11_DLIB_RESOLVE(libx11, XSetErrorHandler);
    X11_DLIB_RESOLVE(libx11, XSetIOErrorHandler);
    X11_DLIB_RESOLVE(libx11, XSetWMNormalHints);
    X11_DLIB_RESOLVE(libx11, XSetWMProtocols);
    X11_DLIB_RESOLVE(libx11, XStoreName);
    X11_DLIB_RESOLVE(libx11, XSync);
    X11_DLIB_RESOLVE(libx11, XUngrabKeyboard);
    X11_DLIB_RESOLVE(libx11, XUngrabPointer);
    X11_DLIB_RESOLVE(libx11, XWarpPointer);

    dlib_close(libx11);

#if defined(USE_XSHM)
    dlib_ctx_t* libxext = dlib_open("Xext", DLIB_NAME_PROBE);

    X11_DLIB_RESOLVE(libxext, XShmAttach);
    X11_DLIB_RESOLVE(libxext, XShmDetach);
    X11_DLIB_RESOLVE(libxext, XShmPutImage);
    X11_DLIB_RESOLVE(libxext, XShmQueryExtension);

    dlib_close(libxext);
#endif

    return success;
}

#endif

/*
 * X11 Global initialization / deinitialization
 */

// Perform global X11 initialization (Open display, run event thread) if needed
// NOTE: Must be called under x11_lock when vector_size(x11_windows) == 1
static bool x11_global_init(void)
{
    if (vector_size(x11_windows) == 1) {
#if defined(USE_LIBS_PROBE)
        static bool x11_avail = false;
        DO_ONCE_SCOPED {
            x11_avail = x11_init_libs();
            if (!x11_avail) {
                rvvm_error("Failed to load libX11 or libXext, check your installation");
            }
        }
        if (!x11_avail) {
            return false;
        }
#endif
        XSetIOErrorHandler(x11_io_error_handler);
        XSetErrorHandler(x11_error_handler);
        x11_display = XOpenDisplay(NULL);
        if (!x11_display) {
            rvvm_error("Failed to connect to X11 Display");
            return false;
        }
        x11_update_keymap();
        x11_wm_delete = XInternAtom(x11_display, "WM_DELETE_WINDOW", False);
        int error     = x11_catch_error();
        if (error) {
            rvvm_error("Failed to connect to X11 Display: Error code %d", error);
            return false;
        }
        x11_thread = thread_create(x11_event_worker, (void*)(size_t)ConnectionNumber(x11_display));
    }
    return !!x11_thread;
}

// Perform global X11 deinitialization if needed
// NOTE: Must be called under x11_lock when vector_size(x11_windows) == 0
static void x11_global_free(void)
{
    if (!vector_size(x11_windows)) {
        if (!x11_catch_error()) {
            XFlush(x11_display);
        }
        if (x11_thread) {
            spin_unlock(&x11_lock);
            thread_join(x11_thread);
            spin_lock(&x11_lock);
            x11_thread = NULL;
        }
        if (x11_display) {
            XCloseDisplay(x11_display);
            x11_display = NULL;
        }
        if (x11_keycodemap) {
            XFree(x11_keycodemap);
            x11_keycodemap = NULL;
        }
    }
}

/*
 * X11 Shared Memory / VMA creation
 * Must be called under x11_lock with unwinding
 */

#if defined(USE_XSHM)

static void x11_xshm_free(gui_window_t* win)
{
    x11_window_t* x11 = gui_backend_get_data(win);
    if (x11->xshm.shmaddr) {
        if (gui_backend_get_vram(win) == x11->xshm.shmaddr) {
            if (x11_display) {
                XShmDetach(x11_display, &x11->xshm);
            }
        }
        shmdt(x11->xshm.shmaddr);
        x11->xshm.shmaddr = NULL;
    }
    if (x11->xshm.shmid > 0) {
        shmctl(x11->xshm.shmid, IPC_RMID, NULL);
        x11->xshm.shmid = 0;
    }
}

static void* x11_xshm_init_internal(gui_window_t* win, size_t xshm_size)
{
    x11_window_t* x11 = gui_backend_get_data(win);
    if (!XShmQueryExtension(x11_display)) {
        rvvm_info("XShm extension not supported");
        return NULL;
    }
    x11->xshm.shmid = shmget(IPC_PRIVATE, xshm_size, IPC_CREAT | 0777);
    if (x11->xshm.shmid <= 0) {
        rvvm_error("Failed to shmget() an XShm segment");
        return NULL;
    }
    x11->xshm.shmaddr = shmat(x11->xshm.shmid, NULL, 0);
    if (x11->xshm.shmaddr == (void*)(size_t)(-1)) {
        x11->xshm.shmaddr = NULL;
    }
    if (x11->xshm.shmaddr == (void*)(size_t)(-1) || x11->xshm.shmaddr == NULL) {
        rvvm_error("Failed to shmget() an XShm segment!");
        return NULL;
    }
    if (!XShmAttach(x11_display, &x11->xshm)) {
        rvvm_error("Failed to XShmAttach()");
        return NULL;
    }
    // Process errors, if any
    int error = x11_consume_error_sync();
    if (error) {
        rvvm_error("Failed to XShmAttach(): Error code %d", error);
        return NULL;
    }
    return x11->xshm.shmaddr;
}

static void* x11_xshm_init(gui_window_t* win, size_t xshm_size)
{
    void* ret = x11_xshm_init_internal(win, xshm_size);
    if (!ret) {
        x11_xshm_free(win);
    }
    return ret;
}

#endif

static bool x11_vram_init(gui_window_t* win)
{
    size_t vram_size = gui_backend_get_vram_size(win);
    void*  vram      = NULL;
#if defined(USE_XSHM)
    if (!rvvm_has_arg("no_xshm")) {
        vram = x11_xshm_init(win, vram_size);
    }
#endif
    if (!vram) {
        vram = vma_alloc(NULL, vram_size, VMA_RDWR);
    }
    if (vram) {
        gui_backend_set_vram(win, vram, vram_size);
        return true;
    }
    return false;
}

static void x11_vram_free(gui_window_t* win)
{
#if defined(USE_XSHM)
    x11_xshm_free(win);
#endif
    vma_free(gui_backend_get_vram(win), gui_backend_get_vram_size(win));
}

/*
 * X11 Window handling internals
 * Must be called under x11_lock with unwinding
 */

static void x11_window_free_internal(gui_window_t* win)
{
    x11_window_t* x11 = gui_backend_get_data(win);
    // Remove window from x11_windows
    vector_foreach_back (x11_windows, i) {
        if (vector_at(x11_windows, i) == win) {
            vector_erase(x11_windows, i);
            break;
        }
    }
    // Free graphics context
    if (x11->gc) {
        XFreeGC(x11_display, x11->gc);
        x11->gc = NULL;
    }
    // Destroy window
    if (x11->window) {
        XDestroyWindow(x11_display, x11->window);
    }
    x11_vram_free(win);
}

static void x11_window_draw_internal(gui_window_t* win, const rvvm_fb_t* fb, uint32_t x, uint32_t y)
{
    x11_window_t* x11 = gui_backend_get_data(win);
    // Prepare image scanout
    XImage image = {
        .width          = rvvm_fb_stride(fb) / 4,
        .height         = rvvm_fb_height(fb),
        .format         = ZPixmap,
        .data           = rvvm_fb_buffer(fb),
        .bitmap_unit    = 32,
        .bitmap_pad     = 32,
        .depth          = 24,
        .bytes_per_line = rvvm_fb_stride(fb),
        .bits_per_pixel = 32,
        .red_mask       = 0xFF0000U,
        .green_mask     = 0xFF00U,
        .blue_mask      = 0xFFU,
    };
    uint32_t width  = rvvm_fb_width(fb);
    uint32_t height = rvvm_fb_height(fb);
    if (x || y || width < x11->width || height < x11->height) {
        // Clear the area around the view
        uint32_t edge_x = x + width;
        uint32_t edge_y = y + height;
        XFillRectangle(x11_display, x11->window, x11->gc, 0, 0, x, x11->height);
        XFillRectangle(x11_display, x11->window, x11->gc, 0, 0, x11->width, y);
        XFillRectangle(x11_display, x11->window, x11->gc, edge_x, 0, x11->width - edge_x, x11->height);
        XFillRectangle(x11_display, x11->window, x11->gc, 0, edge_y, x11->width, x11->height - edge_y);
    }
#if defined(USE_XSHM)
    if (((size_t)rvvm_fb_buffer(fb)) >= ((size_t)x11->xshm.shmaddr)
        && (((size_t)rvvm_fb_buffer(fb)) + rvvm_fb_size(fb))
               <= (((size_t)x11->xshm.shmaddr) + gui_backend_get_vram_size(win))) {
        // Update XShm image
        image.obdata = (void*)&x11->xshm;
        XShmPutImage(x11_display, x11->window, x11->gc, &image, //
                     0, 0, x, y, image.width, image.height, 0);
        XFlush(x11_display);
        return;
    }
#endif
    // Update XImage
    XPutImage(x11_display, x11->window, x11->gc, &image, //
              0, 0, x, y, image.width, image.height);
    XFlush(x11_display);
}

static void x11_window_grab_input_internal(gui_window_t* win, bool grab)
{
    x11_window_t* x11 = gui_backend_get_data(win);
    if (x11->grab != grab) {
        x11->grab = grab;
        if (grab) {
            // Save the original cursor position
            Window   child = None;
            int32_t  win_x = 0, win_y = 0;
            uint32_t mask  = 0;
            x11->grab_root = None;
            XQueryPointer(x11_display, x11->window, &x11->grab_root, &child, //
                          &x11->grab_ptr_x, &x11->grab_ptr_y, &win_x, &win_y, &mask);

            // Grab the input
            XGrabKeyboard(x11_display, x11->window, 1, GrabModeAsync, GrabModeAsync, CurrentTime);
            XGrabPointer(x11_display, x11->window, 1,                             //
                         ButtonPressMask | ButtonReleaseMask | PointerMotionMask, //
                         GrabModeAsync, GrabModeAsync, None, None, CurrentTime);
            XWarpPointer(x11_display, None, x11->window, 0, 0, 0, 0, //
                         x11->width >> 1, x11->height >> 1);
        } else {
            // Ungrab
            XUngrabKeyboard(x11_display, CurrentTime);
            XUngrabPointer(x11_display, CurrentTime);

            // Restore the original cursor position
            if (x11->grab_root) {
                XWarpPointer(x11_display, None, x11->grab_root, 0, 0, 0, 0, //
                             x11->grab_ptr_x, x11->grab_ptr_y);
                x11->grab_root = None;
            }
        }
        XFlush(x11_display);
    }
}

static void x11_window_hide_cursor_internal(gui_window_t* win, bool hide)
{
    x11_window_t* x11 = gui_backend_get_data(win);
    if (hide) {
        XColor     color     = {0};
        const char pixels[8] = {0};
        Pixmap     pixmap    = XCreateBitmapFromData(x11_display, x11->window, pixels, 8, 8);
        Cursor     cursor    = XCreatePixmapCursor(x11_display, pixmap, pixmap, &color, &color, 0, 0);
        XDefineCursor(x11_display, x11->window, cursor);
        XFreeCursor(x11_display, cursor);
        XFreePixmap(x11_display, pixmap);
    } else {
        Cursor cursor = XCreateFontCursor(x11_display, 2);
        XDefineCursor(x11_display, x11->window, cursor);
        XFreeCursor(x11_display, cursor);
    }
    XFlush(x11_display);
}

static bool x11_window_init_internal(gui_window_t* win)
{
    x11_window_t* x11 = gui_backend_get_data(win);
    // X11 Window attributes
    XSetWindowAttributes attrs = {
        .event_mask = KeyPressMask | KeyReleaseMask         //
                    | ButtonPressMask | ButtonReleaseMask   //
                    | FocusChangeMask | StructureNotifyMask //
                    | PointerMotionMask,
    };

    x11_consume_error_sync();

    // Create X11 window
    x11->window = XCreateWindow(x11_display, DefaultRootWindow(x11_display), 0, 0,    //
                                gui_window_width(win), gui_window_height(win), 0, 24, //
                                InputOutput, NULL, CWEventMask, &attrs);

    // Create X11 Graphics Context
    x11->gc = XCreateGC(x11_display, x11->window, 0, NULL);

    // Handle window close
    XSetWMProtocols(x11_display, x11->window, &x11_wm_delete, 1);

    // Show window
    XMapWindow(x11_display, x11->window);

    // Hide cursor
    x11_window_hide_cursor_internal(win, true);

    int error = x11_consume_error_sync();
    if (error) {
        rvvm_error("Failed to create X11 Window: Error code %d", error);
        return false;
    }

    return x11_vram_init(win);
}

/*
 * X11 Window handling
 */

static void x11_window_free(gui_window_t* win)
{
    if (gui_backend_get_data(win)) {
        scoped_spin_lock (&x11_lock) {
            if (!x11_catch_error()) {
                x11_window_free_internal(win);
            }
            x11_window_t* x11 = gui_backend_get_data(win);
            if (x11->gc) {
                // Display is dead, just free the GC structure
                XFree(x11->gc);
            }
            x11_vram_free(win);
            safe_free(x11);
            x11_global_free();
        }
    }
    gui_backend_set_data(win, NULL);
}

static void x11_window_draw(gui_window_t* win, const rvvm_fb_t* fb, uint32_t x, uint32_t y)
{
    scoped_spin_lock (&x11_lock) {
        if (!x11_catch_error()) {
            x11_window_draw_internal(win, fb, x, y);
        }
    }
}

static void x11_window_set_title(gui_window_t* win, const char* title)
{
    scoped_spin_lock (&x11_lock) {
        if (!x11_catch_error()) {
            x11_window_t* x11 = gui_backend_get_data(win);
            XStoreName(x11_display, x11->window, title);
            XFlush(x11_display);
        }
    }
}

static void x11_window_grab_input(gui_window_t* win, bool grab)
{
    scoped_spin_lock (&x11_lock) {
        if (!x11_catch_error()) {
            x11_window_grab_input_internal(win, grab);
        }
    }
}

static void x11_window_hide_cursor(gui_window_t* win, bool hide)
{
    scoped_spin_lock (&x11_lock) {
        if (!x11_catch_error()) {
            x11_window_hide_cursor_internal(win, hide);
        }
    }
}

static void x11_window_set_win_size(gui_window_t* win, uint32_t w, uint32_t h)
{
    scoped_spin_lock (&x11_lock) {
        if (!x11_catch_error()) {
            x11_window_t* x11 = gui_backend_get_data(win);
            XResizeWindow(x11_display, x11->window, w, h);
            XFlush(x11_display);
        }
    }
}

static void x11_window_set_min_size(gui_window_t* win, uint32_t w, uint32_t h)
{
    XSizeHints hints = {
        .flags      = PMinSize,
        .min_width  = w,
        .min_height = h,
    };
    scoped_spin_lock (&x11_lock) {
        if (!x11_catch_error()) {
            x11_window_t* x11 = gui_backend_get_data(win);
            XSetWMNormalHints(x11_display, x11->window, &hints);
            XFlush(x11_display);
        }
    }
}

static void x11_window_get_position(gui_window_t* win, int32_t* x, int32_t* y)
{
    x11_window_t* x11 = gui_backend_get_data(win);
    scoped_spin_lock (&x11_lock) {
        *x = x11->pos_x;
        *y = x11->pos_y;
    }
}

static void x11_window_set_position(gui_window_t* win, int32_t x, int32_t y)
{
    scoped_spin_lock (&x11_lock) {
        if (!x11_catch_error()) {
            x11_window_t* x11 = gui_backend_get_data(win);
            XMoveWindow(x11_display, x11->window, x, y);
            XFlush(x11_display);
        }
    }
}

static void x11_window_get_scr_size(gui_window_t* win, uint32_t* w, uint32_t* h)
{
    UNUSED(win);
    scoped_spin_lock (&x11_lock) {
        if (!x11_catch_error()) {
            *w = DisplayWidth(x11_display, DefaultScreen(x11_display));
            *h = DisplayHeight(x11_display, DefaultScreen(x11_display));
        }
    }
}

static const gui_backend_cb_t x11_window_cb = {
    .free         = x11_window_free,
    .draw         = x11_window_draw,
    .set_title    = x11_window_set_title,
    .grab_input   = x11_window_grab_input,
    .hide_cursor  = x11_window_hide_cursor,
    .set_win_size = x11_window_set_win_size,
    .set_min_size = x11_window_set_min_size,
    .get_position = x11_window_get_position,
    .set_position = x11_window_set_position,
    .get_scr_size = x11_window_get_scr_size,
};

bool x11_window_init(gui_window_t* win)
{
    bool ret = false;

    // Register X11 backend
    x11_window_t* x11 = safe_new_obj(x11_window_t);
    gui_backend_register(win, &x11_window_cb);
    gui_backend_set_data(win, x11);

    // Register X11 window under x11_lock
    scoped_spin_lock (&x11_lock) {
        vector_push_back(x11_windows, win);
        if (!x11_global_init()) {
            // Global init failed
            break;
        }
        ret = !x11_catch_error() && x11_window_init_internal(win);
    }

    return ret;
}

#else

bool x11_window_init(gui_window_t* win)
{
    UNUSED(win);
    return false;
}

#endif

POP_OPTIMIZATION_SIZE
