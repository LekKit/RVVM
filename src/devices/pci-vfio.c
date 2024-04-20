/*
pci-vfio.c - VFIO PCI Passthrough
Copyright (C) 2022  LekKit <github.com/LekKit>

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

#include "pci-vfio.h"
#include "compiler.h"
#include "threading.h"
#include "mem_ops.h"
#include "utils.h"

// Check that <linux/vfio.h> include is available
#if defined(__linux__) && defined(USE_VFIO) && !CHECK_INCLUDE(linux/vfio.h)
#warning Disabling USE_VFIO as <linux/vfio.h> is unavailable
#undef USE_VFIO
#endif

#if defined(__linux__) && defined(USE_VFIO)

#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/eventfd.h>

#include <linux/vfio.h>

static void vfio_pci_sysfs_path(char* buffer, size_t size, const char* pci_id, const char* suffix)
{
    size_t len = rvvm_strlcpy(buffer, "/sys/bus/pci/devices/", size);
    len += rvvm_strlcpy(buffer + len, pci_id, size - len);
    len += rvvm_strlcpy(buffer + len, suffix, size - len);
}

static void vfio_unbind_driver(const char* pci_id)
{
    char path[256] = "/sys/bus/pci/devices/";
    vfio_pci_sysfs_path(path, sizeof(path), pci_id, "/driver/unbind");
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    UNUSED(!write(fd, pci_id, rvvm_strlen(pci_id)));
    close(fd);
}

static void vfio_bind_vfio(const char* pci_id)
{
    int fd = open("/sys/bus/pci/drivers/vfio-pci/bind", O_WRONLY | O_CLOEXEC);
    UNUSED(!write(fd, pci_id, rvvm_strlen(pci_id)));
    close(fd);
}

static bool vfio_needs_rebind(const char* pci_id)
{
    char path[256] = {0};
    char driver_path[256] = {0};
    vfio_pci_sysfs_path(path, sizeof(path), pci_id, "/driver");
    if (readlink(path, driver_path, sizeof(driver_path)) < 0) return true;
    return !rvvm_strfind(driver_path, "vfio-pci");
}

static bool vfio_bind(const char* pci_id)
{
    if (vfio_needs_rebind(pci_id)) {
        rvvm_info("Unbinding the device from it's original driver");
        vfio_unbind_driver(pci_id);
        vfio_bind_vfio(pci_id);
    }
    rvvm_info("Host PCI device %s should now be bound to vfio-pci", pci_id);
    return !vfio_needs_rebind(pci_id);
}

static uint32_t vfio_get_iommu_group(const char* pci_id)
{
    char path[256] = {0};
    char group_path[256] = {0};
    vfio_pci_sysfs_path(path, sizeof(path), pci_id, "/iommu_group");
    if (readlink(path, group_path, sizeof(group_path)) < 0) return -1;
    const char* iommu_path = rvvm_strfind(group_path, "/kernel/iommu_groups/");
    if (iommu_path) {
        return str_to_int_dec(iommu_path + rvvm_strlen("/kernel/iommu_groups/"));
    }
    rvvm_error("Invalid VFIO IOMMU group path!");
    return -1;
}

static int vfio_open_group(const char* pci_id)
{
    uint32_t group = vfio_get_iommu_group(pci_id);
    char path[256] = "/dev/vfio/";
    size_t len = rvvm_strlen(path);
    int_to_str_dec(path + len, sizeof(path) - len, group);
    int fd = open(path, O_RDWR | O_CLOEXEC);
    return fd;
}

typedef struct {
    pci_dev_desc_t pci_desc;
    pci_dev_t*     pci_dev;
    thread_ctx_t*  thread;
    int container;
    int group;
    int device;
    int eventfd;
    bool running;
} vfio_dev_t;

static void vfio_unmask_irq(vfio_dev_t* vfio)
{
    struct vfio_irq_set irq_set = {
        .argsz = sizeof(irq_set),
        .flags = VFIO_IRQ_SET_DATA_NONE | VFIO_IRQ_SET_ACTION_UNMASK,
        .index = VFIO_PCI_MSI_IRQ_INDEX,
        .start = 0,
        .count = 1,
    };
    ioctl(vfio->device, VFIO_DEVICE_SET_IRQS, &irq_set);
}

static void vfio_trigger_irq(vfio_dev_t* vfio)
{
    struct vfio_irq_set irq_set = {
        .argsz = sizeof(irq_set),
        .flags = VFIO_IRQ_SET_DATA_NONE | VFIO_IRQ_SET_ACTION_TRIGGER,
        .index = VFIO_PCI_MSI_IRQ_INDEX,
        .start = 0,
        .count = 1,
    };
    if (vfio->device > 0) ioctl(vfio->device, VFIO_DEVICE_SET_IRQS, &irq_set);
}

static void* vfio_irq_thread(void* data)
{
    vfio_dev_t* vfio = data;
    uint8_t buffer[8] = {0};
    vfio_unmask_irq(vfio);
    while (vfio->running) {
        UNUSED(!read(vfio->eventfd, buffer, sizeof(buffer)));
        pci_send_irq(vfio->pci_dev, 0);
    }
    return NULL;
}

static void vfio_bar_remove(rvvm_mmio_dev_t* dev)
{
    UNUSED(dev);
}

static rvvm_mmio_type_t vfio_bar_type = {
    .name = "vfio_bar",
    .remove = vfio_bar_remove,
};

static void vfio_dev_free(vfio_dev_t* vfio)
{
    for (size_t i=0; i<PCI_FUNC_BARS; ++i) {
        void*  bar_ptr = vfio->pci_desc.func[0].bar[i].data;
        size_t bar_size = vfio->pci_desc.func[0].bar[i].size;
        if (bar_size) munmap(bar_ptr, bar_size);
    }
    vfio->running = false;
    vfio_trigger_irq(vfio);
    thread_join(vfio->thread);
    if (vfio->eventfd > 0)   close(vfio->eventfd);
    if (vfio->device > 0)    close(vfio->device);
    if (vfio->group > 0)     close(vfio->group);
    if (vfio->container > 0) close(vfio->container);
    free(vfio);
}

static void vfio_dev_remove(rvvm_mmio_dev_t* dev)
{
    vfio_dev_t* vfio = dev->data;
    vfio_dev_free(vfio);
}

static rvvm_mmio_type_t vfio_dev_type = {
    .name = "vfio_pci_dev",
    .remove = vfio_dev_remove,
};

static bool vfio_map_dma(vfio_dev_t* vfio, rvvm_machine_t* machine, rvvm_addr_t mem_base, size_t mem_size)
{
    struct vfio_iommu_type1_dma_map dma_map = {
        .argsz = sizeof(struct vfio_iommu_type1_dma_map),
        .flags = VFIO_DMA_MAP_FLAG_READ | VFIO_DMA_MAP_FLAG_WRITE,
        .iova = mem_base,
        .size = mem_size,
        .vaddr = (size_t)rvvm_get_dma_ptr(machine, mem_base, mem_size),
    };
    return ioctl(vfio->container, VFIO_IOMMU_MAP_DMA, &dma_map) == 0;
}

static bool vfio_try_attach(vfio_dev_t* vfio, rvvm_machine_t* machine, const char* pci_id)
{
    vfio->container = open("/dev/vfio/vfio", O_RDWR | O_CLOEXEC);
    if (vfio->container == -1) {
        rvvm_error("Could not open /dev/vfio/vfio: %s", strerror(errno));
        return false;
    }
    vfio->group = vfio_open_group(pci_id);
    if (vfio->group == -1) {
        rvvm_error("Failed to open VFIO group: %s", strerror(errno));
        return false;
    }
    struct vfio_group_status group_status = { .argsz = sizeof(struct vfio_group_status), };
    if (ioctl(vfio->group, VFIO_GROUP_GET_STATUS, &group_status) || !(group_status.flags & VFIO_GROUP_FLAGS_VIABLE)) {
        rvvm_error("VFIO group not viable, are all group devices attached to vfio_pci module?");
        return false;
    }
    if (ioctl(vfio->group, VFIO_GROUP_SET_CONTAINER, &vfio->container)) {
        rvvm_error("Failed to set VFIO container group: %s", strerror(errno));
        return false;
    }
    if (ioctl(vfio->container, VFIO_SET_IOMMU, VFIO_TYPE1_IOMMU)) {
        rvvm_error("Failed to set up VFIO IOMMU: %s", strerror(errno));
        return false;
    }

    // Set up DMA to guest RAM
    rvvm_addr_t mem_base = rvvm_get_opt(machine, RVVM_OPT_MEM_BASE);
    rvvm_addr_t mem_size = rvvm_get_opt(machine, RVVM_OPT_MEM_SIZE);
    if (!vfio_map_dma(vfio, machine, mem_base, mem_size)) {
        // This *kinda* works around a DMA conflict with x86 MSI IRQ vector reserved region
        // More info: https://lore.kernel.org/linux-iommu/20191211082304.2d4fab45@x1.home/
        // cat /sys/kernel/iommu_groups/[iommu group]/reserved_regions

        // LAPIC MSI registers are usually placed on address 0xFEE00000, I/O APIC on address 0xFEС00000
        const rvvm_addr_t msi_x86_low = 0xFEC00000;
        const rvvm_addr_t msi_x86_end = 0xFEF00000;
        rvvm_info("Workaround reserved x86 MSI IRQ vector by splitting DMA region");
        if (mem_base < msi_x86_low) {
            size_t low_size = EVAL_MIN(mem_size, msi_x86_low - mem_base);
            if (!vfio_map_dma(vfio, machine, mem_base, low_size)) {
                rvvm_error("Failed to set up VFIO DMA: %s", strerror(errno));
                rvvm_error("This is likely caused by reserved mappings on your host overlapping guest RAM");
                return false;
            }
        }
        if (mem_base + mem_size > msi_x86_end) {
            size_t high_size = (mem_base + mem_size) - msi_x86_end;
            if (!vfio_map_dma(vfio, machine, msi_x86_end, high_size)) {
                rvvm_error("Failed to set up VFIO DMA: %s", strerror(errno));
                rvvm_error("This is likely caused by reserved mappings on your host overlapping guest RAM");
                return false;
            }
        }
    }

    vfio->device = ioctl(vfio->group, VFIO_GROUP_GET_DEVICE_FD, pci_id);
    if (vfio->device < 0) {
        rvvm_error("Failed to get VFIO device fd: %s", strerror(errno));
        return false;
    }
    struct vfio_device_info device_info = { .argsz = sizeof(struct vfio_device_info), };
    if (ioctl(vfio->device, VFIO_DEVICE_GET_INFO, &device_info)) {
        rvvm_error("Failed to get VFIO device info: %s", strerror(errno));
        return false;
    }

    // Read PCI config space of the device
    struct vfio_region_info pci_cfg_info = { .argsz = sizeof(struct vfio_region_info), .index = VFIO_PCI_CONFIG_REGION_INDEX, };
    if (ioctl(vfio->device, VFIO_DEVICE_GET_REGION_INFO, &pci_cfg_info)) {
        rvvm_error("Failed to get VFIO PCI config space info: %s", strerror(errno));
        return false;
    }
    uint8_t pci_config[64];
    if (pread(vfio->device, pci_config, 64, pci_cfg_info.offset) != 64) {
        rvvm_error("Failed to read PCI config space: %s", strerror(errno));
        return false;
    }

    vfio->pci_desc.func[0].vendor_id = read_uint16_le(pci_config);
    vfio->pci_desc.func[0].device_id = read_uint16_le(pci_config + 0x2);
    vfio->pci_desc.func[0].class_code = read_uint16_le(pci_config + 0xA);
    vfio->pci_desc.func[0].prog_if = pci_config[0x9];
    vfio->pci_desc.func[0].irq_pin = pci_config[0x3D];

    // Enable interrupts, Bus-mastering, MMIO access, Write-inval
    write_uint32_le(pci_config + 4, 0x16);
    if (pwrite(vfio->device, pci_config + 4, 4, pci_cfg_info.offset + 4) != 4) {
        rvvm_error("Failed to write PCI config space: %s", strerror(errno));
        return false;
    }

    // Set up device BAR mappings
    for (uint32_t i=0; i<device_info.num_regions && i<=VFIO_PCI_BAR5_REGION_INDEX; ++i) {
        struct vfio_region_info region_info = { .argsz = sizeof(struct vfio_region_info), .index = i, };
        if (ioctl(vfio->device, VFIO_DEVICE_GET_REGION_INFO, &region_info)) {
            rvvm_error("Failed to get VFIO BAR info: %s", strerror(errno));
            return false;
        }
        if (region_info.size && (region_info.flags & VFIO_REGION_INFO_FLAG_MMAP)) {
            rvvm_info("VFIO PCI BAR %d: size 0x%lx, offset 0x%lx, flags 0x%x", i,
                (unsigned long)region_info.size, (unsigned long)region_info.offset, (uint32_t)region_info.flags);
            void* bar = mmap(NULL, region_info.size, PROT_READ | PROT_WRITE, MAP_SHARED, vfio->device, region_info.offset);
            if (bar == MAP_FAILED) {
                rvvm_error("VFIO BAR mmap() failed: %s", strerror(errno));
                return false;
            }
            vfio->pci_desc.func[0].bar[i].mapping = bar;
            vfio->pci_desc.func[0].bar[i].size = region_info.size;
            vfio->pci_desc.func[0].bar[i].min_op_size = 1;
            vfio->pci_desc.func[0].bar[i].max_op_size = 16;
            vfio->pci_desc.func[0].bar[i].type = &vfio_bar_type;
        }
    }

    // Check IRQ capabilities
    if (device_info.num_irqs <= VFIO_PCI_MSI_IRQ_INDEX) {
        rvvm_error("No support for VFIO INTx IRQ");
        return false;
    }
    struct vfio_irq_info irq_info = { .argsz = sizeof(irq_info), .index = VFIO_PCI_MSI_IRQ_INDEX, };
    if (ioctl(vfio->device, VFIO_DEVICE_GET_IRQ_INFO, &irq_info)) {
        rvvm_error("Failed to get VFIO IRQ info: %s", strerror(errno));
        return false;
    }
    if (!(irq_info.flags & VFIO_IRQ_INFO_EVENTFD)) {
        rvvm_error("No support for VFIO IRQ eventfd");
        return false;
    }

    // Set up device IRQs & IRQ eventfd
    vfio->eventfd = eventfd(0, 0);
    if (vfio->eventfd < 0) {
        rvvm_error("Failed to create VFIO IRQ eventfd: %s", strerror(errno));
        return false;
    }
    size_t irq_size = sizeof(struct vfio_irq_set) + sizeof(int);
    struct vfio_irq_set* irq_set = safe_calloc(irq_size, 1);
    irq_set->argsz = irq_size;
    irq_set->flags = VFIO_IRQ_SET_DATA_EVENTFD | VFIO_IRQ_SET_ACTION_TRIGGER;
    irq_set->index = VFIO_PCI_MSI_IRQ_INDEX;
    irq_set->start = 0;
    irq_set->count = 1;
    *((int*)irq_set->data) = vfio->eventfd; // For MSI IRQs, this is an array of eventfds
    if (ioctl(vfio->device, VFIO_DEVICE_SET_IRQS, irq_set)) {
        rvvm_error("Failed to set VFIO IRQ eventfd: %s", strerror(errno));
        free(irq_set);
        return false;
    }
    free(irq_set);

    // Graceful device reset, all OK
    ioctl(vfio->device, VFIO_DEVICE_RESET);
    return true;
}

PUBLIC bool pci_vfio_init_auto(rvvm_machine_t* machine, const char* pci_id)
{
    char long_pci_id[256] = "0000:";
    if (rvvm_strlen(pci_id) < 12) {
        // This is a shorthand pci id, like in lspci (00:01.0) - extend it to sysfs format
        rvvm_strlcpy(long_pci_id + 5, pci_id, sizeof(long_pci_id) - 5);
        pci_id = long_pci_id;
    }

    UNUSED(!system("modprobe vfio_pci")); // Just in case

    if (vfio_bind(pci_id)) {
        pci_bus_t* pci_bus = rvvm_get_pci_bus(machine);
        vfio_dev_t* vfio = safe_new_obj(vfio_dev_t);
        if (vfio_try_attach(vfio, machine, pci_id)) {
            rvvm_mmio_dev_t vfio_dev_placeholder = {
                .size = 0,
                .type = &vfio_dev_type,
                .data = vfio,
                .read = rvvm_mmio_none,
                .write = rvvm_mmio_none,
            };
            rvvm_mmio_handle_t placeholder = rvvm_attach_mmio(machine, &vfio_dev_placeholder);

            vfio->pci_dev = pci_bus_add_device(pci_bus, &vfio->pci_desc);
            if (vfio->pci_dev) {
                vfio->running = true;
                vfio->thread = thread_create(vfio_irq_thread, vfio);
                return true;
            } else {
                rvvm_detach_mmio(machine, placeholder, false);
            }
        }
        // We couldn't attach to either host VFIO device or guest PCI bus
        vfio_dev_free(vfio);
    } else rvvm_error("Can't bind PCI device to vfio_pci kernel module");

    return false;
}

#else

PUBLIC bool pci_vfio_init_auto(rvvm_machine_t* machine, const char* pci_id)
{
    rvvm_error("VFIO isn't available");
    UNUSED(machine); UNUSED(pci_id);
    return false;
}

#endif
