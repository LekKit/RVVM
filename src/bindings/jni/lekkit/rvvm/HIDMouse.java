/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

package lekkit.rvvm;

public class HIDMouse {
    private final RVVMMachine machine;
    private final long hid_mouse;

    public static final byte HID_BTN_NONE = 0;
    public static final byte HID_BTN_LEFT = 1;
    public static final byte HID_BTN_RIGHT = 2;
    public static final byte HID_BTN_MIDDLE = 4;

    public static final int HID_SCROLL_UP = -1;
    public static final int HID_SCROLL_DOWN = 1;

    public HIDMouse(RVVMMachine machine) {
        if (machine.isValid()) {
            this.machine = machine;
            this.hid_mouse = RVVMNative.hid_mouse_init_auto(machine.getPtr());
        } else {
            this.machine = null;
            this.hid_mouse = 0;
        }
    }

    public boolean isValid() {
        return hid_mouse != 0;
    }

    public void move(int x, int y) {
        if (isValid()) {
            RVVMNative.hid_mouse_move(hid_mouse, x, y);
        }
    }
    public void place(int x, int y) {
        if (isValid()) {
            RVVMNative.hid_mouse_place(hid_mouse, x, y);
        }
    }
    public void resolution(int x, int y) {
        if (isValid()) {
            RVVMNative.hid_mouse_resolution(hid_mouse, x, y);
        }
    }
    public void press(byte btns) {
        if (isValid()) {
            RVVMNative.hid_mouse_press(hid_mouse, btns);
        }
    }
    public void release(byte btns) {
        if (isValid()) {
            RVVMNative.hid_mouse_release(hid_mouse, btns);
        }
    }
    public void scroll(int offset) {
        if (isValid()) {
            RVVMNative.hid_mouse_scroll(hid_mouse, offset);
        }
    }
}
