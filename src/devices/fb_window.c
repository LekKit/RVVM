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

