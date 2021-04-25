/*
fb_window.c - Framebuffer window device
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

#include "riscv32.h"
#include "riscv32_mmu.h"
#include "fb_window.h"

static bool fb_mmio_handler(riscv32_vm_state_t* vm, riscv32_mmio_device_t* device, uint32_t offset, void* data, uint32_t size, uint8_t op)
{
	char* devptr = ((char*)device->data) + offset;
	char* dataptr = (char*)data;

	char *destptr;
	const char *srcptr;

	UNUSED(vm);
	if (op == MMU_WRITE) {
		destptr = devptr;
		srcptr = dataptr;
	} else {
		destptr = dataptr;
		srcptr = devptr;
	}

	memcpy(destptr, srcptr, size);
	return true;
}

void init_fb(riscv32_vm_state_t* vm, struct fb_data *data, unsigned width, unsigned height, uint32_t addr, struct ps2_device *mouse, struct ps2_device *keyboard)
{
    data->framebuffer = malloc(width * height * 4);
    data->mouse = mouse;
    data->keyboard = keyboard;
    riscv32_mmio_add_device(vm, addr, addr + (width * height * 4), fb_mmio_handler, data->framebuffer);
    fb_create_window(data, width, height, "RVVM");
}
