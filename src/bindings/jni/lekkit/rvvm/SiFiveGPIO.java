/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

package lekkit.rvvm;

public class SiFiveGPIO extends MMIODevice implements IGPIODevice {
    private long gpio_dev = 0;

    public SiFiveGPIO(RVVMMachine machine) {
        super(machine);
        if (machine.isValid()) {
            this.gpio_dev = RVVMNative.gpio_dev_create();

            setMMIOHandle(RVVMNative.gpio_sifive_init_auto(getMachine().getPtr(), this.gpio_dev));

            if (!this.isValid() && this.gpio_dev != 0) {
                RVVMNative.gpio_dev_free(gpio_dev);
                this.gpio_dev = 0;
            }
        }
    }

    public boolean write_pins(int offset, int pins) {
        if (isValid()) {
            return RVVMNative.gpio_write_pins(gpio_dev, offset, pins);
        }
        return false;
    }

    public int read_pins(int offset) {
        if (isValid()) {
            return RVVMNative.gpio_read_pins(gpio_dev, offset);
        }
        return 0;
    }

    public boolean write_pins(int pins) {
        return write_pins(0, pins);
    }

    public int read_pins() {
        return read_pins(0);
    }
}
