/*
ps2-mouse.c - PS2 Mouse
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
#include "rvvm.h"
#include "riscv.h"
#include "ps2-mouse.h"
#include "rvtimer.h"
#include "spinlock.h"

/* The mouse is a state machine, see enum ps2_mouse_state */
/* TODO: locking? */

#define PS2_CMD_RESET 0xFF
#define PS2_CMD_RESEND 0xFE
#define PS2_CMD_SET_DEFAULTS 0xF6
#define PS2_CMD_DISABLE_DATA_REPORTING 0xF5
#define PS2_CMD_ENABLE_DATA_REPORTING 0xF4
#define PS2_CMD_SET_SAMPLE_RATE 0xF3
#define PS2_CMD_GET_DEV_ID 0xF2
#define PS2_CMD_SET_REMOTE_MODE 0xF0
#define PS2_CMD_SET_WRAP_MODE 0xEE
#define PS2_CMD_RESET_WRAP_MODE 0xEC
#define PS2_CMD_READ_DATA 0xEB
#define PS2_CMD_SET_STREAM_MODE 0xEA
#define PS2_CMD_STATUS_REQ 0xE9
#define PS2_CMD_SET_RESOLUTION 0xE8
#define PS2_CMD_SET_SCALING_2_1 0xE7
#define PS2_CMD_SET_SCALING_1_1 0xE6

#define PS2_RSP_ACK 0xFA
#define PS2_RSP_NAK 0xFE

// can be used to change the behavior of the x/y coordinate
#define TRANSFORM_COORD(n) (n)

/* state specifies what the byte read means */
enum ps2_mouse_state
{
	STATE_CMD,
	STATE_SET_SAMPLE_RATE,
	STATE_WRAP, /* aka ECHO mode */
	STATE_SET_RESOLUTION,
};

enum ps2_mouse_mode
{
	MODE_STREAM,
	MODE_REMOTE,
};

enum ps2_mouse_scale
{
	SCALE_1_1, /* no scaling */
	SCALE_2_1,
};

struct ps2_mouse
{
	// movement counters - these are actually 9-bit
	int16_t xctr;
	int16_t yctr;
	// counters' overflow flags
	bool xoverflow;
	bool yoverflow;

	struct mouse_btns btns;
	rvtimer_t sample_timer; // used in IRQ handling for sample rate

	enum ps2_mouse_scale scale;
	enum ps2_mouse_mode mode;
	enum ps2_mouse_state state;
	uint8_t resolution; // in pow2, e.g. 2 means multiply by 4
	uint8_t rate; // in samples per second
	bool reporting; // data reporting enabled; needed for STATUS command

	struct ringbuf cmdbuf;
	spinlock_t lock;
};

static int8_t ps2_scale_coord(enum ps2_mouse_scale scale, int8_t n)
{
	switch (scale)
	{
		case SCALE_1_1: return n;
		case SCALE_2_1:
				switch (n)
				{
					case 0:
					case 1: case -1:
					case 3: case -3:
						return n;
					case 2: case -2: return 1;
					case 4: case -4: return 6;
					case 5: case -5: return 9;
					default: return n * 2;
				}
		/* make compiler happy */
		default:
				assert(false);
				return n;
	}
}

static void ps2_push_move_pkt(struct ps2_mouse *dev)
{
	int8_t x = dev->xctr & 0xff;
	bool xsign = dev->xctr < 0;
	int8_t y = dev->yctr & 0xff;
	bool ysign = dev->yctr < 0;

	x = ps2_scale_coord(dev->scale, x);
	y = ps2_scale_coord(dev->scale, y);

	ringbuf_put_u8(&dev->cmdbuf,
			   dev->btns.left
			 | dev->btns.right << 1
			 | dev->btns.middle << 2
			 | 1 << 3
			 | xsign << 4
			 | ysign << 5
			 | dev->xoverflow << 6
			 | dev->yoverflow << 7
			 );
	ringbuf_put_u8(&dev->cmdbuf, x);
	ringbuf_put_u8(&dev->cmdbuf, y);
}

static void ps2_set_sample_rate(struct ps2_mouse *dev, uint8_t rate)
{
	dev->rate = rate;
	rvtimer_init(&dev->sample_timer, rate);
	dev->sample_timer.timecmp = 1; /* one sample */
}

static void ps2_defaults(struct ps2_mouse *dev)
{
	dev->scale = SCALE_1_1;
	dev->mode = MODE_STREAM;
	dev->state = STATE_CMD;
	dev->reporting = false;
	dev->resolution = 2;
	ps2_set_sample_rate(dev, 100);
}

