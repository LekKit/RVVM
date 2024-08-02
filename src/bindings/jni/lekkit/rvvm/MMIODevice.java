/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

package lekkit.rvvm;

public abstract class MMIODevice implements IRemovableDevice {
    private final RVVMMachine machine;
    private long mmio_dev = 0;

    public MMIODevice(RVVMMachine machine) {
        this.machine = machine;
    }

    public RVVMMachine getMachine() {
        return machine;
    }

    protected void setMMIOHandle(long mmio_dev) {
        this.mmio_dev = mmio_dev;
    }

    public boolean isValid() {
        return machine.isValid() && this.mmio_dev != 0;
    }

    public synchronized void remove() {
        if (isValid()) {
            RVVMNative.remove_mmio(this.mmio_dev);
            mmio_dev = 0;
        }
    }
}
