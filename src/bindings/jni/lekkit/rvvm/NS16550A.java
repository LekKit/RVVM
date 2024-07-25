/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

package lekkit.rvvm;

public class NS16550A extends MMIODevice {
    public NS16550A(RVVMMachine machine) {
        super(machine);
        if (machine.isValid()) {
            setMMIOHandle(RVVMNative.ns16550a_init_auto(machine.getPtr()));
        }
    }
}
