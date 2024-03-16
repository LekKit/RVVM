/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

package lekkit.rvvm;

public class GoldfishRTC extends MMIODevice {
    public GoldfishRTC(RVVMMachine machine) {
        super(machine);
        if (machine.isValid()) {
            this.mmio_handle = RVVMNative.rtc_goldfish_init_auto(machine.machine);
        }
    }
}
