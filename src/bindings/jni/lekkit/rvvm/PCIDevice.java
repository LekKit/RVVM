/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

package lekkit.rvvm;

public abstract class PCIDevice implements IRemovableDevice {
    private final RVVMMachine machine;
    private long pci_dev;

    public PCIDevice(RVVMMachine machine) {
        this.machine = machine;
    }

    public RVVMMachine getMachine() {
        return machine;
    }

    protected void setPCIHandle(long pci_dev) {
        this.pci_dev = pci_dev;
    }

    public boolean isValid() {
        return machine.isValid() && pci_dev != 0;
    }

    public synchronized void remove() {
        if (isValid()) {
            RVVMNative.pci_remove_device(pci_dev);
            pci_dev = 0;
        }
    }
}
