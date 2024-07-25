/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

package lekkit.rvvm;

import java.nio.ByteBuffer;

public class Framebuffer extends MMIODevice {
    protected int width;
    protected int height;
    protected int bpp;
    protected ByteBuffer buff;

    public static final int BPP_R5G6B5 = 16;
    public static final int BPP_R8G8B8 = 24;
    public static final int BPP_A8R8G8B8 = 32;

    public Framebuffer(RVVMMachine machine, int x, int y) {
        this(machine, x, y, BPP_A8R8G8B8);
    }

    public Framebuffer(RVVMMachine machine, int x, int y, int bpp) {
        super(machine);
        this.width = x;
        this.height = y;
        this.bpp = bpp;
        this.buff = ByteBuffer.allocateDirect(x * y * (bpp / 8));
        if (machine.isValid()) {
            setMMIOHandle(RVVMNative.framebuffer_init_auto(machine.getPtr(), this.buff, x, y, bpp));
        }
    }

    public ByteBuffer getBuffer() {
        return buff;
    }
    public int getWidth() {
        return width;
    }
    public int getHeight() {
        return height;
    }
    public int getBpp() {
        return bpp;
    }
}
