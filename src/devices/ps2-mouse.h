#ifndef PS2_MOUSE_H
#define PS2_MOUSE_H
#include "ps2-altera.h"

struct mouse_btns {
	bool left : 1;
	bool right : 1;
	bool middle : 1;
	/* maybe will add more later */
};

struct ps2_device ps2_mouse_create();
void ps2_handle_mouse(struct ps2_device *ps2mouse, int x, int y, struct mouse_btns *btns);
#endif
