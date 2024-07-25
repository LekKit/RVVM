/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

package lekkit.rvvm;

public abstract class MMIODevice implements IRemovableDevice {
    private final RVVMMachine machine;
    private int mmio_handle = -1;

    public MMIODevice(RVVMMachine machine) {
        this.machine = machine;
    }

    public RVVMMachine getMachine() {
        return machine;
    }

    protected void setMMIOHandle(int mmio_handle) {
        this.mmio_handle = mmio_handle;
    }

    public boolean isValid() {
        return machine.isValid() && mmio_handle != -1;
    }

    public synchronized void remove() {
        if (isValid()) {
            RVVMNative.detach_mmio(machine.getPtr(), mmio_handle, true);
            mmio_handle = -1;
        }
    }
}
