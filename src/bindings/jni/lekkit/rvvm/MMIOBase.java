/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

package lekkit.rvvm;

import java.nio.ByteBuffer;

public class MMIOBase {
    public long region_addr = 0;
    public long region_size = 0;
    public long min_op_size = 1;
    public long max_op_size = 8;

    ByteBuffer mapping;

    public void remove() {}
    public void update() {}
    public void reset() {}

    public boolean read(ByteBuffer data, long offset, byte size) { return true; }
    public boolean write(ByteBuffer data, long offset, byte size) { return true;}
}
