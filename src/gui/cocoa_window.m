/*
cocoa_window.m - Cocoa (macOS / AppKit) GUI Window
Copyright (C) 2026  LekKit <github.com/LekKit>

This Source Code Form is subject to the terms of the Mozilla Public
License, v. 2.0. If a copy of the MPL was not distributed with this
file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/

#include "feature_test.h"

#include "compiler.h"
#include "gui_window.h"
#include "utils.h"
#include "vma_ops.h"

PUSH_OPTIMIZATION_SIZE

#if defined(USE_GUI) && defined(USE_COCOA_GUI) && defined(HOST_TARGET_APPLE)

#import <Cocoa/Cocoa.h>

/*
 * AppKit insists on running on the main thread, and RVVM conveniently drives
 * the display poll()/draw() callbacks from rvvm_run_eventloop() on the main
 * thread. So instead of blocking in [NSApp run], we manually pump the event
 * queue from poll() and blit the framebuffer from draw().
 */

// Device-dependent modifier flag bits (NX_DEVICE*KEYMASK), used to tell apart
// left/right modifier keys, which the public NSEventModifierFlag* masks can't.
#define COCOA_MOD_LCTRL  0x00000001u
#define COCOA_MOD_LSHIFT 0x00000002u
#define COCOA_MOD_RSHIFT 0x00000004u
#define COCOA_MOD_LMETA  0x00000008u
#define COCOA_MOD_RMETA  0x00000010u
#define COCOA_MOD_LALT   0x00000020u
#define COCOA_MOD_RALT   0x00000040u
#define COCOA_MOD_RCTRL  0x00002000u
#define COCOA_MOD_CAPS   0x00010000u // NSEventModifierFlagCapsLock

/*
 * macOS virtual keycode (kVK_*) -> USB HID usage translation.
 * Keycodes are layout-independent positions, so this table is fixed.
 */
