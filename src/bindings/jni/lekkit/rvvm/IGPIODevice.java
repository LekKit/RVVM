/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

package lekkit.rvvm;

public interface IGPIODevice {

    public boolean write_pins(int offset, int pins);

    public int read_pins(int offset);

    public boolean write_pins(int pins);

    public int read_pins();
}
