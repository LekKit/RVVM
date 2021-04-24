#include "ps2-keyboard.h"
#include "x11keymap.h"
#include <X11/keysym.h>

#define KEY(keysym, ...) [keysym] = { .keycode = { __VA_ARGS__}, .len = sizeof((uint8_t[]) { __VA_ARGS__ }) }

#pragma pack(push,1)
static const struct {
	uint8_t keycode[2];
	uint8_t len : 2;
} x11keymap[] = {
	KEY(XK_a, 0x1C),
	KEY(XK_b, 0x32),
	KEY(XK_c, 0x21),
	KEY(XK_d, 0x23),
	KEY(XK_e, 0x24),
	KEY(XK_f, 0x2B),
	KEY(XK_g, 0x34),
	KEY(XK_h, 0x33),
	KEY(XK_i, 0x43),
	KEY(XK_j, 0x3B),
	KEY(XK_k, 0x42),
	KEY(XK_l, 0x4B),
	KEY(XK_m, 0x3A),
	KEY(XK_n, 0x31),
	KEY(XK_o, 0x44),
	KEY(XK_p, 0x4D),
	KEY(XK_q, 0x15),
	KEY(XK_r, 0x2D),
	KEY(XK_s, 0x1B),
	KEY(XK_t, 0x2C),
	KEY(XK_u, 0x3C),
	KEY(XK_v, 0x2A),
	KEY(XK_w, 0x1D),
	KEY(XK_x, 0x22),
	KEY(XK_y, 0x35),
	KEY(XK_z, 0x1A),
	KEY(XK_0, 0x45),
	KEY(XK_1, 0x16),
	KEY(XK_2, 0x1E),
	KEY(XK_3, 0x26),
	KEY(XK_4, 0x25),
	KEY(XK_5, 0x2E),
	KEY(XK_6, 0x36),
	KEY(XK_7, 0x3D),
	KEY(XK_8, 0x3E),
	KEY(XK_9, 0x46),
	KEY(XK_grave, 0x0E),
	KEY(XK_minus, 0x4E),
	KEY(XK_equal, 0x55),
	KEY(XK_backslash, 0x5D),
	KEY(XK_BackSpace, 0x66),
	KEY(XK_space, 0x29),
	KEY(XK_Tab, 0x0D),
	KEY(XK_Caps_Lock, 0x58),
	KEY(XK_Shift_L, 0x12),
	KEY(XK_Control_L, 0x14),
	KEY(XK_Super_L, 0xE0, 0x1F),
	KEY(XK_Alt_L, 0x11),
	KEY(XK_Shift_R, 0x59),
	KEY(XK_Control_R, 0xE0, 0x14),
	KEY(XK_Super_R, 0xE0, 0x27),
	KEY(XK_Alt_R, 0xE0, 0x11),
	/* wtf is APPS? If you know, here's the code: 0xE0 0x2F */
	KEY(XK_Return, 0x5A),
	KEY(XK_Escape, 0x76),
	KEY(XK_F1, 0x05),
	KEY(XK_F2, 0x06),
	KEY(XK_F3, 0x04),
	KEY(XK_F4, 0x0C),
	KEY(XK_F5, 0x03),
	KEY(XK_F6, 0x0B),
	KEY(XK_F7, 0x83),
	KEY(XK_F8, 0x0A),
	KEY(XK_F9, 0x01),
	KEY(XK_F10, 0x09),
	KEY(XK_F11, 0x78),
	KEY(XK_F12, 0x07),
	/* XK_Print is too big, handled separately */
	KEY(XK_Scroll_Lock, 0x7E),
	/* XK_Pause is too big, handled separately */
	KEY(XK_bracketleft, 0x54),
	KEY(XK_Insert, 0xE0, 0x70),
	KEY(XK_Home, 0xE0, 0x6C),
	KEY(XK_Page_Up, 0xE0, 0x7D),
	KEY(XK_Delete, 0xE0, 0x71),
	KEY(XK_End, 0xE0, 0x69),
	KEY(XK_Page_Down, 0xE0, 0x7A),
	KEY(XK_Up, 0xE0, 0x75),
	KEY(XK_Left, 0xE0, 0x6B),
	KEY(XK_Down, 0xE0, 0x72),
	KEY(XK_Right, 0xE0, 0x74),
	KEY(XK_Num_Lock, 0x77),
	KEY(XK_KP_Divide, 0xE0, 0x4A),
	KEY(XK_KP_Multiply, 0x7C),
	KEY(XK_KP_Subtract, 0x7B),
	KEY(XK_KP_Add, 0x79),
	KEY(XK_KP_Enter, 0xE0, 0x5A),
	KEY(XK_KP_Decimal, 0x71), KEY(XK_KP_Delete, 0x71),
	KEY(XK_KP_0, 0x70), KEY(XK_KP_Insert, 0x70),
	KEY(XK_KP_1, 0x69), KEY(XK_KP_End, 0x69),
	KEY(XK_KP_2, 0x72), KEY(XK_KP_Down, 0x72),
	KEY(XK_KP_3, 0x7A), KEY(XK_KP_Page_Down, 0x7A),
	KEY(XK_KP_4, 0x6B), KEY(XK_KP_Left, 0x6B),
	KEY(XK_KP_5, 0x73), KEY(XK_KP_Begin, 0x73),
	KEY(XK_KP_6, 0x74), KEY(XK_KP_Right, 0x74),
	KEY(XK_KP_7, 0x6C), KEY(XK_KP_Home, 0x6C),
	KEY(XK_KP_8, 0x75), KEY(XK_KP_Up, 0x75),
	KEY(XK_KP_9, 0x7D), KEY(XK_KP_Page_Up, 0x7D),
	KEY(XK_bracketright, 0x5B),
	KEY(XK_semicolon, 0x4C),
	KEY(XK_apostrophe, 0x52),
	KEY(XK_comma, 0x41),
	KEY(XK_period, 0x49),
	KEY(XK_slash, 0x4A),
};
#pragma pack(pop)

struct key x11keysym2makecode(uint16_t xkeysym)
{
	if (xkeysym == XK_Pause)
	{
		struct key k = {
			{ 0xE1, 0x14, 0x77, 0xE1, 0xF0, 0x14, 0xF0, 0x77 },
			8,
		};
		return k;
	}
	else if (xkeysym == XK_Print)
	{
		struct key k = {
			{ 0xE0, 0x12, 0xE0, 0x7C },
			4,
		};
		return k;
	}

	struct key k =
	{
		{
			x11keymap[xkeysym].keycode[0],
			x11keymap[xkeysym].keycode[1]
		},
		x11keymap[xkeysym].len
	};
	return k;
}