static hid_key_t cocoa_key_to_hid(uint16_t key)
{
    switch (key) {
        // clang-format off
        case 0x00: return HID_KEY_A;
        case 0x0B: return HID_KEY_B;
        case 0x08: return HID_KEY_C;
        case 0x02: return HID_KEY_D;
        case 0x0E: return HID_KEY_E;
        case 0x03: return HID_KEY_F;
        case 0x05: return HID_KEY_G;
        case 0x04: return HID_KEY_H;
        case 0x22: return HID_KEY_I;
        case 0x26: return HID_KEY_J;
        case 0x28: return HID_KEY_K;
        case 0x25: return HID_KEY_L;
        case 0x2E: return HID_KEY_M;
        case 0x2D: return HID_KEY_N;
        case 0x1F: return HID_KEY_O;
        case 0x23: return HID_KEY_P;
        case 0x0C: return HID_KEY_Q;
        case 0x0F: return HID_KEY_R;
        case 0x01: return HID_KEY_S;
        case 0x11: return HID_KEY_T;
        case 0x20: return HID_KEY_U;
        case 0x09: return HID_KEY_V;
        case 0x0D: return HID_KEY_W;
        case 0x07: return HID_KEY_X;
        case 0x10: return HID_KEY_Y;
        case 0x06: return HID_KEY_Z;

        case 0x12: return HID_KEY_1;
        case 0x13: return HID_KEY_2;
        case 0x14: return HID_KEY_3;
        case 0x15: return HID_KEY_4;
        case 0x17: return HID_KEY_5;
        case 0x16: return HID_KEY_6;
        case 0x1A: return HID_KEY_7;
        case 0x1C: return HID_KEY_8;
        case 0x19: return HID_KEY_9;
        case 0x1D: return HID_KEY_0;

        case 0x24: return HID_KEY_ENTER;
        case 0x35: return HID_KEY_ESC;
        case 0x33: return HID_KEY_BACKSPACE;
        case 0x30: return HID_KEY_TAB;
        case 0x31: return HID_KEY_SPACE;
        case 0x1B: return HID_KEY_MINUS;
        case 0x18: return HID_KEY_EQUAL;
        case 0x21: return HID_KEY_LEFTBRACE;
        case 0x1E: return HID_KEY_RIGHTBRACE;
        case 0x2A: return HID_KEY_BACKSLASH;
        case 0x29: return HID_KEY_SEMICOLON;
        case 0x27: return HID_KEY_APOSTROPHE;
        case 0x32: return HID_KEY_GRAVE;
        case 0x2B: return HID_KEY_COMMA;
        case 0x2F: return HID_KEY_DOT;
        case 0x2C: return HID_KEY_SLASH;
        case 0x39: return HID_KEY_CAPSLOCK;
        case 0x0A: return HID_KEY_102ND; // ISO § / ± key

        case 0x7A: return HID_KEY_F1;
        case 0x78: return HID_KEY_F2;
        case 0x63: return HID_KEY_F3;
        case 0x76: return HID_KEY_F4;
        case 0x60: return HID_KEY_F5;
        case 0x61: return HID_KEY_F6;
        case 0x62: return HID_KEY_F7;
        case 0x64: return HID_KEY_F8;
        case 0x65: return HID_KEY_F9;
        case 0x6D: return HID_KEY_F10;
        case 0x67: return HID_KEY_F11;
        case 0x6F: return HID_KEY_F12;
        case 0x69: return HID_KEY_F13;
        case 0x6B: return HID_KEY_F14;
        case 0x71: return HID_KEY_F15;
        case 0x6A: return HID_KEY_F16;
        case 0x40: return HID_KEY_F17;
        case 0x4F: return HID_KEY_F18;
        case 0x50: return HID_KEY_F19;
        case 0x5A: return HID_KEY_F20;

        case 0x72: return HID_KEY_INSERT; // Help
        case 0x73: return HID_KEY_HOME;
        case 0x74: return HID_KEY_PAGEUP;
        case 0x75: return HID_KEY_DELETE; // Forward delete
        case 0x77: return HID_KEY_END;
        case 0x79: return HID_KEY_PAGEDOWN;
        case 0x7B: return HID_KEY_LEFT;
        case 0x7C: return HID_KEY_RIGHT;
        case 0x7D: return HID_KEY_DOWN;
        case 0x7E: return HID_KEY_UP;

        case 0x47: return HID_KEY_NUMLOCK; // Keypad clear
        case 0x4B: return HID_KEY_KPSLASH;
        case 0x43: return HID_KEY_KPASTERISK;
        case 0x4E: return HID_KEY_KPMINUS;
        case 0x45: return HID_KEY_KPPLUS;
        case 0x4C: return HID_KEY_KPENTER;
        case 0x53: return HID_KEY_KP1;
        case 0x54: return HID_KEY_KP2;
        case 0x55: return HID_KEY_KP3;
        case 0x56: return HID_KEY_KP4;
        case 0x57: return HID_KEY_KP5;
        case 0x58: return HID_KEY_KP6;
        case 0x59: return HID_KEY_KP7;
        case 0x5B: return HID_KEY_KP8;
        case 0x5C: return HID_KEY_KP9;
        case 0x52: return HID_KEY_KP0;
        case 0x41: return HID_KEY_KPDOT;
        case 0x51: return HID_KEY_KPEQUAL;

        case 0x4A: return HID_KEY_MUTE;
        case 0x48: return HID_KEY_VOLUMEUP;
        case 0x49: return HID_KEY_VOLUMEDOWN;

        case 0x5D: return HID_KEY_YEN;              // JIS ¥
        case 0x5E: return HID_KEY_RO;               // JIS _
        case 0x68: return HID_KEY_KATAKANAHIRAGANA; // JIS Kana
        case 0x66: return HID_KEY_MUHENKAN;         // JIS Eisu

        // Modifiers (also delivered via flagsChanged: handling)
        case 0x3B: return HID_KEY_LEFTCTRL;
        case 0x3E: return HID_KEY_RIGHTCTRL;
        case 0x38: return HID_KEY_LEFTSHIFT;
        case 0x3C: return HID_KEY_RIGHTSHIFT;
        case 0x3A: return HID_KEY_LEFTALT;
        case 0x3D: return HID_KEY_RIGHTALT;
        case 0x37: return HID_KEY_LEFTMETA;
        case 0x36: return HID_KEY_RIGHTMETA;
        // clang-format on
        default:
            return HID_KEY_NONE;
    }
}

