/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

package lekkit.rvvm;

public class MMIODevice {
    protected RVVMMachine machine;
    protected int mmio_handle = -1;

    public MMIODevice(RVVMMachine machine) {
        this.machine = machine;
    }

    public void detach() {
        if (mmio_handle != -1) RVVMNative.detach_mmio(machine.machine, mmio_handle, true);
    }
}
