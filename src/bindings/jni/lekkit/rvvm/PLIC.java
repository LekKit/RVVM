/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

package lekkit.rvvm;

public class PLIC {
    protected final RVVMMachine machine;
    protected final long plic_ctx;

    public PLIC(RVVMMachine machine) {
        if (machine.isValid()) {
            this.machine = machine;
            plic_ctx = RVVMNative.plic_init_auto(machine.getPtr());
        } else {
            this.machine = null;
            plic_ctx = 0;
        }
    }
}