@class RVVMWindow;

/*
 * Framebuffer-blitting, input-handling content view
 */
@interface RVVMView : NSView {
@public
    gui_window_t*   m_win;
    CGColorSpaceRef m_colorspace;

    // Last scanout context (set from draw())
    const void* m_buffer;
    uint32_t    m_width;
    uint32_t    m_height;
    uint32_t    m_stride;
    uint32_t    m_pos_x;
    uint32_t    m_pos_y;
}
@end

@implementation RVVMView

- (instancetype)initWithFrame:(NSRect)frame win:(gui_window_t*)win
{
    self = [super initWithFrame:frame];
    if (self) {
        m_win        = win;
        m_colorspace = CGColorSpaceCreateDeviceRGB();
    }
    return self;
}

- (void)dealloc
{
    if (m_colorspace) {
        CGColorSpaceRelease(m_colorspace);
    }
    [super dealloc];
}

- (BOOL)isOpaque
{
    return YES;
}

- (BOOL)acceptsFirstResponder
{
    return YES;
}

- (BOOL)acceptsFirstMouse:(NSEvent*)event
{
    UNUSED(event);
    return YES;
}

- (void)drawRect:(NSRect)dirty
{
    UNUSED(dirty);
    CGContextRef ctx = (CGContextRef)[[NSGraphicsContext currentContext] CGContext];
    CGFloat      vh  = self.bounds.size.height;

    // Letterbox background
    CGContextSetRGBFillColor(ctx, 0.0, 0.0, 0.0, 1.0);
    CGContextFillRect(ctx, NSRectToCGRect(self.bounds));

    if (!m_buffer || !m_width || !m_height) {
        return;
    }

    // Wrap the live VRAM scanout into a CGImage without copying
    size_t            len  = (size_t)m_stride * m_height;
    CGDataProviderRef prov = CGDataProviderCreateWithData(NULL, m_buffer, len, NULL);
    // XRGB8888 is a8r8g8b8 in a word, i.e. B,G,R,X bytes in memory (little endian)
    CGImageRef img = CGImageCreate(m_width, m_height, 8, 32, m_stride, m_colorspace,
                                   kCGImageAlphaNoneSkipFirst | kCGBitmapByteOrder32Little, //
                                   prov, NULL, false, kCGRenderingIntentDefault);
    if (img) {
        // The view is non-flipped, so CoreGraphics user space is bottom-left
        // origin and CGContextDrawImage already maps the scanout's first (top)
        // row to the top of the dest rect - no CTM flip needed. Translate the
        // top-left letterbox offset into the bottom-left rect origin CG wants.
        CGFloat rect_y = vh - (CGFloat)m_pos_y - (CGFloat)m_height;
        CGContextDrawImage(ctx, CGRectMake(m_pos_x, rect_y, m_width, m_height), img);
        CGImageRelease(img);
    }
    CGDataProviderRelease(prov);
}

/*
 * Mouse handling - report absolute placement in scanout space (HID tablet)
 */
- (void)reportMousePlace:(NSEvent*)event
{
    NSPoint p = [self convertPoint:[event locationInWindow] fromView:nil];
    // Convert from AppKit bottom-left to scanout top-left, minus letterbox offset
    int32_t x = (int32_t)p.x - (int32_t)m_pos_x;
    int32_t y = (int32_t)(self.bounds.size.height - p.y) - (int32_t)m_pos_y;
    x         = EVAL_MAX(EVAL_MIN(x, (int32_t)m_width - 1), 0);
    y         = EVAL_MAX(EVAL_MIN(y, (int32_t)m_height - 1), 0);
    gui_backend_on_mouse_place(m_win, x, y);
}

- (void)mouseMoved:(NSEvent*)event
{
    [self reportMousePlace:event];
}

- (void)mouseDragged:(NSEvent*)event
{
    [self reportMousePlace:event];
}

- (void)rightMouseDragged:(NSEvent*)event
{
    [self reportMousePlace:event];
}

