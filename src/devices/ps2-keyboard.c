/*
ps2-keyboard.c - PS2 Keyboard
Copyright (C) 2021  cerg2010cerg2010 <github.com/cerg2010cerg2010>

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

#include "ps2-altera.h"
#include "ringbuf.h"
#include "riscv32.h"
#include "rvtimer.h"
#include "ps2-keyboard.h"
#include "spinlock.h"

/* Based on the ps2-mouse.c */
/* TODO: locking? */

#define PS2_CMD_RESET 0xFF
#define PS2_CMD_RESEND 0xFE
#define PS2_CMD_SET_DEFAULTS 0xF6
#define PS2_CMD_DISABLE_DATA_REPORTING 0xF5
#define PS2_CMD_ENABLE_DATA_REPORTING 0xF4
#define PS2_CMD_SET_SAMPLE_RATE 0xF3
#define PS2_CMD_GET_DEV_ID 0xF2
#define PS2_CMD_SET_SCAN_CODE_SET 0xF0
#define PS2_CMD_ECHO 0xEE
#define PS2_CMD_LEDS 0xED

#define PS2_RSP_ACK 0xFA
#define PS2_RSP_NAK 0xFE

/* state specifies what the byte read means */
enum ps2_keyboard_state
{
	STATE_CMD,
	STATE_SET_SAMPLE_RATE,
	STATE_SET_SCAN_CODE_SET,
	STATE_SET_LEDS,
};

struct ps2_keyboard
{
	enum ps2_keyboard_state state;
	struct key lastkey; // last key pressed, used for typematic input
	rvtimer_t sample_timer; // used in IRQ handling for typematic (repeated) input
	uint8_t rate : 5; // typematic rate in command encoding
	uint8_t delay : 2; // typematic delay in command encoding (0.25sec * (delay + 1))
	bool reporting : 1; // data reporting enabled; needed for STATUS command

	// TODO: disable typematic, make, break keycodes

	struct ringbuf cmdbuf;
	spinlock_t lock;
};

static void ps2_set_sample_rate(struct ps2_keyboard *dev, uint8_t rate)
{
	dev->rate = rate & 0x1f;
	dev->delay = rate & 3;

	rvtimer_init(&dev->sample_timer, dev->delay + 1);
	dev->sample_timer.timecmp = 1; /* one sample */
}

static void ps2_defaults(struct ps2_keyboard *dev)
{
	dev->rate = 20;
	dev->delay = 1;
}

static bool ps2_cmd_set_defaults(struct ps2_keyboard *dev)
{
	ps2_defaults(dev);
	ringbuf_put_u8(&dev->cmdbuf, PS2_RSP_ACK);
	return true;
}

static bool ps2_cmd_reset(struct ps2_keyboard *dev)
{
	ps2_cmd_set_defaults(dev);
	ringbuf_put_u8(&dev->cmdbuf, 0xAA);
	return true;
}

static bool ps2_cmd_resend(struct ps2_keyboard *dev)
{
	/* TODO: remember the ringbuf state, save it, then send it back */
	UNUSED(dev);
	return false;
}

static bool ps2_cmd_disable_data_reporting(struct ps2_keyboard *dev)
{
	dev->reporting = false;
	ps2_defaults(dev);
	ringbuf_put_u8(&dev->cmdbuf, PS2_RSP_ACK);
	return true;
}

static bool ps2_cmd_enable_data_reporting(struct ps2_keyboard *dev)
{
	dev->reporting = true;
	ringbuf_put_u8(&dev->cmdbuf, PS2_RSP_ACK);
	return true;
}

static bool ps2_cmd_set_sample_rate(struct ps2_keyboard *dev)
{
	dev->state = STATE_SET_SAMPLE_RATE;
	ringbuf_put_u8(&dev->cmdbuf, PS2_RSP_ACK);
	return true;
}

static bool ps2_cmd_get_dev_id(struct ps2_keyboard *dev)
{
	ringbuf_put_u8(&dev->cmdbuf, PS2_RSP_ACK);
	ringbuf_put_u8(&dev->cmdbuf, 0xAB);
	ringbuf_put_u8(&dev->cmdbuf, 0x83);
	return true;
}

static bool ps2_cmd_set_scan_code_set(struct ps2_keyboard *dev)
{
	dev->state = STATE_SET_SCAN_CODE_SET;
	ringbuf_put_u8(&dev->cmdbuf, PS2_RSP_ACK);
	return true;
}

