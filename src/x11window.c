#ifndef _WIN32

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
#include <unistd.h>
#include <stdint.h>

#include "x11window.h"

static Display* dsp = NULL;
static Window window;
static GC gc;
XImage* ximage;

const void* in_data;
char* local_data;

void create_window(const void* data, int width, int height, const char* name)
{
    dsp = XOpenDisplay(NULL);
    if (!dsp) {
        printf("Could not open a connection to the X server\n");
        return;
    }

    XSetWindowAttributes attributes;
    attributes.backing_store = NotUseful;
    window = XCreateWindow(dsp, DefaultRootWindow(dsp),
            0, 0, width, height, 0,
            DefaultDepth(dsp, XDefaultScreen(dsp)),
            InputOutput, CopyFromParent, CWBackingStore, &attributes);
    XStoreName(dsp, window, name);
    XSelectInput(dsp, window, StructureNotifyMask);
    XMapWindow(dsp, window);

    XGCValues xgcvalues;
    xgcvalues.graphics_exposures = False;
    gc = XCreateGC(dsp, window, GCGraphicsExposures, &xgcvalues);

    local_data = data;//malloc(4*width*height);

    ximage = XCreateImage(dsp, XDefaultVisual(dsp, XDefaultScreen(dsp)),
                        DefaultDepth(dsp, XDefaultScreen(dsp)), ZPixmap, 0,
                        local_data, width, height, 8, 0);
    XSync(dsp, False);
    in_data = data;
}

void close_window()
{
    XFreeGC(dsp, gc);
    XDestroyWindow(dsp, window);
    XCloseDisplay(dsp);
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

void update_fb()
{
    if (!dsp || !ximage) return;
    //r5g6b5_to_r8g8b8(in_data, local_data, ximage->width*ximage->height);
    XPutImage(dsp, window, gc, ximage, 0, 0, 0, 0, ximage->width, ximage->height);
    XSync(dsp, False);
}

#endif
