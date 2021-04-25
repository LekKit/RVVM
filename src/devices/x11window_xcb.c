/*
x11window.c - X11 VM Window, XCB backend
Copyright (C) 2021  cerg2010cerg2010 <github.com/cerg2010cerg2010>

Based on Xlib backend code by:
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

#if defined(USE_X11) && defined(USE_XCB)

#include "riscv32.h"
#include "fb_window.h"
#include "ps2-mouse.h"
#include "ps2-keyboard.h"
#include "x11keymap.h"

#include <string.h>
#include <xcb/xcb.h>
#include <xcb/xcb_icccm.h>

struct x11_data
{
	struct mouse_btns btns;
	int x, y;
	uint8_t* local_data;
	xcb_window_t win;
	xcb_gcontext_t gc;
	xcb_pixmap_t ximage;
	uint16_t width;
	uint16_t height;
	uint8_t depth;
};

static xcb_connection_t *connection = NULL;
static int prefscreen;
static xcb_keysym_t *keycodemap = NULL;
static int keycodemap_len; // in xcb_keysym_t
static xcb_keycode_t min_keycode;
static xcb_keycode_t max_keycode;
static uint8_t keysyms_per_keycode;
static size_t window_count;

static void x11_update_keymap()
{
	xcb_get_keyboard_mapping_reply_t *kbmap_ret = xcb_get_keyboard_mapping_reply(connection,
			xcb_get_keyboard_mapping(connection,
				min_keycode,
				max_keycode - min_keycode + 1),
			NULL);
	if (kbmap_ret == NULL)
	{
		printf("Unable to get X keyboard mapping\n");
		keycodemap = NULL;
		return;
	}

	keysyms_per_keycode = kbmap_ret->keysyms_per_keycode;
	xcb_keysym_t *keycodemap_ret = xcb_get_keyboard_mapping_keysyms(kbmap_ret);
	keycodemap_len = xcb_get_keyboard_mapping_keysyms_length(kbmap_ret);
	if (keycodemap != NULL)
	{
		free(keycodemap);
	}
	keycodemap = malloc(sizeof(xcb_keysym_t) * keycodemap_len);
	memcpy(keycodemap, keycodemap_ret, sizeof(xcb_keysym_t) * keycodemap_len);
	free(kbmap_ret);
}

void fb_create_window(struct fb_data* data, unsigned width, unsigned height, const char* name)
{
	++window_count;

	struct x11_data *xdata = malloc(sizeof(struct x11_data));
	xcb_generic_error_t *err;
	data->winsys_data = xdata;
	xdata->local_data = (uint8_t*)data->framebuffer;//malloc(4 * width * height);

	/* connect to the X server */
	if (connection == NULL)
	{
		connection = xcb_connect(NULL, &prefscreen);
		if (connection == NULL)
		{
			printf("Could not open a connection to the X server\n");
			return;
		}
	}

	/* get the prefrred screen */
	const xcb_setup_t *setup = xcb_get_setup(connection);
	xcb_screen_iterator_t scr_iter = xcb_setup_roots_iterator(setup);
	for (int i = 0; i < prefscreen; ++i)
	{
		xcb_screen_next(&scr_iter);
	}
	xcb_screen_t *screen = scr_iter.data;

	/* get keyboard mapping */
	if (keycodemap == NULL)
	{
		min_keycode = setup->min_keycode;
		max_keycode = setup->max_keycode;
		x11_update_keymap();
	}

	/* create window */
	xdata->win = xcb_generate_id(connection);
	uint32_t crwin_values[] = {
		XCB_EVENT_MASK_KEY_PRESS
			| XCB_EVENT_MASK_KEY_RELEASE
			| XCB_EVENT_MASK_BUTTON_PRESS
			| XCB_EVENT_MASK_BUTTON_RELEASE
			| XCB_EVENT_MASK_POINTER_MOTION
			| XCB_EVENT_MASK_BUTTON_MOTION
	};
	xcb_create_window(connection,
			XCB_COPY_FROM_PARENT,
			xdata->win,
			screen->root,
			0, /* x */
			0, /* y */
			width,
			height,
			0, /* border_with */
			XCB_WINDOW_CLASS_INPUT_OUTPUT,
			XCB_COPY_FROM_PARENT, /* visual */
			XCB_CW_EVENT_MASK,
			crwin_values);
	xcb_change_property(connection, XCB_PROP_MODE_REPLACE, xdata->win, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8, strlen(name), name);
	xcb_size_hints_t hints = {
		.flags = XCB_ICCCM_SIZE_HINT_P_MAX_SIZE | XCB_ICCCM_SIZE_HINT_P_MIN_SIZE,
		.min_width = width, .min_height = height,
		.max_width = width, .max_height = height,
	};
	//xcb_icccm_set_wm_normal_hints(connection, xdata->win, &hints);
	xcb_change_property(connection, XCB_PROP_MODE_REPLACE, xdata->win, XCB_ATOM_WM_NORMAL_HINTS, XCB_ATOM_WM_SIZE_HINTS, 32, sizeof(hints) >> 2, &hints);
	xcb_map_window(connection, xdata->win);

	/* create graphics context */
	xdata->gc = xcb_generate_id(connection);
	if ((err = xcb_request_check(connection, xcb_create_gc_checked(connection, xdata->gc, xdata->win, 0, NULL))))
	{
		printf("Error in xcb_create_gc, code: %d\n", err->error_code);
		goto err;
	}

	/* create pixmap */
	xdata->width = width;
	xdata->height = height;
	xdata->depth = screen->root_depth;
	xdata->ximage = xcb_generate_id(connection);
	if ((err = xcb_request_check(connection, xcb_create_pixmap_checked(connection, xdata->depth, xdata->ximage, xdata->win, width, height))))
	{
		printf("Error in xcb_create_pixmap, code: %d\n", err->error_code);
		goto err;
	}

	/* flush events */
	if (xcb_flush(connection) <= 0)
	{
		printf("Error initializing X11\n");
		goto err;
	}

	return;