static void ps2_reset_counters(struct ps2_mouse *dev)
{
	dev->xctr = 0;
	dev->yctr = 0;
	dev->xoverflow = 0;
	dev->yoverflow = 0;
}

static bool ps2_cmd_set_defaults(struct ps2_mouse *dev)
{
	ps2_defaults(dev);
	ringbuf_put_u8(&dev->cmdbuf, PS2_RSP_ACK);
	return true;
}

static bool ps2_cmd_reset(struct ps2_mouse *dev)
{
	ps2_cmd_set_defaults(dev);

	ringbuf_put_u8(&dev->cmdbuf, 0xAA);
	ringbuf_put_u8(&dev->cmdbuf, 0x00);
	return true;
}

static bool ps2_cmd_resend(struct ps2_mouse *dev)
{
	/* TODO: remember the ringbuf state, save it, then send it back */
	UNUSED(dev);
	return false;
}

static bool ps2_cmd_disable_data_reporting(struct ps2_mouse *dev)
{
	dev->reporting = false;
	ringbuf_put_u8(&dev->cmdbuf, PS2_RSP_ACK);
	return true;
}

static bool ps2_cmd_enable_data_reporting(struct ps2_mouse *dev)
{
	dev->reporting = true;
	ringbuf_put_u8(&dev->cmdbuf, PS2_RSP_ACK);
	return true;
}

static bool ps2_cmd_set_sample_rate(struct ps2_mouse *dev)
{
	dev->state = STATE_SET_SAMPLE_RATE;
	ringbuf_put_u8(&dev->cmdbuf, PS2_RSP_ACK);
	return true;
}

static bool ps2_cmd_get_dev_id(struct ps2_mouse *dev)
{
	ringbuf_put_u8(&dev->cmdbuf, PS2_RSP_ACK);
	ringbuf_put_u8(&dev->cmdbuf, '\0'); // 0x00 - standard PS/2 mouse
	return true;
}

static bool ps2_cmd_set_remote_mode(struct ps2_mouse *dev)
{
	ps2_reset_counters(dev);
	dev->mode = MODE_REMOTE;
	ringbuf_put_u8(&dev->cmdbuf, PS2_RSP_ACK);
	return true;
}

static bool ps2_cmd_set_wrap_mode(struct ps2_mouse *dev)
{
	ps2_reset_counters(dev);
	dev->state = STATE_WRAP;
	ringbuf_put_u8(&dev->cmdbuf, PS2_RSP_ACK);
	return true;
}

static bool ps2_cmd_reset_wrap_mode(struct ps2_mouse *dev)
{
	ps2_reset_counters(dev);
	dev->state = STATE_CMD;
	ringbuf_put_u8(&dev->cmdbuf, PS2_RSP_ACK);
	return true;
}

static bool ps2_cmd_read_data(struct ps2_mouse *dev)
{
	ringbuf_put_u8(&dev->cmdbuf, PS2_RSP_ACK);
	ps2_push_move_pkt(dev);
	ps2_reset_counters(dev);
	return true;
}

static bool ps2_cmd_set_stream_mode(struct ps2_mouse *dev)
{
	ps2_reset_counters(dev);
	dev->mode = MODE_STREAM;
	ringbuf_put_u8(&dev->cmdbuf, PS2_RSP_ACK);
	return true;
}

static bool ps2_cmd_status_req(struct ps2_mouse *dev)
{
	ringbuf_put_u8(&dev->cmdbuf, PS2_RSP_ACK);
	ringbuf_put_u8(&dev->cmdbuf,
			  dev->btns.right
			| dev->btns.middle << 1
			| dev->btns.left << 2
			| (dev->scale == SCALE_2_1) << 4
			| dev->reporting << 5
			| (dev->mode == MODE_REMOTE) << 6
			);
	ringbuf_put_u8(&dev->cmdbuf, dev->resolution);
	ringbuf_put_u8(&dev->cmdbuf, dev->rate);
	return true;
}

static bool ps2_cmd_set_resolution(struct ps2_mouse *dev)
{
	dev->state = STATE_SET_RESOLUTION;
	ringbuf_put_u8(&dev->cmdbuf, PS2_RSP_ACK);
	return true;
}

static bool ps2_cmd_set_scaling_1_1(struct ps2_mouse *dev)
{
	dev->scale = SCALE_1_1;
	ringbuf_put_u8(&dev->cmdbuf, PS2_RSP_ACK);
	return true;
}

