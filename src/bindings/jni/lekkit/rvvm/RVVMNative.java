/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

package lekkit.rvvm;

import java.nio.ByteBuffer;

// Regenerate C prototypes: $ javac -h . RVVMNative.java

public class RVVMNative {
    // Do not crash the JVM if we failed to load native lib
    public static boolean loaded = false;

    private static void checkABI() {
        if (check_abi(9)) {
            loaded = true;
        } else {
            System.out.println("ERROR: Invalid librvvm ABI version! Please update your JNI bindings!");
        }
    }

    // Manually load librvvm
    public static boolean loadLib(String path) {
        if (loaded) return true;
        try {
            System.load(path);
            checkABI();
        } catch (Throwable e) {
            System.out.println("ERROR: Failed to load librvvm: " + e.toString());
        }
        return loaded;
    }

    static {
        try {
            System.loadLibrary("rvvm");
            checkABI();
        } catch (Throwable e) {
            System.out.println("INFO: Failed to load system-wide librvvm: " + e.toString());
        }
    }

    public static boolean isLoaded() {
        return loaded;
    }

    //
    // Common RVVM API functions
    //

    public static native boolean check_abi(int abi);

    ///
    // RVVM Machine Management API
    //

    public static native long    create_machine(long mem_size, int smp, String isa);
    public static native void    set_cmdline(long machine, String cmdline);
    public static native void    append_cmdline(long machine, String cmdline);
    public static native boolean load_bootrom(long machine, String path);
    public static native boolean load_kernel(long machine, String path);
    public static native boolean load_dtb(long machine, String path);
    public static native boolean dump_dtb(long machine, String path);
    public static native long    get_opt(long machine, int opt);
    public static native void    set_opt(long machine, int opt, long val);
    public static native boolean start_machine(long machine);
    public static native boolean pause_machine(long machine);
    public static native boolean reset_machine(long machine, boolean reset);
    public static native boolean machine_running(long machine);
    public static native boolean machine_powered(long machine);
    public static native void    free_machine(long machine);
    public static native void    run_eventloop();

    //
    // RVVM Device API
    //

    public static native ByteBuffer get_dma_buf(long machine, long addr, long size);
    public static native long       mmio_zone_auto(long machine, long addr, long size);

    public static native void remove_mmio(long mmio_dev);

    // Get wired interrupt controller
    public static native long get_intc(long machine);
    public static native void set_intc(long machine, long intc);

    // Get PCI bus (root complex)
    public static native long get_pci_bus(long machine);
    public static native void set_pci_bus(long machine, long pci_bus);

    // Get I2C bus
    public static native long get_i2c_bus(long machine);
    public static native void set_i2c_bus(long machine, long i2c_bus);

    //
    // RVVM Devices
    //

    public static native void riscv_clint_init_auto(long machine);
    public static native void riscv_imsic_init_auto(long machine);

    // Returns wired IRQ controller handle
    public static native long riscv_plic_init_auto(long machine);
    public static native long riscv_aplic_init_auto(long machine);

    // Returns PCI bus handle
    public static native long pci_bus_init_auto(long machine);

    // Returns I2C bus handle
    public static native long i2c_bus_init_auto(long machine);

    // Returns TAP device handle
    public static native long tap_user_open();

    // Returns MMIO handle
    public static native long syscon_init_auto(long machine);
    public static native long rtc_goldfish_init_auto(long machine);
    public static native long rtc_ds1742_init_auto(long machine);
    public static native long ns16550a_init_auto(long machine);
    public static native long gpio_sifive_init_auto(long machine, long gpio);
    public static native long mtd_physmap_init_auto(long machine, String image_path, boolean rw);
    public static native long framebuffer_init_auto(long machine, ByteBuffer[] fb, int x, int y, int bpp);

    // Returns PCI device handle
    public static native long rtl8169_init(long pci_bus, long tap);
    public static native long nvme_init(long pci_bus, String image_path, boolean rw);

    // Returns HID mouse handle
    public static native long hid_mouse_init_auto(long machine);

    // Returns HID keyboard handle
    public static native long hid_keyboard_init_auto(long machine);

    // Returns GPIO handle
    public static native long gpio_dev_create();

    // Takes PCI device hande
    public static native void pci_remove_device(long dev);

    // Takes GPIO handle
    public static native void    gpio_dev_free(long gpio);
    public static native int     gpio_read_pins(long gpio, int off);
    public static native boolean gpio_write_pins(long gpio, int off, int pins);

    // Takes HID mouse handle
    public static native void hid_mouse_resolution(long mouse, int x, int y);
    public static native void hid_mouse_place(long mouse, int x, int y);
    public static native void hid_mouse_move(long mouse, int x, int y);
    public static native void hid_mouse_press(long mouse, byte btns);
    public static native void hid_mouse_release(long mouse, byte btns);
    public static native void hid_mouse_scroll(long mouse, int offset);

    // Takes HID keyboard handle
    public static native void hid_keyboard_press(long kb, byte key);
    public static native void hid_keyboard_release(long kb, byte key);

    //
    // NS16550A JNI bridge
    //
    // Attaches a second UART backed by two 64 KiB ring buffers instead of
    // stdio. Java drains guest TX with poll() and injects guest RX with
    // feed(). The bridge handle is freed automatically when the owning
    // machine is freed — no explicit remove call needed.
    //

    // Returns a bridge handle (opaque), or 0 on failure.
    public static native long ns16550a_bridge_init(long machine);

    // Drains up to out.length bytes of guest TX into `out`. Returns count written.
    public static native int  ns16550a_bridge_poll(long handle, byte[] out);

    // Feeds up to in.length bytes into guest RX. Returns count accepted.
    public static native int  ns16550a_bridge_feed(long handle, byte[] in);

    // Fills a long[5] with {pushed, popped, fed, consumed, dropped}.
    public static native void ns16550a_bridge_stats(long handle, long[] out);
}
