/*
haiku_window.cpp - Haiku RVVM Window
Copyright (C) 2022  X547 <github.com/X547>
                    LekKit <github.com/LekKit>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

extern "C" {
#include "gui_window.h"
#include "utils.h"
}

#include <Application.h>
#include <Window.h>
#include <View.h>
#include <Bitmap.h>
#include <Cursor.h>
#include <OS.h>
#include <private/shared/AutoDeleter.h>

// C++ doesn't allow designated initializers like in win32window... Ugh
// Luckily, Haiku has contiguous & small keycodes for a trivial array initializer

// Don't ever touch this table, or The Order will not take kindly.
// Otherwise be prepared to suffer the Consequences...
static const hid_key_t haiku_key_to_hid_byte_map[] = {
    HID_KEY_NONE,
    HID_KEY_ESC,
    HID_KEY_F1,
    HID_KEY_F2,
    HID_KEY_F3,
    HID_KEY_F4,
    HID_KEY_F5,
    HID_KEY_F6,
    HID_KEY_F7,
    HID_KEY_F8,
    HID_KEY_F9,
    HID_KEY_F10,
    HID_KEY_F11,
    HID_KEY_F12,
    HID_KEY_SYSRQ,
    HID_KEY_SCROLLLOCK,
    HID_KEY_PAUSE,
    HID_KEY_GRAVE,
    HID_KEY_1,
    HID_KEY_2,
    HID_KEY_3,
    HID_KEY_4,
    HID_KEY_5,
    HID_KEY_6,
    HID_KEY_7,
    HID_KEY_8,
    HID_KEY_9,
    HID_KEY_0,
    HID_KEY_MINUS,
    HID_KEY_EQUAL,
    HID_KEY_BACKSPACE,
    HID_KEY_INSERT,
    HID_KEY_HOME,
    HID_KEY_PAGEUP,
    HID_KEY_NUMLOCK,
    HID_KEY_KPSLASH,
    HID_KEY_KPASTERISK,
    HID_KEY_KPMINUS,
    HID_KEY_TAB,
    HID_KEY_Q,
    HID_KEY_W,
    HID_KEY_E,
    HID_KEY_R,
    HID_KEY_T,
    HID_KEY_Y,
    HID_KEY_U,
    HID_KEY_I,
    HID_KEY_O,
    HID_KEY_P,
    HID_KEY_LEFTBRACE,
    HID_KEY_RIGHTBRACE,
    HID_KEY_BACKSLASH,
    HID_KEY_DELETE,
    HID_KEY_END,
    HID_KEY_PAGEDOWN,
    HID_KEY_KP7,
    HID_KEY_KP8,
    HID_KEY_KP9,
    HID_KEY_KPPLUS,
    HID_KEY_CAPSLOCK,
    HID_KEY_A,
    HID_KEY_S,
    HID_KEY_D,
    HID_KEY_F,
    HID_KEY_G,
    HID_KEY_H,
    HID_KEY_J,
    HID_KEY_K,
    HID_KEY_L,
    HID_KEY_SEMICOLON,
    HID_KEY_APOSTROPHE,
    HID_KEY_ENTER,
    HID_KEY_KP4,
    HID_KEY_KP5,
    HID_KEY_KP6,
    HID_KEY_LEFTSHIFT,
    HID_KEY_Z,
    HID_KEY_X,
    HID_KEY_C,
    HID_KEY_V,
    HID_KEY_B,
    HID_KEY_N,
    HID_KEY_M,
    HID_KEY_COMMA,
    HID_KEY_DOT,
    HID_KEY_SLASH,
    HID_KEY_RIGHTSHIFT,
    HID_KEY_UP,
    HID_KEY_KP1,
    HID_KEY_KP2,
    HID_KEY_KP3,
    HID_KEY_KPENTER,
    HID_KEY_LEFTCTRL,
    HID_KEY_LEFTALT,
    HID_KEY_SPACE,
    HID_KEY_RIGHTALT,
    HID_KEY_RIGHTCTRL,
    HID_KEY_LEFT,
    HID_KEY_DOWN,
    HID_KEY_RIGHT,
    HID_KEY_KP0,
    HID_KEY_KPDOT,
    HID_KEY_LEFTMETA,
    HID_KEY_RIGHTMETA,
    HID_KEY_COMPOSE,
    HID_KEY_102ND,
    HID_KEY_YEN,
    HID_KEY_RO,
    HID_KEY_MUHENKAN,
    HID_KEY_HENKAN,
    HID_KEY_KATAKANAHIRAGANA,
    HID_KEY_NONE, // Haiku keycode 0x6f unused?
    HID_KEY_KPCOMMA,
    // 0xf0 hangul?
    // 0xf1 hanja?
};

static hid_key_t haiku_key_to_hid(uint32_t haiku_key)
{
    if (haiku_key < sizeof(haiku_key_to_hid_byte_map)) {
        return haiku_key_to_hid_byte_map[haiku_key];
    }
    return HID_KEY_NONE;
}

class View: public BView {
private:
    ObjectDeleter<BBitmap> m_bitmap;
    gui_window_t* m_win;

public:
    View(BRect frame, const char* name, uint32 resizingMode, uint32 flags, gui_window_t* win);
    virtual ~View() {}

    void AttachedToWindow() override;
    void Draw(BRect dirty) override;
    void MessageReceived(BMessage* msg) override;
    void WindowActivated(bool active) override;

    BBitmap* GetBitmap() {
        return m_bitmap.Get();
    }
};

class Window: public BWindow {
private:
    View* m_view;
    gui_window_t* m_win;

public:
    Window(BRect frame, const char *title, gui_window_t* win);
    virtual ~Window() {}

    bool QuitRequested() override;

    View* GetView() {
        return m_view;
    }
};

View::View(BRect frame, const char* name, uint32 resizingMode, uint32 flags, gui_window_t* win):
    BView(frame, name, resizingMode, flags | B_WILL_DRAW),
    m_win(win)
{
    SetViewColor(B_TRANSPARENT_COLOR);
    SetLowColor(0, 0, 0);
    m_bitmap.SetTo(new BBitmap(frame.OffsetToCopy(B_ORIGIN), B_RGBA32));
}

void View::AttachedToWindow()
{
    BCursor cursor(B_CURSOR_ID_NO_CURSOR);
    SetViewCursor(&cursor);
}

void View::Draw(BRect dirty)
{
    UNUSED(dirty);
    DrawBitmap(GetBitmap());
}

void View::MessageReceived(BMessage* msg)
{
    int32 key;
    BPoint point;
    int32 btns;
    float wheel;
    switch (msg->what) {
        case B_KEY_DOWN:
        case B_UNMAPPED_KEY_DOWN:
            msg->FindInt32("be:key_repeat", &key);
            if (key == 0) {
                // Ignore key repeat events
                msg->FindInt32("key", &key);
                m_win->on_key_press(m_win, haiku_key_to_hid(key));
            }
            return;
        case B_KEY_UP:
        case B_UNMAPPED_KEY_UP:
            msg->FindInt32("key", &key);
            m_win->on_key_release(m_win, haiku_key_to_hid(key));
            return;
        case B_MOUSE_DOWN:
            SetMouseEventMask(B_POINTER_EVENTS);
            msg->FindInt32("buttons", &btns);
            m_win->on_mouse_press(m_win, btns);
            return;
        case B_MOUSE_UP:
            msg->FindInt32("buttons", &btns);
            m_win->on_mouse_release(m_win, ~btns);
            return;
        case B_MOUSE_MOVED:
            msg->FindPoint("where", &point);
            m_win->on_mouse_place(m_win, point.x, point.y);
            return;
        case B_MOUSE_WHEEL_CHANGED:
            msg->FindFloat("be:wheel_delta_y", &wheel);
            m_win->on_mouse_scroll(m_win, (int32_t)wheel);
            return;
    }
    return BView::MessageReceived(msg);
}

void View::WindowActivated(bool active)
{
    if (!active) {
        m_win->on_focus_lost(m_win);
    }
}

Window::Window(BRect frame, const char* title, gui_window_t* win):
    BWindow(frame, title, B_TITLED_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL, B_NOT_ZOOMABLE | B_NOT_RESIZABLE),
    m_win(win)
{
    m_view = new View(frame.OffsetToCopy(B_ORIGIN), "view", B_FOLLOW_ALL, 0, win);
    AddChild(m_view);
    m_view->MakeFocus();
}

bool Window::QuitRequested()
{
    m_win->on_close(m_win);
    return false;
}

static thread_id sAppThread = B_ERROR;

static status_t app_thread(void *arg)
{
    UNUSED(arg);
    be_app->Lock();
    be_app->Run();
    return B_OK;
}

static status_t init_application()
{
    if (be_app != NULL) return B_OK;

    new BApplication("application/x-vnd.RVVM");
    if (be_app == NULL) return B_NO_MEMORY;
    be_app->Unlock();
    sAppThread = spawn_thread(app_thread, "application", B_NORMAL_PRIORITY, NULL);
    if (sAppThread < B_OK) return sAppThread;
    resume_thread(sAppThread);

    return B_OK;
}

static void haiku_window_draw(gui_window_t* win)
{
    Window* window = (Window*)win->win_data;
    View* view = window->GetView();
    view->LockLooper();
    view->Invalidate();
    view->UnlockLooper();
}

static void haiku_window_poll(gui_window_t* win)
{
    // Input events handling done from window thread. TODO: Check thread safety.
    // Other GUI backends could implement threaded input too for better latency
    UNUSED(win);
}

static void haiku_window_set_title(gui_window_t* win, const char* title)
{
    Window* window = (Window*)win->win_data;
    window->SetTitle(title);
}

static void haiku_window_remove(gui_window_t* win)
{
    Window* window = (Window*)win->win_data;
    View* view = window->GetView();
    view->LockLooper();
    window->Quit(); // Also deletes window
}

bool haiku_window_init(gui_window_t* win)
{
    if (init_application() < B_OK) {
        rvvm_error("Failed to initialize be_app thread!");
        return false;
    }

    Window* window = new Window(BRect(0, 0, win->fb.width - 1, win->fb.height - 1), "RVVM", win);
    window->CenterOnScreen();
    window->Show();

    win->win_data = window;
    win->fb.format = RGB_FMT_A8R8G8B8;
    win->fb.buffer = window->GetView()->GetBitmap()->Bits();

    win->draw = haiku_window_draw;
    win->poll = haiku_window_poll;
    win->remove = haiku_window_remove;
    // TODO: haiku_window_grab_input with relative mouse mode
    win->set_title = haiku_window_set_title;

    return true;
}
