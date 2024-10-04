/*
x11window.c - X11 RVVM Window, Xlib backend
Copyright (C) 2021  LekKit <github.com/LekKit>

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

#include "gui_window.h"
#include "dlib.h"
#include "vma_ops.h"
#include "utils.h"
#include "compiler.h"

#define X11_DYNAMIC_LOADING

// Resolve symbols at runtime
#define X11_DLIB_SYM(sym) static typeof(sym)* sym##_dlib = NULL;

// Check for X11 headers presence
#if defined(USE_X11) && !(CHECK_INCLUDE(X11/Xlib.h, 1) && CHECK_INCLUDE(X11/Xutil.h, 1) && CHECK_INCLUDE(X11/keysym.h, 1))
#undef USE_X11
#warning Disabling X11 support as <X11/Xlib.h> is unavailable
#endif

// Check for XShm header presence
#if CHECK_INCLUDE(X11/extensions/XShm.h, 1) && CHECK_INCLUDE(sys/ipc.h, 1) && CHECK_INCLUDE(sys/shm.h, 1)
#define USE_XSHM
#else
#warning Disabling XShm support as <X11/extensions/XShm.h> is unavailable
#endif

#ifdef USE_X11

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#ifdef USE_XSHM
#include <X11/extensions/XShm.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#ifdef X11_DYNAMIC_LOADING
X11_DLIB_SYM(XShmQueryExtension)
X11_DLIB_SYM(XShmDetach)
X11_DLIB_SYM(XShmCreateImage)
X11_DLIB_SYM(XShmAttach)
X11_DLIB_SYM(XShmPutImage)

#define XShmQueryExtension XShmQueryExtension_dlib
#define XShmDetach XShmDetach_dlib
#define XShmCreateImage XShmCreateImage_dlib
#define XShmAttach XShmAttach_dlib
#define XShmPutImage XShmPutImage_dlib
#endif

#endif

#ifdef X11_DYNAMIC_LOADING
X11_DLIB_SYM(XGetKeyboardMapping)
X11_DLIB_SYM(XFree)
X11_DLIB_SYM(XListPixmapFormats)
X11_DLIB_SYM(XSetErrorHandler)
X11_DLIB_SYM(XSync)
X11_DLIB_SYM(XPutImage)
X11_DLIB_SYM(XWarpPointer)
X11_DLIB_SYM(XFlush)
X11_DLIB_SYM(XPending)
X11_DLIB_SYM(XNextEvent)
X11_DLIB_SYM(XPeekEvent)
X11_DLIB_SYM(XGrabKeyboard)
X11_DLIB_SYM(XGrabPointer)
X11_DLIB_SYM(XQueryPointer)
X11_DLIB_SYM(XUngrabKeyboard)
X11_DLIB_SYM(XUngrabPointer)
X11_DLIB_SYM(XStoreName)
X11_DLIB_SYM(XFreeGC)
X11_DLIB_SYM(XDestroyWindow)
X11_DLIB_SYM(XCloseDisplay)
X11_DLIB_SYM(XDisplayKeycodes)
X11_DLIB_SYM(XSetWMNormalHints)
X11_DLIB_SYM(XSetWMProtocols)
X11_DLIB_SYM(XCreateBitmapFromData)
X11_DLIB_SYM(XCreatePixmapCursor)
X11_DLIB_SYM(XDefineCursor)
X11_DLIB_SYM(XFreeCursor)
X11_DLIB_SYM(XFreePixmap)
X11_DLIB_SYM(XMapWindow)
X11_DLIB_SYM(XCreateGC)
X11_DLIB_SYM(XCreateImage)
X11_DLIB_SYM(XOpenDisplay)
X11_DLIB_SYM(XInternAtom)
X11_DLIB_SYM(XCreateWindow)

#define XGetKeyboardMapping XGetKeyboardMapping_dlib
#define XFree XFree_dlib
#define XListPixmapFormats XListPixmapFormats_dlib
#define XSetErrorHandler XSetErrorHandler_dlib
#define XSync XSync_dlib
#define XPutImage XPutImage_dlib
#define XWarpPointer XWarpPointer_dlib
#define XFlush XFlush_dlib
#define XPending XPending_dlib
#define XNextEvent XNextEvent_dlib
#define XPeekEvent XPeekEvent_dlib
#define XGrabKeyboard XGrabKeyboard_dlib
#define XGrabPointer XGrabPointer_dlib
#define XQueryPointer XQueryPointer_dlib
#define XUngrabKeyboard XUngrabKeyboard_dlib
#define XUngrabPointer XUngrabPointer_dlib
#define XStoreName XStoreName_dlib
#define XFreeGC XFreeGC_dlib
#define XDestroyWindow XDestroyWindow_dlib
#define XCloseDisplay XCloseDisplay_dlib
#define XDisplayKeycodes XDisplayKeycodes_dlib
#define XSetWMNormalHints XSetWMNormalHints_dlib
#define XSetWMProtocols XSetWMProtocols_dlib
#define XCreateBitmapFromData XCreateBitmapFromData_dlib
#define XCreatePixmapCursor XCreatePixmapCursor_dlib
#define XDefineCursor XDefineCursor_dlib
#define XFreeCursor XFreeCursor_dlib
#define XFreePixmap XFreePixmap_dlib
#define XMapWindow XMapWindow_dlib
#define XCreateGC XCreateGC_dlib
#define XCreateImage XCreateImage_dlib
#define XOpenDisplay XOpenDisplay_dlib
#define XInternAtom XInternAtom_dlib
#define XCreateWindow XCreateWindow_dlib
#endif

typedef struct {
    Display* display;
    Window window;
    GC gc;
    XImage* ximage;
    void* image_buffer;

#ifdef USE_XSHM
    XShmSegmentInfo seginfo;
#endif

    // Keycode stuff
    KeySym* keycodemap;
    int min_keycode;
    int max_keycode;
    int keysyms_per_keycode;

    // Handle window closing
    Atom wm_delete;

    bool grabbed;

    // These are used to restore the original pointer position
    Window grab_root;
    int grab_pointer_x;
    int grab_pointer_y;
} x11_data_t;

static hid_key_t x11_keysym_to_hid(KeySym keysym)
{
    // XK_ definitions are huge numbers, don't use a table
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
        case XK_Prior:        return HID_KEY_PAGEUP; // ?
        case XK_Delete:       return HID_KEY_DELETE;
        case XK_End:          return HID_KEY_END;
        case XK_Next:         return HID_KEY_PAGEDOWN; // ?
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
    }
    return HID_KEY_NONE;
}

static hid_key_t x11_event_key_to_hid(x11_data_t* x11, int keycode)
{
    if (x11->keycodemap == NULL) {
        rvvm_warn("XKeycodemap not initialized!");
        return HID_KEY_NONE;
    } else if (keycode < x11->min_keycode || keycode > x11->max_keycode) {
        rvvm_warn("XEvent keycode out of keycodemap range!");
        return HID_KEY_NONE;
    } else {
        uint32_t entry = (keycode - x11->min_keycode) * x11->keysyms_per_keycode;
        return x11_keysym_to_hid(x11->keycodemap[entry]);
    }
}

static void x11_update_keymap(x11_data_t* x11)
{
    KeySym* keycodemap = XGetKeyboardMapping(x11->display,
                            x11->min_keycode, x11->max_keycode - x11->min_keycode + 1,
                            &x11->keysyms_per_keycode);
    if (keycodemap) {
        if (x11->keycodemap) {
            XFree(x11->keycodemap);
        }
        x11->keycodemap = keycodemap;
    } else {
        rvvm_warn("XGetKeyboardMapping() failed!");
    }
}

static rgb_fmt_t x11_get_rgb_format(Display* display)
{
    rgb_fmt_t format = RGB_FMT_INVALID;
    int nfmts = 0;
    XPixmapFormatValues* fmts = XListPixmapFormats(display, &nfmts);
    if (fmts) {
        for (int i = 0; i < nfmts; ++i) {
            if (fmts[i].depth == DefaultDepth(display, DefaultScreen(display))) {
                format = rgb_format_from_bpp(fmts[i].bits_per_pixel);
                break;
            }
        }
        XFree(fmts);
    }
    return format;
}

#ifdef USE_XSHM

static bool xshm_error = false;
static int x11_dummy_error_handler(Display* display, XErrorEvent* error)
{
    UNUSED(display);
    UNUSED(error);
    xshm_error = true;
    return 0;
}

static void x11_free_xshm(x11_data_t* x11)
{
    if (x11->seginfo.shmaddr) {
        XShmDetach(x11->display, &x11->seginfo);
        shmdt(x11->seginfo.shmaddr);
    }
    x11->seginfo.shmaddr = NULL;
}

static void* x11_xshm_init(gui_window_t* win)
{
    x11_data_t* x11 = win->win_data;
    Display* dsp = x11->display;

    if (!XShmQueryExtension(dsp)) {
        rvvm_info("XShm extension not supported");
        return NULL;
    }

    x11->ximage = XShmCreateImage(dsp,
                        DefaultVisual(dsp, DefaultScreen(dsp)),
                        DefaultDepth(dsp, DefaultScreen(dsp)),
                        ZPixmap, NULL, &x11->seginfo,
                        win->fb.width, win->fb.height);
    if (!x11->ximage) {
        rvvm_error("XShmCreateImage() failed!");
        return NULL;
    }

    x11->seginfo.shmid = shmget(IPC_PRIVATE, framebuffer_size(&win->fb), IPC_CREAT | 0777);
    if (x11->seginfo.shmid < 0) {
        rvvm_error("XShm shmget() failed!");
        return NULL;
    }

    x11->seginfo.shmaddr = shmat(x11->seginfo.shmid, NULL, 0);
    shmctl(x11->seginfo.shmid, IPC_RMID, NULL);
    if (x11->seginfo.shmaddr == (void*)-1 || x11->seginfo.shmaddr == NULL) {
        rvvm_error("XShm shmget() failed!");
        return NULL;
    }

    x11->ximage->data = x11->seginfo.shmaddr;
    if (!XShmAttach(dsp, &x11->seginfo)) {
        rvvm_error("XShmAttach() failed!");
        return NULL;
    }

    return x11->seginfo.shmaddr;
}

static void* x11_xshm_attach(gui_window_t* win)
{
    x11_data_t* x11 = win->win_data;
    void* old_handler = XSetErrorHandler(x11_dummy_error_handler);
    void* xshm = x11_xshm_init(win);

    // Process errors, if any
    XSync(x11->display, False);

    // Cleanup on error
    if (xshm == NULL || xshm_error) {
        rvvm_info("XShm failed to initialize");
        x11_free_xshm(x11);
        if (x11->ximage) {
            XDestroyImage(x11->ximage);
            x11->ximage = NULL;
        }
        xshm = NULL;
    }

    XSync(x11->display, False);
    XSetErrorHandler(old_handler);
    return xshm;
}

#endif

static void x11_window_draw(gui_window_t* win)
{
    x11_data_t* x11 = win->win_data;
    Display* dsp = x11->display;
#ifdef USE_XSHM
    if (x11->seginfo.shmaddr != NULL) {
        XShmPutImage(dsp,
                     x11->window,
                     x11->gc,
                     x11->ximage,
                     0, 0, 0, 0, // src, dst x & y
                     x11->ximage->width,
                     x11->ximage->height,
                     False /* send_event */);
        return;
    }
