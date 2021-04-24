#ifndef PS2_KEYBOARD_H
#define PS2_KEYBOARD_H
#include "ps2-altera.h"

/* size of one key, in bytes */
#define KEY_SIZE 8

struct key {
	uint8_t keycode[KEY_SIZE];
	uint8_t len;
};

struct ps2_device ps2_keyboard_create();
void ps2_handle_keyboard(struct ps2_device *ps2keyboard, struct key *key, bool pressed);
#endif
