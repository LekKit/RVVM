/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

package lekkit.rvvm;

public class MTDFlash extends MMIODevice {
    public MTDFlash(RVVMMachine machine, String imagePath, boolean rw) {
        super(machine);
        if (machine.isValid()) {
            setMMIOHandle(RVVMNative.mtd_physmap_init_auto(machine.getPtr(), imagePath, rw));
        }
    }
}