#endif
    XPutImage(dsp,
              x11->window,
              x11->gc,
              x11->ximage,
              0, 0, 0, 0, // src, dst x & y
              x11->ximage->width,
              x11->ximage->height);
}

static void x11_handle_mouse_motion(gui_window_t* win, XMotionEvent* xmotion)
{
    x11_data_t* x11 = win->win_data;
    Display* dsp = x11->display;

    if (x11->grabbed) {
        int center_x = win->fb.width / 2;
        int center_y = win->fb.height / 2;
        int dx = xmotion->x - center_x;
        int dy = xmotion->y - center_y;
        if (dx || dy) {
            XWarpPointer(dsp, None, x11->window, 0, 0, 0, 0, center_x, center_y);
            XFlush(dsp);
            win->on_mouse_move(win, dx, dy);
        }
    } else {
        win->on_mouse_place(win, xmotion->x, xmotion->y);
    }
}

static void x11_window_poll(gui_window_t* win)
{
    x11_data_t* x11 = win->win_data;
    Display* dsp = x11->display;
    size_t pending = 0;

    XSync(dsp, False);
    while ((pending = XPending(dsp))) {
        XEvent ev = {0};
        XNextEvent(dsp, &ev);
        switch (ev.type) {
            case ButtonPress:
                if (ev.xbutton.button == Button1) {
                    win->on_mouse_press(win, HID_BTN_LEFT);
                } else if (ev.xbutton.button == Button2) {
                    win->on_mouse_press(win, HID_BTN_MIDDLE);
                } else if (ev.xbutton.button == Button3) {
                    win->on_mouse_press(win, HID_BTN_RIGHT);
                } else if (ev.xbutton.button == Button4) {
                    win->on_mouse_scroll(win, HID_SCROLL_UP);
                } else if (ev.xbutton.button == Button5) {
                    win->on_mouse_scroll(win, HID_SCROLL_DOWN);
                }
                break;
            case ButtonRelease:
                if (ev.xbutton.button == Button1) {
                    win->on_mouse_release(win, HID_BTN_LEFT);
                } else if (ev.xbutton.button == Button2) {
                    win->on_mouse_release(win, HID_BTN_MIDDLE);
                } else if (ev.xbutton.button == Button3) {
                    win->on_mouse_release(win, HID_BTN_RIGHT);
                }
                break;
            case MotionNotify:
                x11_handle_mouse_motion(win, &ev.xmotion);
                break;
            case KeyPress:
                win->on_key_press(win, x11_event_key_to_hid(x11, ev.xkey.keycode));
                break;
            case KeyRelease:
                if (pending > 1) {
                    XEvent tmp = {0};
                    XPeekEvent(dsp, &tmp);
                    if (tmp.type == KeyPress && tmp.xkey.time == ev.xkey.time && tmp.xkey.keycode == ev.xkey.keycode) {
                        // Skip the repeated key release event, repeated presses are filtered by hid_keyboard
                        break;
                    }
                }
                win->on_key_release(win, x11_event_key_to_hid(x11, ev.xkey.keycode));
                break;
            case MappingNotify:
                if (ev.xmapping.request == MappingKeyboard) {
                    x11->min_keycode = ev.xmapping.first_keycode;
                    x11->max_keycode = ev.xmapping.first_keycode + ev.xmapping.count - 1;
                    x11_update_keymap(x11);
                }
                break;
            case ClientMessage:
                if (((Atom)ev.xclient.data.l[0]) == x11->wm_delete) {
                    // Attempted to close window
                    win->on_close(win);
                }
                break;
            case FocusOut:
                if (ev.xfocus.mode == NotifyNormal) {
                    win->on_focus_lost(win);
                }
                break;
        }
    }
}

