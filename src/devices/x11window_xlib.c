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

#include "riscv32.h"
#include "fb_window.h"
#include "ps2-mouse.h"
#include "ps2-keyboard.h"
#include "x11keymap.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#ifdef USE_XSHM
#include <X11/extensions/XShm.h>
#include <errno.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#endif

struct x11_data
{
	struct mouse_btns btns;
	int x;
	int y;
	Window window;
	GC gc;
	XImage *ximage;
#ifdef USE_XSHM
	XShmSegmentInfo seginfo;
#endif
};

static Display *dsp = NULL;
static KeySym *keycodemap = NULL;
static int min_keycode;
static int max_keycode;
static int keysyms_per_keycode;
static size_t window_count;
static int bpp;

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

#ifdef USE_XSHM
static bool xerror = false;
static int dummy_error_handler(Display *display, XErrorEvent *error)
{
	UNUSED(display);
	UNUSED(error);
	xerror = true;
	return 0;
}
#endif

static void* x11_xshm_init(struct x11_data *xdata, unsigned width, unsigned height)
{
#ifdef USE_XSHM
	/* Workaround of ugliness of Xlib API */
	int (*old_handler)(Display*, XErrorEvent*) = XSetErrorHandler(dummy_error_handler);

	if (!XShmQueryExtension(dsp))
	{
		/* extension is not supported, quit silently */
		goto err;
	}

	xdata->ximage = XShmCreateImage(dsp,
			DefaultVisual(dsp, DefaultScreen(dsp)),
			DefaultDepth(dsp, DefaultScreen(dsp)),
			ZPixmap,
			NULL,
			&xdata->seginfo,
			width,
			height);
	if (!xdata->ximage)
	{
		printf("Error in XShmCreateImage\n");
		goto err;
	}

	xdata->seginfo.shmid = shmget(IPC_PRIVATE, (bpp / 8) * width * height, IPC_CREAT | 0777);
	if (xdata->seginfo.shmid < 0)
	{

		printf("Error in shmget, err %d\n", errno);
		goto err_image;
	}

	xdata->seginfo.shmaddr = xdata->ximage->data = shmat(xdata->seginfo.shmid, NULL, 0);
	if (xdata->seginfo.shmaddr == (void*) -1)
	{
		printf("Error in shmat, err %d\n", errno);
		goto err_shmget;
	}

	xdata->seginfo.readOnly = False;

	if (!XShmAttach(dsp, &xdata->seginfo))
	{
		printf("Error in XShmAttach\n");
		goto err_shmat;
	}

	XSync(dsp, False);
	if (xerror)
	{
		goto err_shmat;
	}

	XSetErrorHandler(old_handler);

	if (bpp != 32)
	{
		return calloc(4, (size_t)width * height);
	}
	return xdata->seginfo.shmaddr;

err_shmat:
	shmdt(xdata->seginfo.shmaddr);
err_shmget:
	shmctl(xdata->seginfo.shmid, IPC_RMID, NULL);
err_image:
	XDestroyImage(xdata->ximage);
err:
	xdata->seginfo.shmaddr = NULL;
	XSetErrorHandler(old_handler);
	return NULL;
#else
	UNUSED(xdata);
	UNUSED(width);
	UNUSED(height);
	return NULL;
#endif
}

