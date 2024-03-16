/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

package lekkit.rvvm;

public class RTL8169 extends PCIDevice {
    public RTL8169(RVVMMachine machine) {
        if (machine.isValid()) {
            this.machine = machine;
            this.pci_dev = RVVMNative.rtl8169_init_auto(machine.machine);
        }
    }
}