static void x11_window_grab_input(gui_window_t* win, bool grab)
{
    x11_data_t* x11 = win->win_data;
    Display* dsp = x11->display;

    if (x11->grabbed != grab) {
        x11->grabbed = grab;
        if (x11->grabbed) {
            // Grab the input
            XGrabKeyboard(dsp, x11->window, True, GrabModeAsync, GrabModeAsync, CurrentTime);
            XGrabPointer(dsp, x11->window, True,
                    ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
                    GrabModeAsync, GrabModeAsync, None, None, CurrentTime);

            // Save the original cursor position
            Window child = None;
            int win_x = 0, win_y = 0;
            unsigned int mask = 0;
            x11->grab_root = None;
            XQueryPointer(dsp, x11->window, &x11->grab_root,
                    &child, &x11->grab_pointer_x, &x11->grab_pointer_y,
                    &win_x, &win_y, &mask);
            XWarpPointer(dsp, None, x11->window, 0, 0, 0, 0,
                    win->fb.width / 2, win->fb.height / 2);
        } else {
            // Ungrab
            XUngrabKeyboard(dsp, CurrentTime);
            XUngrabPointer(dsp, CurrentTime);

            // Restore the original cursor position
            if (x11->grab_root) {
                XWarpPointer(x11->display, None, x11->grab_root, 0, 0, 0, 0,
                            x11->grab_pointer_x, x11->grab_pointer_y);
                x11->grab_root = None;
            }
        }
    }
}

