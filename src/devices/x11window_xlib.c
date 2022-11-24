/*
x11window.c - X11 VM Window, Xlib backend
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

#include "fb_window.h"
#include "utils.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#ifdef USE_XSHM
#include <X11/extensions/XShm.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#endif

struct win_data {
    Display* display;
    Window window;
    GC gc;
    XImage* ximage;
    KeySym* keycodemap;
    int min_keycode;
    int max_keycode;
    int keysyms_per_keycode;
    Atom del_win;
#ifdef USE_XSHM
    XShmSegmentInfo seginfo;
#endif
};

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
        case XK_Control_L:    return HID_KEY_LEFTCTRL;
        case XK_Shift_L:      return HID_KEY_LEFTSHIFT;
        case XK_Alt_L:        return HID_KEY_LEFTALT;
        case XK_Super_L:      return HID_KEY_LEFTMETA;
        case XK_Control_R:    return HID_KEY_RIGHTCTRL;
        case XK_Shift_R:      return HID_KEY_RIGHTSHIFT;
        case XK_Alt_R:        return HID_KEY_RIGHTALT;
        case XK_Super_R:      return HID_KEY_RIGHTMETA;
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
        case XK_Menu:         return HID_KEY_MENU;
    }
    return HID_KEY_NONE;
}

static hid_key_t x11_event_key_to_hid(fb_window_t* win, int keycode)
{
    if (win->data->keycodemap == NULL) {
        rvvm_warn("XKeycodemap not initialized!");
        return HID_KEY_NONE;
    } else if (keycode < win->data->min_keycode || keycode > win->data->max_keycode) {
        rvvm_warn("XEvent keycode out of keycodemap range!");
        return HID_KEY_NONE;
    } else {
        uint32_t entry = (keycode - win->data->min_keycode) * win->data->keysyms_per_keycode;
        return x11_keysym_to_hid(win->data->keycodemap[entry]);
    }
}

static void x11_update_keymap(fb_window_t* win)
{
    KeySym* keycodemap = XGetKeyboardMapping(win->data->display,
                            win->data->min_keycode,
                            win->data->max_keycode - win->data->min_keycode + 1,
                            &win->data->keysyms_per_keycode);
    if (keycodemap) {
        if (win->data->keycodemap) XFree(win->data->keycodemap);
        win->data->keycodemap = keycodemap;
    } else rvvm_warn("XGetKeyboardMapping() failed!");
}

static rgb_fmt_t x11_get_rgb_format(Display* display)
{
    rgb_fmt_t format = RGB_FMT_INVALID;
    int nfmts = 0;
    XPixmapFormatValues *fmts = XListPixmapFormats(display, &nfmts);
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
static int x11_dummy_error_handler(Display *display, XErrorEvent *error)
{
    UNUSED(display);
    UNUSED(error);
    xshm_error = true;
    return 0;
}

static void* x11_xshm_init(fb_window_t* win)
{
    Display* dsp = win->data->display;
    int (*old_handler)(Display*, XErrorEvent*) = XSetErrorHandler(x11_dummy_error_handler);

    if (XShmQueryExtension(dsp)) {
        win->data->ximage = XShmCreateImage(dsp,
                            DefaultVisual(dsp, DefaultScreen(dsp)),
                            DefaultDepth(dsp, DefaultScreen(dsp)),
                            ZPixmap, NULL, &win->data->seginfo,
                            win->fb.width, win->fb.height);
        if (win->data->ximage) {
            win->data->seginfo.shmid = shmget(IPC_PRIVATE, framebuffer_size(&win->fb), IPC_CREAT | 0777);
            if (win->data->seginfo.shmid > 0) {
                win->data->seginfo.shmaddr = win->data->ximage->data = shmat(win->data->seginfo.shmid, NULL, 0);
                if (win->data->seginfo.shmaddr != (void*)-1 && win->data->seginfo.shmaddr != NULL) {
                    if (!XShmAttach(dsp, &win->data->seginfo)) rvvm_error("XShmAttach() failed");
                } else {
                    win->data->seginfo.shmaddr = NULL;
                    rvvm_error("XShm shmat() failed");
                }
            } else rvvm_error("XShm shmget() failed");
        } else rvvm_error("XShmCreateImage() failed");
    } else rvvm_info("XShm extension not supported");
    // Process errors, if any
    XSync(dsp, False);
    // Cleanup on error
    if (win->data->seginfo.shmaddr == NULL || xshm_error) {
        rvvm_info("XShm failed to initialize");
        if (win->data->seginfo.shmaddr) shmdt(win->data->seginfo.shmaddr);
        if (win->data->seginfo.shmid > 0) shmctl(win->data->seginfo.shmid, IPC_RMID, NULL);
        if (win->data->ximage) XDestroyImage(win->data->ximage);
        win->data->seginfo.shmaddr = NULL;
    }

    XSetErrorHandler(old_handler);

    return win->data->seginfo.shmaddr;
}
#endif

bool fb_window_create(fb_window_t* win)
{
    Display* dsp = XOpenDisplay(NULL);
    if (dsp == NULL) {
        rvvm_error("Could not open a connection to the X server");
        return false;
    }

    win->data = safe_calloc(sizeof(win_data_t), 1);
    win->data->display = dsp;
    win->fb.format = x11_get_rgb_format(dsp);

    XDisplayKeycodes(dsp, &win->data->min_keycode, &win->data->max_keycode);
    x11_update_keymap(win);

    XSetWindowAttributes attributes = {
        .event_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
    };
    win->data->window = XCreateWindow(dsp, DefaultRootWindow(dsp),
                            0, 0, win->fb.width, win->fb.height, 0,
                            DefaultDepth(dsp, DefaultScreen(dsp)),
                            InputOutput, CopyFromParent, CWEventMask, &attributes);

    XSizeHints hints = {
        .flags = PMinSize | PMaxSize,
        .min_width = win->fb.width, .min_height = win->fb.height,
        .max_width = win->fb.width, .max_height = win->fb.height,
    };
    XSetWMNormalHints(dsp, win->data->window, &hints);
    XStoreName(dsp, win->data->window, "RVVM");
    win->data->del_win = XInternAtom(dsp, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dsp, win->data->window, &win->data->del_win, 1);
    XMapWindow(dsp, win->data->window);

    win->data->gc = XCreateGC(dsp, win->data->window, 0, NULL);

#ifdef USE_XSHM
    win->fb.buffer = x11_xshm_init(win);
#endif
    if (win->fb.buffer == NULL) {
        win->data->ximage = XCreateImage(dsp, DefaultVisual(dsp, DefaultScreen(dsp)),
            DefaultDepth(dsp, DefaultScreen(dsp)), ZPixmap, 0,
            NULL, win->fb.width, win->fb.height, 8, 0);
        win->data->ximage->data = safe_calloc(win->data->ximage->bytes_per_line, win->data->ximage->height);

        win->fb.buffer = win->data->ximage->data;
    }

    XSync(dsp, False);
    return true;
}

void fb_window_close(fb_window_t* win)
{
    Display* dsp = win->data->display;
#ifdef USE_XSHM
    if (win->data->seginfo.shmaddr != NULL) {
        XShmDetach(dsp, &win->data->seginfo);
        shmdt(win->data->seginfo.shmaddr);
        shmctl(win->data->seginfo.shmid, IPC_RMID, NULL);
    }
#endif
    XDestroyImage(win->data->ximage);
    XFreeGC(dsp, win->data->gc);
    XDestroyWindow(dsp, win->data->window);
    XFree(win->data->keycodemap);
    XCloseDisplay(dsp);

    free(win->data);
}

void fb_window_update(fb_window_t* win)
{
    Display* dsp = win->data->display;
#ifdef USE_XSHM
    if (win->data->seginfo.shmaddr != NULL) {
        XShmPutImage(dsp,
                win->data->window,
                win->data->gc,
                win->data->ximage,
                0, 0, 0, 0, // src, dst x & y
                win->data->ximage->width,
                win->data->ximage->height,
                False /* send_event */);
    } else