void fb_create_window(struct fb_data* data, unsigned width, unsigned height, const char* name)
{
	struct x11_data *xdata = calloc(1, sizeof(struct x11_data));
	if (xdata == NULL)
	{
		return;
	}

	data->winsys_data = xdata;
	++window_count;
	if (dsp == NULL)
	{
		init_keycodes();
		dsp = XOpenDisplay(NULL);
		if (dsp == NULL) {
			printf("Could not open a connection to the X server\n");
			free(data->winsys_data);
			data->winsys_data = NULL;
			return;
		}

		if (keycodemap == NULL)
		{
			XDisplayKeycodes(dsp, &min_keycode, &max_keycode);
			x11_update_keymap();
		}

		/* try to get the bits per pixel value */
		int nfmts;
		XPixmapFormatValues *fmts = XListPixmapFormats(dsp, &nfmts);
		if (fmts == NULL)
		{
			/* no memory, go away */
			return;
		}

		for (int i = 0; i < nfmts; ++i)
		{
			if (fmts[i].depth == DefaultDepth(dsp, DefaultScreen(dsp)))
			{
				bpp = fmts[i].bits_per_pixel;
				break;
			}
		}

		XFree(fmts);

		if (bpp != 16 && bpp != 32)
		{
			printf("Error, depth %d is not supported\n", bpp);
			goto err;
		}
	}

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

	data->framebuffer = x11_xshm_init(xdata, width, height);
	if (data->framebuffer == NULL)
	{
		xdata->ximage = XCreateImage(dsp, DefaultVisual(dsp, DefaultScreen(dsp)),
			DefaultDepth(dsp, DefaultScreen(dsp)), ZPixmap, 0,
			NULL, width, height, 8, 0);
		xdata->ximage->data = calloc(xdata->ximage->bytes_per_line, xdata->ximage->height);
		if (xdata->ximage->data == NULL)
		{
			goto err;
		}

		if (bpp != 32) {
			data->framebuffer = calloc(4, (size_t)width * height);
			if (data->framebuffer == NULL)
			{
				goto err;
			}
		} else {
			data->framebuffer = xdata->ximage->data;
		}
	}

	XSync(dsp, False);
	return;
err:
	fb_close_window(data);
	return;
}

void fb_close_window(struct fb_data *data)
{
	struct x11_data *xdata = data->winsys_data;
	if (keycodemap != NULL)
	{
		XFree(keycodemap);
		keycodemap = NULL;
	}

	if (bpp != 32 && data->framebuffer != NULL)
	{
		free(data->framebuffer);
		data->framebuffer = NULL;
	}

	if (dsp == NULL)
	{
		goto free_xdata;
	}

#ifdef USE_XSHM
	if (xdata->seginfo.shmaddr != NULL)
	{
		XShmDetach(dsp, &xdata->seginfo);
		shmdt(xdata->seginfo.shmaddr);
		shmctl(xdata->seginfo.shmid, IPC_RMID, NULL);
		XDestroyImage(xdata->ximage);
		xdata->seginfo.shmaddr = NULL;
		data->framebuffer = NULL;
	}
	else
#endif
	{
		if (xdata->ximage->data != NULL)
		{
			free(xdata->ximage->data);
			xdata->ximage->data = NULL;
			data->framebuffer = NULL;
		}
		XDestroyImage(xdata->ximage);
	}

	XFreeGC(dsp, xdata->gc);
	XDestroyWindow(dsp, xdata->window);
	if (--window_count == 0)
	{
		XCloseDisplay(dsp);
		dsp = NULL;
	}

free_xdata:
	free(data->winsys_data);
	data->winsys_data = NULL;
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

		if (bpp != 32)
		{
			a8r8g8b8_to_r5g6b5(all_data[i].framebuffer, xdata->ximage->data,
					   (size_t)xdata->ximage->width * xdata->ximage->height);
		}

#ifdef USE_XSHM
		if (xdata->seginfo.shmaddr != NULL)
		{
			XShmPutImage(dsp,
					xdata->window,
					xdata->gc,
					xdata->ximage,
					0, /* src_x */
					0, /* src_y */
					0, /* dst_x */
					0, /* dst_y */
					xdata->ximage->width,
					xdata->ximage->height,
					False /* send_event */);
		}
		else
#endif
		{
			XPutImage(dsp,
					xdata->window,
					xdata->gc,
					xdata->ximage,
					0, /* src_x */
					0, /* src_y */
					0, /* dst_x */
					0, /* dst_y */
					xdata->ximage->width,
					xdata->ximage->height);
		}
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
					struct key k = keysym2makecode(keysym);
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
					struct key k = keysym2makecode(keysym);
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
