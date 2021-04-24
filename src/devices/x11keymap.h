#ifndef X11KEYMAP_H
#define X11KEYMAP_H

#ifdef USE_X11
#include <riscv.h>

struct key x11keysym2makecode(uint16_t xkeysym);

#endif

#endif
