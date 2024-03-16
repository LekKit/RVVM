/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

package lekkit.rvvm;

public class GPIODevice extends MMIODevice {
    protected long gpio_dev;

    public GPIODevice(RVVMMachine machine) {
        super(machine);
        if (machine.isValid()) {
            gpio_dev = RVVMNative.gpio_dev_create();
        } else {
            gpio_dev = 0;
        }
    }

    public boolean write_pins(int offset, int pins) {
        if (gpio_dev != 0) return RVVMNative.gpio_write_pins(gpio_dev, offset, pins);
        return false;
    }

    public int read_pins(int offset) {
        if (gpio_dev != 0) return RVVMNative.gpio_read_pins(gpio_dev, offset);
        return 0;
    }

    public boolean write_pins(int pins) {
        return write_pins(0, pins);
    }

    public int read_pins() {
        return read_pins(0);
    }
}
