#ifndef FB_WINDOW_H
#define FB_WINDOW_H
#include "ps2-altera.h"

/* TODO: pixel format */

struct fb_data
{
	char *framebuffer;
	struct ps2_device *mouse;
	struct ps2_device *keyboard;
	void *winsys_data; // private window system data
};

void init_fb(riscv32_vm_state_t* vm, struct fb_data *data, unsigned width, unsigned height, uint32_t addr, struct ps2_device *mouse, struct ps2_device *keyboard);

#if defined(USE_X11)
void fb_create_window(struct fb_data *data, unsigned width, unsigned height, const char* name);
void fb_close_window(struct fb_data *data);
void fb_update(struct fb_data *data);
#else
/* dummy functions when no window system available */
inline void fb_create_window(struct fb_data* data, unsigned width, unsigned height, const char* name) { }
inline void fb_close_window(struct fb_data *data) { }
inline void fb_update(struct fb_data *data) { }
#endif
#endif