err:
	if (keycodemap != NULL)
	{
		free(keycodemap);
		keycodemap = NULL;
	}

	connection = NULL;
	return;
}

void fb_close_window(struct fb_data *data)
{
	struct x11_data *xdata = data->winsys_data;
	if (keycodemap != NULL)
	{
		free(keycodemap);
		keycodemap = NULL;
	}
	xcb_free_pixmap(connection, xdata->ximage);
	xcb_free_gc(connection, xdata->gc);
	xcb_destroy_window(connection, xdata->win);
	if (--window_count == 0)
	{
		xcb_disconnect(connection);
	}
}

struct ev_queue
{
	xcb_generic_event_t *prev;
	xcb_generic_event_t *curr;
	xcb_generic_event_t *next;
};

xcb_generic_event_t* poll_for_event(xcb_connection_t *conn, struct ev_queue *q)
{
	if (q->prev != NULL)
	{
		free(q->prev);
	}
	else if (q->curr == NULL)
	{
		q->curr = xcb_poll_for_event(conn);
		q->next = xcb_poll_for_queued_event(conn);
		return q->curr;
	}

	if (q->next == NULL)
	{
		/* final event is handled, cleanup */
		free(q->curr);
		q->prev = NULL;
		q->curr = NULL;
		return q->curr;
	}

	q->prev = q->curr;
	q->curr = q->next;
	q->next = xcb_poll_for_queued_event(conn);
	return q->curr;
}

#define GET_DATA_FOR_WINDOW(data, all_data, nfbs, xevent) \
	do { \
		for (size_t i = 0; i < nfbs; ++i) \
		{ \
			struct x11_data *tmpdata = all_data[i].winsys_data; \
			if (tmpdata != NULL && tmpdata->win == xevent->event) \
			{ \
				data = &all_data[i]; \
				break; \
			} \
		} \
	} while (0)


