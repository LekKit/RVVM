/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

package lekkit.rvvm;

public class SiFiveGPIO extends GPIODevice {
    public SiFiveGPIO(RVVMMachine machine) {
        super(machine);
        if (this.gpio_dev != 0 && machine.isValid()) {
            this.machine = machine;
            this.mmio_handle = RVVMNative.gpio_sifive_init_auto(machine.machine, this.gpio_dev);
        }
        if (this.mmio_handle == -1 && this.gpio_dev != 0) RVVMNative.gpio_dev_free(gpio_dev);
    }
}
