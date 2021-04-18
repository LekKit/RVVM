#include "ps2-altera.h"
#include "ringbuf.h"
#include "riscv32.h"
#include "ps2-mouse.h"

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

	enum ps2_mouse_scale scale;
	enum ps2_mouse_mode mode;
	enum ps2_mouse_state state;
	uint8_t resolution; // in pow2, e.g. 2 means multiply by 4
	uint8_t rate; // in samples per second; unused for now
	bool reporting; // data reporting enabled; needed for STATUS command

	struct ringbuf cmdbuf;
};

static uint8_t ps2_scale_coord(enum ps2_mouse_scale scale, uint8_t n)
{
	switch (scale)
	{
		case SCALE_1_1: return n;
		case SCALE_2_1:
				switch (n)
				{
					case 0:
					case 1:
					case 3:
						return n;
					case 2: return 1;
					case 4: return 6;
					case 5: return 9;
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
	uint8_t x = dev->xctr & 0xff;
	uint8_t xsign = dev->xctr < 0;
	uint8_t y = dev->yctr & 0xff;
	uint8_t ysign = dev->yctr < 0;

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

static void ps2_defaults(struct ps2_mouse *dev)
{
	dev->scale = SCALE_1_1;
	dev->mode = MODE_STREAM;
	dev->state = STATE_CMD;
	dev->reporting = false;
	dev->resolution = 2;
	dev->rate = 100;
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
	ringbuf_put_u8(&dev->cmdbuf, '\0'); // linux requires additional byte
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
	dev->state = STATE_SET_SAMPLE_RATE;
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
		riscv32_debug_always("ps2 mice cmd sent: %h\n", *val);
		bool ret = false;
		switch (dev->state)
		{
			case STATE_CMD:
				/* go to the command switch */
				break;
			case STATE_SET_SAMPLE_RATE:
				dev->rate = *val;
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
		altps2_interrupt(ps2dev);
		return ret;
	}
	else
	{
		size_t avail = dev->cmdbuf.consumed;
		if (avail == 0)
		{
			*val = '\0';
			return 0;
		}

		ringbuf_get_u8(&dev->cmdbuf, val);
		riscv32_debug_always("ps2 mice cmd resp: %h avail: %h\n", *val, (uint16_t)avail);
		return (uint16_t)(avail - 1);
	}
}

struct ps2_device ps2_mouse_create()
{
	struct ps2_mouse *ptr = calloc(1, sizeof(struct ps2_mouse));
	struct ps2_device dev;
	dev.ps2_op = ps2_mouse_op;
	dev.data = ptr;
	ringbuf_create(&ptr->cmdbuf, 256);
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

	if (x == 0 && y == 0
			&& (btns == NULL
				|| (btns->left == dev->btns.left
					&& btns->middle == dev->btns.middle
					&& btns->right == dev->btns.right)))
	{
		/* nothing to do */
		return;
	}

	if (btns)
	{
		dev->btns = *btns;
	}

	int32_t newx = dev->xctr + (x << dev->resolution);
	int32_t newy = dev->yctr + (y << dev->resolution);

	if (newx > 0xff || newx < -0x100)
	{
		dev->xoverflow = true;
		newx %= 0xff;
	}

	if (newy > 0xff || newy < -0x100)
	{
		dev->yoverflow = true;
		newy %= 0xff;
	}

	dev->xctr = newx;
	dev->yctr = newy;

	if (dev->mode != MODE_STREAM || !dev->reporting)
	{
		return;
	}

	ps2_push_move_pkt(dev);
	ps2_reset_counters(dev);
	altps2_interrupt(ps2mouse);
}

