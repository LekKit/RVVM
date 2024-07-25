/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

package lekkit.rvvm;

public class Syscon extends MMIODevice {
    public Syscon(RVVMMachine machine) {
        super(machine);
        if (machine.isValid()) {
            setMMIOHandle(RVVMNative.syscon_init_auto(machine.getPtr()));
        }
    }
}