#endif
    {
        XPutImage(dsp,
                win->data->window,
                win->data->gc,
                win->data->ximage,
                0, 0, 0, 0, // src, dst x & y
                win->data->ximage->width,
                win->data->ximage->height);
    }
    XSync(dsp, False);

    for (int pending = XPending(dsp); pending != 0; --pending) {
        XEvent ev;
        XNextEvent(dsp, &ev);
        switch (ev.type) {
            case ButtonPress:
                if (ev.xbutton.button == Button1) {
                    hid_mouse_press(win->mouse, HID_BTN_LEFT);
                } else if (ev.xbutton.button == Button2) {
                    hid_mouse_press(win->mouse, HID_BTN_MIDDLE);
                } else if (ev.xbutton.button == Button3) {
                    hid_mouse_press(win->mouse, HID_BTN_RIGHT);
                } else if (ev.xbutton.button == Button4) {
                    hid_mouse_scroll(win->mouse, HID_SCROLL_UP);
                } else if (ev.xbutton.button == Button5) {
                    hid_mouse_scroll(win->mouse, HID_SCROLL_DOWN);
                }
                break;
            case ButtonRelease:
                if (ev.xbutton.button == Button1) {
                    hid_mouse_release(win->mouse, HID_BTN_LEFT);
                } else if (ev.xbutton.button == Button2) {
                    hid_mouse_release(win->mouse, HID_BTN_MIDDLE);
                } else if (ev.xbutton.button == Button3) {
                    hid_mouse_release(win->mouse, HID_BTN_RIGHT);
                }
                break;
            case MotionNotify:
                hid_mouse_place(win->mouse, ev.xmotion.x, ev.xmotion.y);
                break;
            case KeyPress:
                hid_keyboard_press(win->keyboard, x11_event_key_to_hid(win, ev.xkey.keycode));
                break;
            case KeyRelease:
                if (pending > 1) {
                    XEvent nev;
                    XPeekEvent(dsp, &nev);
                    if (nev.type == KeyPress && nev.xkey.time == ev.xkey.time && nev.xkey.keycode == ev.xkey.keycode) {
                        // Skip the fake key press/release event
                        XNextEvent(dsp, &nev);
                        --pending;
                        break;
                    }
                }
                hid_keyboard_release(win->keyboard, x11_event_key_to_hid(win, ev.xkey.keycode));
                break;
            case MappingNotify:
                if (ev.xmapping.request == MappingKeyboard) {
                    win->data->min_keycode = ev.xmapping.first_keycode;
                    win->data->max_keycode = ev.xmapping.first_keycode + ev.xmapping.count - 1;
                    x11_update_keymap(win);
                }
                break;
            case ClientMessage:
                if (((Atom)ev.xclient.data.l[0]) == win->data->del_win) {
                    // Power down the machine that owns this window
                    rvvm_reset_machine(win->machine, false);
                }
                break;
        }
    }
}