static bool ps2_cmd_set_scaling_2_1(struct ps2_mouse *dev)
{
	dev->scale = SCALE_2_1;
	ringbuf_put_u8(&dev->cmdbuf, PS2_RSP_ACK);
	return true;
}

static uint16_t ps2_mouse_op(struct ps2_device *ps2dev, uint8_t *val, bool is_write)
{
	struct ps2_mouse *dev = (struct ps2_mouse*)ps2dev->data;
	if (is_write)
	{
		//printf("ps2 mice cmd sent: %02x\n", (int)*val);
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
			case STATE_WRAP:
				if (*val == PS2_CMD_RESET_WRAP_MODE
						|| *val == PS2_CMD_RESET)
				{
					/* exit wrap mode */
					break;
				}

				ringbuf_put_u8(&dev->cmdbuf, *val);
				goto out;
			case STATE_SET_RESOLUTION:
				dev->resolution = *val;
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
			case PS2_CMD_SET_REMOTE_MODE: ret = ps2_cmd_set_remote_mode(dev); break;
			case PS2_CMD_SET_WRAP_MODE: ret = ps2_cmd_set_wrap_mode(dev); break;
			case PS2_CMD_RESET_WRAP_MODE: ret = ps2_cmd_reset_wrap_mode(dev); break;
			case PS2_CMD_READ_DATA: ret = ps2_cmd_read_data(dev); break;
			case PS2_CMD_SET_STREAM_MODE: ret = ps2_cmd_set_stream_mode(dev); break;
			case PS2_CMD_STATUS_REQ: ret = ps2_cmd_status_req(dev); break;
			case PS2_CMD_SET_RESOLUTION: ret = ps2_cmd_set_resolution(dev); break;
			case PS2_CMD_SET_SCALING_1_1: ret = ps2_cmd_set_scaling_1_1(dev); break;
			case PS2_CMD_SET_SCALING_2_1: ret = ps2_cmd_set_scaling_2_1(dev); break;
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
		//printf("ps2 mice cmd resp: %02x avail: %d\n", *val, (int)avail);
out2:
		spin_unlock(&dev->lock);
		return (uint16_t)avail;
	}
}

struct ps2_device ps2_mouse_create()
{
	struct ps2_mouse *ptr = safe_calloc(1, sizeof(struct ps2_mouse));
	struct ps2_device dev;
	dev.ps2_op = ps2_mouse_op;
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

void ps2_handle_mouse(struct ps2_device *ps2mouse, int x, int y, struct mouse_btns *btns)
{
	x = TRANSFORM_COORD(x);
	y = TRANSFORM_COORD(y);

	struct ps2_mouse *dev = (struct ps2_mouse *)ps2mouse->data;
	spin_lock(&dev->lock);

	if (x == 0 && y == 0
			&& (btns == NULL
				|| (btns->left == dev->btns.left
					&& btns->middle == dev->btns.middle
					&& btns->right == dev->btns.right)))
	{
		/* nothing to do */
		goto out;
	}

	if (btns)
	{
		dev->btns = *btns;
	}

	// 8 counts/mm is the default in Linux, make it report original coordinates
	int shift = 3 - dev->resolution;
	int32_t newx, newy;
	if (shift >= 0)
	{
		newx = dev->xctr + (x >> shift);
		newy = dev->yctr + (y >> shift);
	}
	else
	{
		newx = dev->xctr + (x << -shift);
		newy = dev->yctr + (y << -shift);
	}

	if (newx > 0xff || newx < -0x100)
	{
		dev->xoverflow = true;
		newx = (int8_t)newx;
	}

	if (newy > 0xff || newy < -0x100)
	{
		dev->yoverflow = true;
		newy = (int8_t)newy;
	}

	dev->xctr = newx;
	dev->yctr = newy;
	//printf("mouse move x: %d y: %d shift: %d\n", newx, newy, shift);

	if (dev->mode != MODE_STREAM || !dev->reporting)
	{
		goto out;
	}

	if (!rvtimer_pending(&dev->sample_timer))
	{
		goto out;
	}

	dev->sample_timer.time = 0;
	rvtimer_rebase(&dev->sample_timer);

	ps2_push_move_pkt(dev);
	ps2_reset_counters(dev);
	spin_unlock(&dev->lock);
	altps2_interrupt(ps2mouse);
	return;
out:
	spin_unlock(&dev->lock);
}