- (void)otherMouseDragged:(NSEvent*)event
{
    [self reportMousePlace:event];
}

- (void)mouseDown:(NSEvent*)event
{
    [self reportMousePlace:event];
    gui_backend_on_mouse_press(m_win, HID_BTN_LEFT);
}

- (void)mouseUp:(NSEvent*)event
{
    [self reportMousePlace:event];
    gui_backend_on_mouse_release(m_win, HID_BTN_LEFT);
}

- (void)rightMouseDown:(NSEvent*)event
{
    [self reportMousePlace:event];
    gui_backend_on_mouse_press(m_win, HID_BTN_RIGHT);
}

- (void)rightMouseUp:(NSEvent*)event
{
    [self reportMousePlace:event];
    gui_backend_on_mouse_release(m_win, HID_BTN_RIGHT);
}

- (void)otherMouseDown:(NSEvent*)event
{
    if ([event buttonNumber] == 2) {
        gui_backend_on_mouse_press(m_win, HID_BTN_MIDDLE);
    }
}

- (void)otherMouseUp:(NSEvent*)event
{
    if ([event buttonNumber] == 2) {
        gui_backend_on_mouse_release(m_win, HID_BTN_MIDDLE);
    }
}

- (void)scrollWheel:(NSEvent*)event
{
    CGFloat dy = [event deltaY];
    if (dy > 0.0) {
        gui_backend_on_mouse_scroll(m_win, HID_SCROLL_UP);
    } else if (dy < 0.0) {
        gui_backend_on_mouse_scroll(m_win, HID_SCROLL_DOWN);
    }
}

/*
 * Keyboard handling
 */
- (void)keyDown:(NSEvent*)event
{
    if (![event isARepeat]) {
        gui_backend_on_key_press(m_win, cocoa_key_to_hid([event keyCode]));
    }
}

- (void)keyUp:(NSEvent*)event
{
    gui_backend_on_key_release(m_win, cocoa_key_to_hid([event keyCode]));
}

- (void)flagsChanged:(NSEvent*)event
{
    // Modifier keys arrive here; press state is derived from device-dependent bits
    static const struct {
        uint32_t  mask;
        hid_key_t key;
    } mods[] = {
        {COCOA_MOD_LCTRL,  HID_KEY_LEFTCTRL  },
        {COCOA_MOD_RCTRL,  HID_KEY_RIGHTCTRL },
        {COCOA_MOD_LSHIFT, HID_KEY_LEFTSHIFT },
        {COCOA_MOD_RSHIFT, HID_KEY_RIGHTSHIFT},
        {COCOA_MOD_LALT,   HID_KEY_LEFTALT   },
        {COCOA_MOD_RALT,   HID_KEY_RIGHTALT  },
        {COCOA_MOD_LMETA,  HID_KEY_LEFTMETA  },
        {COCOA_MOD_RMETA,  HID_KEY_RIGHTMETA },
    };
    uint32_t flags = (uint32_t)[event modifierFlags];
    for (size_t i = 0; i < STATIC_ARRAY_SIZE(mods); ++i) {
        if (flags & mods[i].mask) {
            gui_backend_on_key_press(m_win, mods[i].key);
        } else {
            gui_backend_on_key_release(m_win, mods[i].key);
        }
    }
    // Caps Lock reports toggle state, not key state - emit a tap on each change
    if (flags & COCOA_MOD_CAPS) {
        gui_backend_on_key_press(m_win, HID_KEY_CAPSLOCK);
    } else {
        gui_backend_on_key_release(m_win, HID_KEY_CAPSLOCK);
    }
}

@end

/*
 * Window, also acts as its own delegate for close / focus events
 */
@interface RVVMWindow : NSWindow <NSWindowDelegate> {
@public
    gui_window_t* m_win;
    RVVMView*     m_view;
}
@end

@implementation RVVMWindow

- (BOOL)canBecomeKeyWindow
{
    return YES;
}

- (BOOL)canBecomeMainWindow
{
    return YES;
}

- (BOOL)windowShouldClose:(id)sender
{
    UNUSED(sender);
    gui_backend_on_close(m_win);
    // RVVM decides what closing means (reset / poweroff), don't destroy here
    return NO;
}

