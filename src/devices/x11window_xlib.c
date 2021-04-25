/*
x11window.c - X11 VM Window, Xlib backend
Copyright (C) 2021  cerg2010cerg2010 <github.com/cerg2010cerg2010>
                    LekKit <github.com/LekKit>

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

#if defined(USE_X11) && !defined(USE_XCB)

#include "riscv32.h"
#include "fb_window.h"
#include "ps2-mouse.h"
#include "ps2-keyboard.h"
#include "x11keymap.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/Xfixes.h>
#include <X11/XKBlib.h>

struct x11_data
{
	struct mouse_btns btns;
	int x;
	int y;
	Window window;
	GC gc;
	XImage* ximage;
	char* local_data;
};

static Display *dsp = NULL;
static KeySym *keycodemap = NULL;
static int min_keycode;
static int max_keycode;
static int keysyms_per_keycode;
static size_t window_count;

static void x11_update_keymap()
{
	if (keycodemap != NULL)
	{
		XFree(keycodemap);
	}
	keycodemap = XGetKeyboardMapping(dsp,
			min_keycode,
			max_keycode - min_keycode + 1,
			&keysyms_per_keycode);
}

void fb_create_window(struct fb_data* data, unsigned width, unsigned height, const char* name)
{
	++window_count;

	struct x11_data *xdata = malloc(sizeof(struct x11_data));
	data->winsys_data = xdata;
	if (dsp == NULL)
	{
		dsp = XOpenDisplay(NULL);
		if (dsp == NULL) {
			printf("Could not open a connection to the X server\n");
			return;
		}
	}

	if (keycodemap == NULL)
	{
		XDisplayKeycodes(dsp, &min_keycode, &max_keycode);
		x11_update_keymap();
	}

	xdata->local_data = data->framebuffer;//malloc(4*width*height);

	XSetWindowAttributes attributes = {
	    .event_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
	};
	xdata->window = XCreateWindow(dsp, DefaultRootWindow(dsp),
	        0, 0, width, height, 0,
	        DefaultDepth(dsp, DefaultScreen(dsp)),
	        InputOutput, CopyFromParent, CWEventMask, &attributes);

	XSizeHints hints = {
		.flags = PMinSize | PMaxSize,
		.min_width = width, .min_height = height,
		.max_width = width, .max_height = height,
	};
	XSetWMNormalHints(dsp, xdata->window, &hints);
	XStoreName(dsp, xdata->window, name);
	XMapWindow(dsp, xdata->window);

	xdata->gc = XCreateGC(dsp, xdata->window, 0, NULL);

	xdata->ximage = XCreateImage(dsp, DefaultVisual(dsp, DefaultScreen(dsp)),
	        DefaultDepth(dsp, DefaultScreen(dsp)), ZPixmap, 0,
	        data->framebuffer, width, height, 8, 0);

	XSync(dsp, False);
}

void fb_close_window(struct fb_data *data)
{
	struct x11_data *xdata = data->winsys_data;
	if (keycodemap != NULL)
	{
		XFree(keycodemap);
		keycodemap = NULL;
	}
	XDestroyImage(xdata->ximage);
	XFreeGC(dsp, xdata->gc);
	XDestroyWindow(dsp, xdata->window);
	if (--window_count == 0)
	{
		XCloseDisplay(dsp);
	}
}

static void r5g6b5_to_r8g8b8(const void* _in, void* _out, size_t length)
{
	const uint8_t* in = (uint8_t*)_in;
	uint8_t* out = (uint8_t*)_out;
	for (size_t i=0; i<length; ++i) {
		uint8_t r5 = in[i*2] & 31;
		uint8_t g6 = ((in[i*2] >> 5) | (in[i*2 + 1] << 3)) & 63;
		uint8_t b5 = in[i*2 + 1] >> 3;

		out[i*4] = (r5 << 3) | (r5 >> 2);
		out[i*4 + 1] = (g6 << 2) | (g6 >> 4);
		out[i*4 + 2] = (b5 << 3) | (b5 >> 2);
		out[i*4 + 3] = 0;
	}
}

#define GET_DATA_FOR_WINDOW(data, all_data, nfbs, event) \
	do { \
		for (size_t i = 0; i < nfbs; ++i) \
		{ \
			struct x11_data *tmpdata = all_data[i].winsys_data; \
			if (tmpdata != NULL && tmpdata->window == event.window) \
			{ \
				data = &all_data[i]; \
				break; \
			} \
		} \
	} while (0)
					

void fb_update(struct fb_data *all_data, size_t nfbs)
{
	if (dsp == NULL)
	{
		return;
	}

	for (size_t i = 0; i < nfbs; ++i)
	{
		struct x11_data *xdata = all_data[i].winsys_data;
		if (!xdata->ximage)
		{
			continue;
		}

		UNUSED(r5g6b5_to_r8g8b8);
		//r5g6b5_to_r8g8b8(in_data, local_data, ximage->width*ximage->height);
		XPutImage(dsp, xdata->window, xdata->gc, xdata->ximage, 0, 0, 0, 0, xdata->ximage->width, xdata->ximage->height);
	}

	XSync(dsp, False);

	for (int pending = XPending(dsp); pending != 0; --pending)
	{
		XEvent ev;
		XNextEvent(dsp, &ev);
		switch (ev.type)
		{
			case ButtonPress:
				{
					struct fb_data *data = NULL;
					GET_DATA_FOR_WINDOW(data, all_data, nfbs, ev.xbutton);
					if (data == NULL) break;
					struct x11_data *xdata = data->winsys_data;

					if (ev.xbutton.button == Button1)
					{
						xdata->btns.left = true;
					}
					else if (ev.xbutton.button == Button2)
					{
						xdata->btns.middle = true;
					}
					else if (ev.xbutton.button == Button3)
					{
						xdata->btns.right = true;
					}
					ps2_handle_mouse(data->mouse, 0, 0, &xdata->btns);
					break;
				}
			case ButtonRelease:
				{
					struct fb_data *data = NULL;
					GET_DATA_FOR_WINDOW(data, all_data, nfbs, ev.xbutton);
					if (data == NULL) break;
					struct x11_data *xdata = data->winsys_data;

					if (ev.xbutton.button == Button1)
					{
						xdata->btns.left = false;
					}
					else if (ev.xbutton.button == Button2)
					{
						xdata->btns.middle = false;
					}
					else if (ev.xbutton.button == Button3)
					{
						xdata->btns.right = false;
					}
					ps2_handle_mouse(data->mouse, 0, 0, &xdata->btns);
					break;
				}
			case MotionNotify:
				{
					struct fb_data *data = NULL;
					GET_DATA_FOR_WINDOW(data, all_data, nfbs, ev.xmotion);
					if (data == NULL) break;
					struct x11_data *xdata = data->winsys_data;

					ps2_handle_mouse(data->mouse, ev.xmotion.x - xdata->x, -(ev.xmotion.y - xdata->y), &xdata->btns);
					xdata->x = ev.xmotion.x;
					xdata->y = ev.xmotion.y;
					break;
				}
			case KeyPress:
				{
					struct fb_data *data = NULL;
					GET_DATA_FOR_WINDOW(data, all_data, nfbs, ev.xkey);
					if (data == NULL) break;

					KeySym keysym = keycodemap[(ev.xkey.keycode - min_keycode) * keysyms_per_keycode];
					struct key k = x11keysym2makecode(keysym);
#if 0
					printf("keysym pressed: %04x code ", (uint16_t)keysym);
					for (size_t i = 0; i < k.len; ++i)
					{
						printf("%02x ", k.keycode[i]);
					}
					printf("\n");
#endif
					ps2_handle_keyboard(data->keyboard, &k, true);
					break;
				}
			case KeyRelease:
				{
					struct fb_data *data = NULL;
					GET_DATA_FOR_WINDOW(data, all_data, nfbs, ev.xkey);
					if (data == NULL) break;

					if (pending > 1)
					{
						XEvent nev;
						XPeekEvent(dsp, &nev);
						if (nev.type == KeyPress
								&& nev.xkey.time == ev.xkey.time
								&& nev.xkey.keycode == ev.xkey.keycode)
						{
							/* skip the fake key press/release event */
							XNextEvent(dsp, &nev);
							--pending;
							break;
						}
					}

					KeySym keysym = keycodemap[(ev.xkey.keycode - min_keycode) * keysyms_per_keycode];
					struct key k = x11keysym2makecode(keysym);
#if 0
					printf("keysym released: %04x code ", (uint16_t)keysym);
					for (size_t i = 0; i < k.len; ++i)
					{
						printf("%02x ", k.keycode[i]);
					}
					printf("\n");
#endif
					ps2_handle_keyboard(data->keyboard, &k, false);
					break;
				}
			case MappingNotify:
				{
					if (ev.xmapping.request != MappingKeyboard)
					{
						/* not interested in others */
						break;
					}

					min_keycode = ev.xmapping.first_keycode;
					max_keycode = ev.xmapping.first_keycode + ev.xmapping.count;
					x11_update_keymap();
					break;
				}
		}
	}

	for (size_t i = 0; i < nfbs; ++i)
	{
		ps2_handle_keyboard(all_data[i].keyboard, NULL, false);
	}
}

#endif