static bool ps2_cmd_echo(struct ps2_keyboard *dev)
{
	ringbuf_put_u8(&dev->cmdbuf, 0xEE);
	return true;
}

static bool ps2_cmd_leds(struct ps2_keyboard *dev)
{
	dev->state = STATE_SET_LEDS;
	ringbuf_put_u8(&dev->cmdbuf, PS2_RSP_ACK);
	return true;
}

static uint16_t ps2_keyboard_op(struct ps2_device *ps2dev, uint8_t *val, bool is_write)
{
	struct ps2_keyboard *dev = (struct ps2_keyboard*)ps2dev->data;
	if (is_write)
	{
		//printf("ps2 kbd cmd sent: %02x\n", (int)*val);
		spin_lock(&dev->lock);
		bool ret = false;
		switch (dev->state)
		{
			case STATE_CMD:
				/* go to the command switch */
				break;
			case STATE_SET_SAMPLE_RATE:
				ps2_set_sample_rate(dev, *val);
				dev->state = STATE_CMD;
				ringbuf_put_u8(&dev->cmdbuf, PS2_RSP_ACK);
				goto out;
			case STATE_SET_SCAN_CODE_SET:
				if (*val == 0)
				{
					ringbuf_put_u8(&dev->cmdbuf, PS2_RSP_ACK);
					ringbuf_put_u8(&dev->cmdbuf, 2);
				}
				else if (*val == 2)
				{
					ringbuf_put_u8(&dev->cmdbuf, PS2_RSP_ACK);
				}
				else
				{
					ringbuf_put_u8(&dev->cmdbuf, PS2_RSP_NAK);
				}
				dev->state = STATE_CMD;
				ringbuf_put_u8(&dev->cmdbuf, PS2_RSP_ACK);
				goto out;
			case STATE_SET_LEDS:
				/* leds are ignored */
				dev->state = STATE_CMD;
				ringbuf_put_u8(&dev->cmdbuf, PS2_RSP_ACK);
				goto out;
		}

		switch (*val)
		{
			case PS2_CMD_RESET: ret = ps2_cmd_reset(dev); break;
			case PS2_CMD_RESEND: ret = ps2_cmd_resend(dev); break;
			case PS2_CMD_SET_DEFAULTS: ret = ps2_cmd_set_defaults(dev); break;
			case PS2_CMD_DISABLE_DATA_REPORTING: ret = ps2_cmd_disable_data_reporting(dev); break;
			case PS2_CMD_ENABLE_DATA_REPORTING: ret = ps2_cmd_enable_data_reporting(dev); break;
			case PS2_CMD_SET_SAMPLE_RATE: ret = ps2_cmd_set_sample_rate(dev); break;
			case PS2_CMD_GET_DEV_ID: ret = ps2_cmd_get_dev_id(dev); break;
			case PS2_CMD_SET_SCAN_CODE_SET: ret = ps2_cmd_set_scan_code_set(dev); break;
			case PS2_CMD_ECHO: ret = ps2_cmd_echo(dev); break;
			case PS2_CMD_LEDS: ret = ps2_cmd_leds(dev); break;
			default:
					   ringbuf_put_u8(&dev->cmdbuf, PS2_RSP_NAK);
					   ret = true;
					   break;
		}

out:
		altps2_interrupt_unlocked(ps2dev);
		spin_unlock(&dev->lock);
		return ret;
	}
	else
	{
		spin_lock(&dev->lock);
		size_t avail = dev->cmdbuf.consumed;
		if (avail == 0)
		{
			*val = '\0';
			goto out2;
		}

		ringbuf_get_u8(&dev->cmdbuf, val);
		//printf("ps2 kbd cmd resp: %02x avail: %d\n", *val, (int)avail);
out2:
		spin_unlock(&dev->lock);
		return (uint16_t)avail;
	}
}

struct ps2_device ps2_keyboard_create()
{
	struct ps2_keyboard *ptr = calloc(1, sizeof(struct ps2_keyboard));
	struct ps2_device dev;
	dev.ps2_op = ps2_keyboard_op;
	dev.data = ptr;
	/* big number is needed because it can overrun when interrupts aren't delivered
	 * a long time */
	ringbuf_create(&ptr->cmdbuf, 1024);
	spin_init(&ptr->lock);
	ps2_cmd_reset(ptr);

	/* consume first ACK from command */
	uint8_t ack;
	ringbuf_get_u8(&ptr->cmdbuf, &ack);
	assert(ack == PS2_RSP_ACK);

	return dev;
}