static void x11_window_set_title(gui_window_t* win, const char* title)
{
    x11_data_t* x11 = win->win_data;
    Display* dsp = x11->display;

    XStoreName(dsp, x11->window, title);
}

static void x11_window_remove(gui_window_t* win)
{
    x11_data_t* x11 = win->win_data;
    Display* dsp = x11->display;

    // Restore input
    x11_window_grab_input(win, false);
#ifdef USE_XSHM
    x11_free_xshm(x11);
#endif
    if (x11->image_buffer) {
        // Free framebuffer VMA
        vma_free(x11->image_buffer, framebuffer_size(&win->fb));
        x11->ximage->data = NULL;
    }
    XDestroyImage(x11->ximage);
    XFreeGC(dsp, x11->gc);
    XDestroyWindow(dsp, x11->window);
    XFree(x11->keycodemap);
    XCloseDisplay(dsp);

    free(x11);
}

#define X11_DLIB_RESOLVE(lib, sym) \
do { \
    sym = dlib_resolve(lib, #sym);\
    if (sym == NULL) return false; \
} while (0)

static bool x11_init_libs(void)
{
#ifdef X11_DYNAMIC_LOADING
    dlib_ctx_t* libx11 = dlib_open("X11", DLIB_NAME_PROBE);

    X11_DLIB_RESOLVE(libx11, XGetKeyboardMapping);
    X11_DLIB_RESOLVE(libx11, XFree);
    X11_DLIB_RESOLVE(libx11, XListPixmapFormats);
    X11_DLIB_RESOLVE(libx11, XSetErrorHandler);
    X11_DLIB_RESOLVE(libx11, XSync);
    X11_DLIB_RESOLVE(libx11, XPutImage);
    X11_DLIB_RESOLVE(libx11, XWarpPointer);
    X11_DLIB_RESOLVE(libx11, XFlush);
    X11_DLIB_RESOLVE(libx11, XPending);
    X11_DLIB_RESOLVE(libx11, XNextEvent);
    X11_DLIB_RESOLVE(libx11, XPeekEvent);
    X11_DLIB_RESOLVE(libx11, XGrabKeyboard);
    X11_DLIB_RESOLVE(libx11, XGrabPointer);
    X11_DLIB_RESOLVE(libx11, XQueryPointer);
    X11_DLIB_RESOLVE(libx11, XUngrabKeyboard);
    X11_DLIB_RESOLVE(libx11, XUngrabPointer);
    X11_DLIB_RESOLVE(libx11, XStoreName);
    X11_DLIB_RESOLVE(libx11, XFreeGC);
    X11_DLIB_RESOLVE(libx11, XDestroyWindow);
    X11_DLIB_RESOLVE(libx11, XCloseDisplay);
    X11_DLIB_RESOLVE(libx11, XDisplayKeycodes);
    X11_DLIB_RESOLVE(libx11, XSetWMNormalHints);
    X11_DLIB_RESOLVE(libx11, XSetWMProtocols);
    X11_DLIB_RESOLVE(libx11, XCreateBitmapFromData);
    X11_DLIB_RESOLVE(libx11, XCreatePixmapCursor);
    X11_DLIB_RESOLVE(libx11, XDefineCursor);
    X11_DLIB_RESOLVE(libx11, XFreeCursor);
    X11_DLIB_RESOLVE(libx11, XFreePixmap);
    X11_DLIB_RESOLVE(libx11, XMapWindow);
    X11_DLIB_RESOLVE(libx11, XCreateGC);
    X11_DLIB_RESOLVE(libx11, XCreateImage);
    X11_DLIB_RESOLVE(libx11, XOpenDisplay);
    X11_DLIB_RESOLVE(libx11, XInternAtom);
    X11_DLIB_RESOLVE(libx11, XCreateWindow);

    dlib_close(libx11);
#ifdef USE_XSHM
    dlib_ctx_t* libxext = dlib_open("Xext", DLIB_NAME_PROBE);

    X11_DLIB_RESOLVE(libxext, XShmQueryExtension);
    X11_DLIB_RESOLVE(libxext, XShmDetach);
    X11_DLIB_RESOLVE(libxext, XShmCreateImage);
    X11_DLIB_RESOLVE(libxext, XShmAttach);
    X11_DLIB_RESOLVE(libxext, XShmPutImage);

    dlib_close(libxext);
#endif
#endif
    return true;
}

bool x11_window_init(gui_window_t* win)
{
    static bool libx11_avail = false;
    DO_ONCE(libx11_avail = x11_init_libs());
    if (!libx11_avail) {
        rvvm_info("Failed to load libX11!");
        return false;
    }

    Display* dsp = XOpenDisplay(NULL);
    if (dsp == NULL) {
        rvvm_info("Could not open a connection to the X server!");
        return false;
    }

    x11_data_t* x11 = safe_new_obj(x11_data_t);
    x11->display = dsp;

    // Initialize callbacks
    win->win_data = x11;

    win->draw = x11_window_draw;
    win->poll = x11_window_poll;
    win->remove = x11_window_remove;
    win->grab_input = x11_window_grab_input;
    win->set_title = x11_window_set_title;

    XDisplayKeycodes(dsp, &x11->min_keycode, &x11->max_keycode);
    x11_update_keymap(x11);

    XSetWindowAttributes attributes = {
        .event_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | FocusChangeMask,
    };
    x11->window = XCreateWindow(dsp, DefaultRootWindow(dsp),
                                0, 0, win->fb.width, win->fb.height, 0,
                                DefaultDepth(dsp, DefaultScreen(dsp)),
                                InputOutput, CopyFromParent, CWEventMask, &attributes);

    XSizeHints hints = {
        .flags = PMinSize | PMaxSize,
        .min_width = win->fb.width, .min_height = win->fb.height,
        .max_width = win->fb.width, .max_height = win->fb.height,
    };
    XSetWMNormalHints(dsp, x11->window, &hints);
    XStoreName(dsp, x11->window, "RVVM");

    // Handle window close
    x11->wm_delete = XInternAtom(dsp, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dsp, x11->window, &x11->wm_delete, 1);

    // Hide cursor
    XColor color = {0};
    const char pixels[8] = {0};
    Pixmap pixmap = XCreateBitmapFromData(dsp, x11->window, pixels, 8, 8);
    Cursor cursor = XCreatePixmapCursor(dsp, pixmap, pixmap, &color, &color, 0, 0);
    XDefineCursor(dsp, x11->window, cursor);
    XFreeCursor(dsp, cursor);
    XFreePixmap(dsp, pixmap);

    // Show window
    XMapWindow(dsp, x11->window);

    x11->gc = XCreateGC(dsp, x11->window, 0, NULL);

    win->fb.format = x11_get_rgb_format(dsp);
#ifdef USE_XSHM
    win->fb.buffer = x11_xshm_attach(win);
#endif
    if (win->fb.buffer == NULL) {
        x11->ximage = XCreateImage(dsp, DefaultVisual(dsp, DefaultScreen(dsp)),
                                   DefaultDepth(dsp, DefaultScreen(dsp)), ZPixmap, 0,
                                   NULL, win->fb.width, win->fb.height, 8, 0);
        x11->image_buffer = vma_alloc(NULL, framebuffer_size(&win->fb), VMA_RDWR);
        x11->ximage->data = x11->image_buffer;
        win->fb.buffer = x11->image_buffer;
    }

    XSync(dsp, False);
    return true;
}

#else

bool x11_window_init(gui_window_t* win)
{
    UNUSED(win);
    return false;
}

#endif
