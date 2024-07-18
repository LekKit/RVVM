/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

package lekkit.rvvm;

public class PCIDevice {
    protected RVVMMachine machine;
    protected long pci_dev;

    public synchronized void detach() {
        if (pci_dev != 0) {
            RVVMNative.pci_remove_device(pci_dev);
            pci_dev = 0;
        }
    }
}