static void ps2_handle_typematic(struct ps2_device *ps2keyboard)
{
	struct ps2_keyboard *dev = (struct ps2_keyboard *)ps2keyboard->data;

	if (!dev->reporting)
	{
		/* key reporting is disabled */
		return;
	}

	if (dev->lastkey.len == 0)
	{
		/* no keys are pressed */
		return;
	}

	static const uint16_t rate2realrate[32] =
	{
		[0] = 20,
		[1] = 21,
		[2] = 23,
		[3] = 25,
		[4] = 28,
		[5] = 30,
		[6] = 33,
		[7] = 37,
		[8] = 40,
		[9] = 43,
		[10] = 46,
		[11] = 50,
		[12] = 55,
		[13] = 60,
		[14] = 67,
		[15] = 75,
		[16] = 80,
		[17] = 86,
		[18] = 92,
		[19] = 100,
		[20] = 109,
		[21] = 120,
		[22] = 133,
		[23] = 150,
		[24] = 160,
		[25] = 171,
		[26] = 185,
		[27] = 200,
		[28] = 218,
		[29] = 240,
		[30] = 267,
		[31] = 300
	};

	if (!rvtimer_pending(&dev->sample_timer))
	{
		return;
	}

	//printf("typematic!11 timecmp: %ld freq: %ld\n", dev->sample_timer.timecmp, dev->sample_timer.freq);

	/* update timer to the next rate tick */
	rvtimer_init(&dev->sample_timer, rate2realrate[dev->rate]);
	dev->sample_timer.timecmp = 10;

	ringbuf_put(&dev->cmdbuf, dev->lastkey.keycode, dev->lastkey.len);
	altps2_interrupt(ps2keyboard);
}

// Send key to the PS/2 keyboard
// This functions accepts codes as PS/2 makecodes
// if pressed is false then it will try to make break code from make code and send it
// Set key to NULL to handle typematic repeat
void ps2_handle_keyboard(struct ps2_device *ps2keyboard, struct key *key, bool pressed)
{
	struct ps2_keyboard *dev = (struct ps2_keyboard *)ps2keyboard->data;
	spin_lock(&dev->lock);

	if (key == NULL)
	{
		ps2_handle_typematic(ps2keyboard);
		goto out;
	}

	if (!dev->reporting)
	{
		/* key reporting is disabled */
		goto out;
	}

	uint8_t keycmd[KEY_SIZE];
	uint8_t keylen;
	if (pressed)
	{
		/* no other key types are known... */
		assert(key->len == 1 || key->len == 2 || key->len == 8);

		keylen = dev->lastkey.len = key->len * sizeof(key->keycode[0]) > KEY_SIZE
			? KEY_SIZE
			: key->len * sizeof(key->keycode[0]);
		memcpy(keycmd, key->keycode, keylen);

		memcpy(dev->lastkey.keycode, key->keycode, keylen);
		rvtimer_init(&dev->sample_timer, 4 - dev->delay);
		dev->sample_timer.timecmp = 1;
	}
	else
	{
		/* try to make the break code */
		/* this is for scan set 2 */

		//memset(dev->lastkey, '\0', KEY_SIZE);
		dev->lastkey.len = 0;

		if (key->len == 1)
		{
			keycmd[0] = 0xF0;
			keycmd[1] = key->keycode[0];
			keylen = 2;
		}
		else if (key->len == 2 && key->keycode[0] == 0xE0)
		{
			keycmd[0] = 0xE0;
			keycmd[1] = 0xF0;
			keycmd[2] = key->keycode[1];
			keylen = 3;
		}
		else if (key->len == 4
				&& key->keycode[0] == 0xE0
				&& key->keycode[2] == 0xE0)
		{
			/* print screen is special */
			keycmd[0] = 0xE0;
			keycmd[1] = 0xF0;
			keycmd[2] = key->keycode[3];
			keycmd[3] = 0xE0;
			keycmd[4] = 0xF0;
			keycmd[5] = key->keycode[1];
			keylen = 6;
		}
		else if (key->len == 8 && key->keycode[0] == 0xE1)
		{
			/* pause key, ignore */
			goto out;
		}
		else
		{
			fputs("PS2 keyboard: unknown make code ", stderr);
			for (size_t i = 0; i < key->len; ++i)
			{
				fprintf(stderr, "%02X ", key->keycode[i]);
			}
			fputs("\n", stderr);
			goto out;
		}
	}

	ringbuf_put(&dev->cmdbuf, keycmd, keylen);
	altps2_interrupt(ps2keyboard);
out:
	spin_unlock(&dev->lock);
}
