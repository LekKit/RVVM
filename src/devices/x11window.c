/*
x11window.c - X11 VM Window
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

#ifdef USE_X11

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <math.h>
#include <stdbool.h>
#include <sys/shm.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/Xfixes.h>
#include <X11/XKBlib.h>
#include <unistd.h>
#include <stdint.h>

#include "fb_window.h"
#include "ps2-mouse.h"
#include "ps2-keyboard.h"
#include "x11keymap.h"

struct x11_data
{
	Display* dsp;
	Window window;
	GC gc;
	XImage* ximage;
	char* local_data;
};

void fb_create_window(struct fb_data* data, unsigned width, unsigned height, const char* name)
{
	struct x11_data *xdata = malloc(sizeof(struct x11_data));
	data->winsys_data = xdata;
	xdata->dsp = XOpenDisplay(NULL);
	if (!xdata->dsp) {
		printf("Could not open a connection to the X server\n");
		return;
	}

	xdata->local_data = data->framebuffer;//malloc(4*width*height);

	XSetWindowAttributes attributes = {
	    //.event_mask = KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
	    .backing_store = NotUseful
	};
	xdata->window = XCreateWindow(xdata->dsp, DefaultRootWindow(xdata->dsp),
	        0, 0, width, height, 0,
	        DefaultDepth(xdata->dsp, XDefaultScreen(xdata->dsp)),
	        InputOutput, CopyFromParent, CWBackingStore, &attributes);

	XStoreName(xdata->dsp, xdata->window, name);
	XSelectInput(xdata->dsp, xdata->window, KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask);
	XkbSetDetectableAutoRepeat(xdata->dsp, false, NULL); /* disable key auto repeat */
	XMapWindow(xdata->dsp, xdata->window);

	XGCValues xgcvalues = {
	    .graphics_exposures = False
	};
	xdata->gc = XCreateGC(xdata->dsp, xdata->window, GCGraphicsExposures, &xgcvalues);

	xdata->ximage = XCreateImage(xdata->dsp, XDefaultVisual(xdata->dsp, XDefaultScreen(xdata->dsp)),
	        DefaultDepth(xdata->dsp, XDefaultScreen(xdata->dsp)), ZPixmap, 0,
	        data->framebuffer, width, height, 8, 0);

	XSync(xdata->dsp, False);
}

void fb_close_window(struct fb_data *data)
{
	struct x11_data *xdata = data->winsys_data;
	XFreeGC(xdata->dsp, xdata->gc);
	XDestroyWindow(xdata->dsp, xdata->window);
	XCloseDisplay(xdata->dsp);
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

void fb_update(struct fb_data *data)
{
	struct x11_data *xdata = data->winsys_data;

	if (!xdata->dsp || !xdata->ximage) return;
	UNUSED(r5g6b5_to_r8g8b8);
	//r5g6b5_to_r8g8b8(in_data, local_data, ximage->width*ximage->height);
	XPutImage(xdata->dsp, xdata->window, xdata->gc, xdata->ximage, 0, 0, 0, 0, xdata->ximage->width, xdata->ximage->height);
	XSync(xdata->dsp, False);

	static struct mouse_btns btns;
	static int x, y;

	int xcur = 0, ycur = 0;
	for (int pending = XPending(xdata->dsp); pending != 0; --pending)
	{
		XEvent ev;
		XNextEvent(xdata->dsp, &ev);
		if (ev.type == ButtonPress)
		{
			if (ev.xbutton.button == Button1)
			{
				btns.left = true;
			}
			else if (ev.xbutton.button == Button2)
			{
				btns.middle = true;
			}
			else if (ev.xbutton.button == Button3)
			{
				btns.right = true;
			}
		}
		else if (ev.type == ButtonRelease)
		{
			if (ev.xbutton.button == Button1)
			{
				btns.left = false;
			}
			else if (ev.xbutton.button == Button2)
			{
				btns.middle = false;
			}
			else if (ev.xbutton.button == Button3)
			{
				btns.right = false;
			}
		}
		else if (ev.type == MotionNotify)
		{
			xcur += ev.xmotion.x - x;
			ycur += -(ev.xmotion.y - y);
			x = ev.xmotion.x;
			y = ev.xmotion.y;
		}
		else if (ev.type == KeyPress)
		{
			KeySym keysym = XkbKeycodeToKeysym(xdata->dsp, ev.xkey.keycode, 0, 0);
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
		}
		else if (ev.type == KeyRelease)
		{
			if (pending > 1)
			{
				XEvent nev;
				XPeekEvent(xdata->dsp, &nev);
				if (nev.type == KeyPress
						&& nev.xkey.time == ev.xkey.time
						&& nev.xkey.keycode == ev.xkey.keycode)
				{
					/* skip the fake key press/release event */
					XNextEvent(xdata->dsp, &nev);
					--pending;
					continue;
				}
			}

			KeySym keysym = XkbKeycodeToKeysym(xdata->dsp, ev.xkey.keycode, 0, 0);
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
		}

	}

	//if (xcur != 0 && ycur != 0) printf("motion x: %d y: %d\n", xcur, ycur);
	ps2_handle_mouse(data->mouse, xcur, ycur, &btns);
	ps2_handle_keyboard(data->keyboard, NULL, false);
}

#endif