void fb_update(struct fb_data *all_data, size_t nfbs)
{
	if (connection == NULL)
	{
		return;
	}

	for (size_t i = 0; i < nfbs; ++i)
	{
		struct x11_data *xdata = all_data[i].winsys_data;
		xcb_generic_error_t *err;
		if ((err = xcb_request_check(connection, xcb_put_image_checked(connection,
							XCB_IMAGE_FORMAT_Z_PIXMAP,
							xdata->win,
							xdata->gc,
							xdata->width,
							xdata->height,
							0, /* dst_x */
							0, /* dst_y */
							0, /* left_pad */
							xdata->depth,
							4 * xdata->width * xdata->height,
							xdata->local_data))))
		{
			printf("err in put_image, code: %d\n", err->error_code);
		}
	}
	xcb_flush(connection);

	struct ev_queue q = { };

	for (xcb_generic_event_t *ev; (ev = poll_for_event(connection, &q));)
	{
		switch (ev->response_type & ~0x80)
		{
			case XCB_KEY_PRESS:
				{
					xcb_key_press_event_t *xkey = (xcb_key_press_event_t *) ev;
					struct fb_data *data = NULL;
					GET_DATA_FOR_WINDOW(data, all_data, nfbs, xkey);
					if (data == NULL) break;

					xcb_keysym_t keysym = keycodemap[(xkey->detail - min_keycode) * keysyms_per_keycode];
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
			case XCB_KEY_RELEASE:
				{
					xcb_key_release_event_t *xkey = (xcb_key_press_event_t *) ev;
					struct fb_data *data = NULL;
					GET_DATA_FOR_WINDOW(data, all_data, nfbs, xkey);
					if (data == NULL) break;

					if (q.next != NULL && (q.next->response_type & ~0x80) == XCB_KEY_PRESS)
					{
						xcb_key_press_event_t *nxkey = (xcb_key_press_event_t *) q.next;
						if (nxkey->time == xkey->time && nxkey->detail == xkey->detail)
						{
							/* skip the fake key press/release event */
							poll_for_event(connection, &q);
							break;
						}
					}

					xcb_keysym_t keysym = keycodemap[(xkey->detail - min_keycode) * keysyms_per_keycode];
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
			case XCB_BUTTON_PRESS:
				{
					xcb_button_press_event_t *xbutton = (xcb_button_press_event_t *) ev;
					struct fb_data *data = NULL;
					GET_DATA_FOR_WINDOW(data, all_data, nfbs, xbutton);
					if (data == NULL) break;
					struct x11_data *xdata = data->winsys_data;

					if (xbutton->detail == XCB_BUTTON_INDEX_1)
					{
						xdata->btns.left = true;
					}
					else if (xbutton->detail == XCB_BUTTON_INDEX_2)
					{
						xdata->btns.middle = true;
					}
					else if (xbutton->detail == XCB_BUTTON_INDEX_3)
					{
						xdata->btns.right = true;
					}
					ps2_handle_mouse(data->mouse, 0, 0, &xdata->btns);
					break;
				}
			case XCB_BUTTON_RELEASE:
				{
					xcb_button_release_event_t *xbutton = (xcb_button_press_event_t *) ev;
					struct fb_data *data = NULL;
					GET_DATA_FOR_WINDOW(data, all_data, nfbs, xbutton);
					if (data == NULL) break;
					struct x11_data *xdata = data->winsys_data;

					if (xbutton->detail == XCB_BUTTON_INDEX_1)
					{
						xdata->btns.left = false;
					}
					else if (xbutton->detail == XCB_BUTTON_INDEX_2)
					{
						xdata->btns.middle = false;
					}
					else if (xbutton->detail == XCB_BUTTON_INDEX_3)
					{
						xdata->btns.right = false;
					}
					ps2_handle_mouse(data->mouse, 0, 0, &xdata->btns);
					break;
				}
			case XCB_MOTION_NOTIFY:
				{
					xcb_motion_notify_event_t *xmotion = (xcb_motion_notify_event_t *) ev;
					struct fb_data *data = NULL;
					GET_DATA_FOR_WINDOW(data, all_data, nfbs, xmotion);
					if (data == NULL) break;
					struct x11_data *xdata = data->winsys_data;

					ps2_handle_mouse(data->mouse, xmotion->event_x - xdata->x, -(xmotion->event_y - xdata->y), &xdata->btns);
					xdata->x = xmotion->event_x;
					xdata->y = xmotion->event_y;
					break;
				}
			case XCB_MAPPING_NOTIFY:
				{
					xcb_mapping_notify_event_t *xmapping = (xcb_mapping_notify_event_t *) ev;
					if (xmapping->request != XCB_MAPPING_KEYBOARD)
					{
						/* not interested in others */
						break;
					}

					min_keycode = xmapping->first_keycode;
					max_keycode = xmapping->first_keycode + xmapping->count - 1;
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