- (void)windowDidResignKey:(NSNotification*)notification
{
    UNUSED(notification);
    gui_backend_on_focus_lost(m_win);
}

@end

typedef struct {
    RVVMWindow* window;
    RVVMView*   view;
    bool        cursor_hidden;
    bool        grab;
} cocoa_window_t;

static bool cocoa_global_init(void)
{
    DO_ONCE_SCOPED {
        [NSApplication sharedApplication];
        // Foreground app so the window can receive focus & key events without a bundle
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        [NSApp finishLaunching];
        [NSApp activateIgnoringOtherApps:YES];
    }
    return NSApp != nil;
}

// Convert a top-left origin screen point into AppKit's bottom-left frame origin
static NSRect cocoa_frame_from_topleft(RVVMWindow* window, int32_t x, int32_t y)
{
    NSRect  frame  = [window frame];
    NSRect  screen = [[NSScreen mainScreen] frame];
    frame.origin.x = x;
    frame.origin.y = screen.size.height - y - frame.size.height;
    return frame;
}

static void cocoa_window_free(gui_window_t* win)
{
    cocoa_window_t* cocoa = gui_backend_get_data(win);
    if (cocoa) {
        if (cocoa->window) {
            [cocoa->window setDelegate:nil];
            [cocoa->window close];
            [cocoa->window release];
        }
        if (cocoa->cursor_hidden) {
            [NSCursor unhide];
        }
        // Free VRAM VMA
        vma_free(gui_backend_get_vram(win), gui_backend_get_vram_size(win));
        safe_free(cocoa);
    }
    gui_backend_set_data(win, NULL);
}

static void cocoa_window_poll(gui_window_t* win)
{
    UNUSED(win);
    @autoreleasepool {
        NSEvent* event = nil;
        while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                           untilDate:[NSDate distantPast]
                                              inMode:NSDefaultRunLoopMode
                                             dequeue:YES])) {
            [NSApp sendEvent:event];
        }
    }
}

static void cocoa_window_draw(gui_window_t* win, const rvvm_fb_t* fb, uint32_t x, uint32_t y)
{
    cocoa_window_t* cocoa = gui_backend_get_data(win);
    RVVMView*       view  = cocoa->view;

    view->m_buffer = rvvm_fb_buffer(fb);
    view->m_width  = rvvm_fb_width(fb);
    view->m_height = rvvm_fb_height(fb);
    view->m_stride = rvvm_fb_stride(fb);
    view->m_pos_x  = x;
    view->m_pos_y  = y;

    // GUI user expects a synchronous flip
    [view setNeedsDisplay:YES];
    [view displayIfNeeded];
}

static void cocoa_window_set_title(gui_window_t* win, const char* title)
{
    cocoa_window_t* cocoa = gui_backend_get_data(win);
    @autoreleasepool {
        [cocoa->window setTitle:[NSString stringWithUTF8String:title]];
    }
}

static void cocoa_window_grab_input(gui_window_t* win, bool grab)
{
    cocoa_window_t* cocoa = gui_backend_get_data(win);
    cocoa->grab           = grab;
    // Detach the hardware cursor so relative input isn't clipped to the screen
    CGAssociateMouseAndMouseCursorPosition(!grab);
    CGDisplayHideCursor(kCGDirectMainDisplay);
    if (!grab) {
        CGDisplayShowCursor(kCGDirectMainDisplay);
    }
}

static void cocoa_window_hide_cursor(gui_window_t* win, bool hide)
{
    cocoa_window_t* cocoa = gui_backend_get_data(win);
    if (cocoa->cursor_hidden != hide) {
        cocoa->cursor_hidden = hide;
        if (hide) {
            [NSCursor hide];
        } else {
            [NSCursor unhide];
        }
    }
}

static void cocoa_window_set_win_size(gui_window_t* win, uint32_t w, uint32_t h)
{
    cocoa_window_t* cocoa = gui_backend_get_data(win);
    [cocoa->window setContentSize:NSMakeSize(w, h)];
}

