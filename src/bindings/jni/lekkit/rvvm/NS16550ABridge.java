/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

package lekkit.rvvm;

/**
 * An NS16550A UART backed by ring buffers exposed to the JVM.
 *
 * <p>Create one per machine that wants a programmatic serial line. The
 * UART attaches via the same auto-init path as {@link NS16550A} (and thus
 * appears in the FDT as {@code /soc/uart@...}), but instead of stdio its
 * chardev backend is a pair of 64 KiB ring buffers:
 *
 * <ul>
 *   <li>{@link #poll(byte[])} drains guest TX.</li>
 *   <li>{@link #feed(byte[])} injects guest RX.</li>
 * </ul>
 *
 * <p>The bridge is freed automatically when its owning machine is freed —
 * the chardev's {@code remove} vtable entry tears down the rings. Do not
 * hold a handle past the machine's lifetime.
 */
public class NS16550ABridge {
    private final RVVMMachine machine;
    private long handle;

    public NS16550ABridge(RVVMMachine machine) {
        this.machine = machine;
        this.handle = RVVMNative.ns16550a_bridge_init(machine.getPtr());
    }

    public boolean isValid() {
        return machine.isValid() && handle != 0;
    }

    public RVVMMachine getMachine() {
        return machine;
    }

    /**
     * Drain up to {@code out.length} bytes of guest TX. Returns the number
     * of bytes actually written into {@code out}. Non-blocking; returns 0
     * if the guest hasn't produced anything.
     */
    public int poll(byte[] out) {
        if (!isValid() || out == null) return 0;
        return RVVMNative.ns16550a_bridge_poll(handle, out);
    }

    /**
     * Push bytes into guest RX. Returns the count accepted; may be less
     * than {@code in.length} if the RX ring is near-full.
     */
    public int feed(byte[] in) {
        if (!isValid() || in == null) return 0;
        return RVVMNative.ns16550a_bridge_feed(handle, in);
    }

    /**
     * Counters for instrumentation: {pushed, popped, fed, consumed,
     * dropped}. Fills the provided {@code long[5]} to avoid allocating
     * on hot paths.
     */
    public void stats(long[] out) {
        if (!isValid() || out == null || out.length < 5) return;
        RVVMNative.ns16550a_bridge_stats(handle, out);
    }
}
