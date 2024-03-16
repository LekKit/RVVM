/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

package lekkit.rvvm;

import java.nio.ByteBuffer;

// Regenerate C prototypes: $ javac -h . RvvmJni.java

public class RVVMNative {
    // Do not crash the JVM if we failed to load native lib
    public static boolean loaded = false;

    // Manually load librvvm
    public static boolean loadLib(String path) {
        if (loaded) return true;
        try {
            System.load(path);
            int abi = get_abi_version();
            if (abi == 6) {
                loaded = true;
            } else {
                System.out.println("ERROR: Invalid librvvm ABI version: " + Integer.toString(abi));
            }
        } catch (Throwable e) {
            System.out.println("ERROR: Failed to load librvvm: " + e.toString());
        }
        return loaded;
    }

    static {
        try {
            System.loadLibrary("rvvm");
            int abi = get_abi_version();
            if (abi == 6) {
                loaded = true;
            } else {
                System.out.println("ERROR: Invalid librvvm ABI version: " + Integer.toString(abi));
            }
        } catch (Throwable e) {
            System.out.println("INFO: Failed to load system-wide librvvm: " + e.toString());
        }
    }

    public static boolean isLoaded() {
        return loaded;
    }

    public static final long DEFAULT_MEMBASE = 0x80000000L;

    public static native int get_abi_version();

    // Common RVVM API functions
    public static native long       create_machine(long mem_base, long mem_size, int smp, boolean rv64);
    public static native ByteBuffer get_dma_buf(long machine, long addr, long size);
    public static native long       get_plic(long machine);
    public static native void       set_plic(long machine, long plic);
    public static native long       get_pci_bus(long machine);
    public static native void       set_pci_bus(long machine, long pci_bus);
    public static native long       get_i2c_bus(long machine);
    public static native void       set_i2c_bus(long machine, long i2c_bus);
    public static native void       set_cmdline(long machine, String cmdline);
    public static native void       append_cmdline(long machine, String cmdline);
    public static native long       get_opt(long machine, int opt);
    public static native void       set_opt(long machine, int opt, long val);
    public static native boolean    load_bootrom(long machine, String path);
    public static native boolean    load_kernel(long machine, String path);
    public static native boolean    load_dtb(long machine, String path);
    public static native boolean    dump_dtb(long machine, String path);
    public static native boolean    start_machine(long machine);
    public static native boolean    pause_machine(long machine);
    public static native boolean    reset_machine(long machine, boolean reset);
    public static native boolean    machine_powered(long machine);
    public static native void       free_machine(long machine);
    public static native long       mmio_zone_auto(long machine, long addr, long size);
    public static native void       detach_mmio(long machine, int handle, boolean cleanup);
    public static native void       run_eventloop();

    // TODO: MMIO API
    //public static native int        attach_mmio(long machine, MMIOBase mmio);

    // Devices
    public static native void clint_init_auto(long machine);
    public static native long plic_init_auto(long machine);
    public static native long pci_bus_init_auto(long machine);
    public static native long i2c_bus_init_auto(long machine);
    public static native int  ns16550a_init_auto(long machine);
    public static native int  rtc_goldfish_init_auto(long machine);
    public static native int  syscon_init_auto(long machine);
    public static native long rtl8169_init_auto(long machine);
    public static native long nvme_init_auto(long machine, String image_path, boolean rw);
    public static native int  mtd_physmap_init_auto(long machine, String image_path, boolean rw);
    public static native int  framebuffer_init_auto(long machine, ByteBuffer fb, int x, int y, int bpp);
    public static native long hid_mouse_init_auto(long machine);
    public static native long hid_keyboard_init_auto(long machine);

    public static native long    gpio_dev_create();
    public static native void    gpio_dev_free(long gpio);
    public static native int     gpio_read_pins(long gpio, int off);
    public static native boolean gpio_write_pins(long gpio, int off, int pins);
    public static native int     gpio_sifive_init_auto(long machine, long gpio);

    public static native void pci_remove_device(long dev);

    public static native void hid_mouse_resolution(long mouse, int x, int y);
    public static native void hid_mouse_place(long mouse, int x, int y);
    public static native void hid_mouse_move(long mouse, int x, int y);
    public static native void hid_mouse_press(long mouse, byte btns);
    public static native void hid_mouse_release(long mouse, byte btns);
    public static native void hid_mouse_scroll(long mouse, int offset);

    public static native void hid_keyboard_press(long kb, byte key);
    public static native void hid_keyboard_release(long kb, byte key);
}