static void cocoa_window_set_min_size(gui_window_t* win, uint32_t w, uint32_t h)
{
    cocoa_window_t* cocoa = gui_backend_get_data(win);
    [cocoa->window setContentMinSize:NSMakeSize(w, h)];
}

static void cocoa_window_get_position(gui_window_t* win, int32_t* x, int32_t* y)
{
    cocoa_window_t* cocoa  = gui_backend_get_data(win);
    NSRect          frame  = [cocoa->window frame];
    NSRect          screen = [[NSScreen mainScreen] frame];
    *x                     = (int32_t)frame.origin.x;
    *y                     = (int32_t)(screen.size.height - frame.origin.y - frame.size.height);
}

static void cocoa_window_set_position(gui_window_t* win, int32_t x, int32_t y)
{
    cocoa_window_t* cocoa = gui_backend_get_data(win);
    [cocoa->window setFrame:cocoa_frame_from_topleft(cocoa->window, x, y) display:NO];
}

static void cocoa_window_get_scr_size(gui_window_t* win, uint32_t* w, uint32_t* h)
{
    UNUSED(win);
    NSRect frame = [[NSScreen mainScreen] visibleFrame];
    *w           = (uint32_t)frame.size.width;
    *h           = (uint32_t)frame.size.height;
}

static void cocoa_window_set_fullscreen(gui_window_t* win, bool fullscreen)
{
    cocoa_window_t* cocoa  = gui_backend_get_data(win);
    bool            is_now = ([cocoa->window styleMask] & NSWindowStyleMaskFullScreen) != 0;
    if (is_now != fullscreen) {
        [cocoa->window toggleFullScreen:nil];
    }
}

static const gui_backend_cb_t cocoa_window_cb = {
    .free          = cocoa_window_free,
    .poll          = cocoa_window_poll,
    .draw          = cocoa_window_draw,
    .set_title     = cocoa_window_set_title,
    .grab_input    = cocoa_window_grab_input,
    .hide_cursor   = cocoa_window_hide_cursor,
    .set_win_size  = cocoa_window_set_win_size,
    .set_min_size  = cocoa_window_set_min_size,
    .get_position  = cocoa_window_get_position,
    .set_position  = cocoa_window_set_position,
    .get_scr_size  = cocoa_window_get_scr_size,
    .set_fullscreen = cocoa_window_set_fullscreen,
};

bool cocoa_window_init(gui_window_t* win)
{
    gui_backend_register(win, &cocoa_window_cb);

    if (!cocoa_global_init()) {
        rvvm_error("Failed to initialize NSApplication");
        return false;
    }

    @autoreleasepool {
        cocoa_window_t* cocoa = safe_new_obj(cocoa_window_t);
        gui_backend_set_data(win, cocoa);

        // Allocate VRAM the framebuffer device scans out of
        size_t vram_size = gui_backend_get_vram_size(win);
        void*  vram      = vma_alloc(NULL, vram_size, VMA_RDWR);
        if (vram) {
            gui_backend_set_vram(win, vram, vram_size);
        } else {
            rvvm_error("vma_alloc() failed!");
            return false;
        }

        NSRect          rect  = NSMakeRect(0, 0, gui_window_width(win), gui_window_height(win));
        NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable //
                                | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;

        RVVMWindow* window = [[RVVMWindow alloc] initWithContentRect:rect
                                                          styleMask:style
                                                            backing:NSBackingStoreBuffered
                                                              defer:NO];
        window->m_win      = win;
        [window setDelegate:window];
        [window setAcceptsMouseMovedEvents:YES];
        [window setReleasedWhenClosed:NO];
        [window setOpaque:YES];
        [window setBackgroundColor:[NSColor blackColor]];

        RVVMView* view = [[RVVMView alloc] initWithFrame:rect win:win];
        [view setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
        window->m_view = view;
        cocoa->view    = view;

        [window setContentView:view];
        [window makeFirstResponder:view];
        [view release];

        cocoa->window = window;

        [window center];
        [window makeKeyAndOrderFront:nil];
    }

    return true;
}

#else

bool cocoa_window_init(gui_window_t* win)
{
    UNUSED(win);
    return false;
}

#endif

POP_OPTIMIZATION_SIZE
