/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

package lekkit.rvvm;

public class I2CBus {
    protected final RVVMMachine machine;
    protected final long i2c_bus;

    public I2CBus(RVVMMachine machine) {
        if (machine.isValid()) {
            this.machine = machine;
            i2c_bus = RVVMNative.i2c_bus_init_auto(machine.machine);
        } else {
            this.machine = null;
            i2c_bus = 0;
        }
    }
}
