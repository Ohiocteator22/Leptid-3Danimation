// Leptid — native macOS backend (AppKit/Cocoa). Zero external dependencies:
// Cocoa is a system framework, so the universal (arm64 + x86_64) build links
// against it on every Mac with no third-party dylibs and no X11.
//
// Structure mirrors platform_x11.cpp / platform_win32.cpp: one NSWindow with a
// custom NSView that blits the RGBA framebuffer (drawRect), a manual event pump
// inside pollEvents(), and the same InputEvent mapping as the other backends.
//
// IMPORTANT (from the first CI run): Objective-C @interface/@implementation
// blocks may ONLY appear at global scope — they must NOT sit inside a C++
// namespace ("Objective-C declarations may only appear in global scope").
// The ObjC classes below are global; only MacWindow (plain C++) lives in
// namespace leptid. Compiled with ARC (set in CMakeLists).
#ifdef LEPTID_HAS_COCOA

#include "platform/platform.h"
#import <Cocoa/Cocoa.h>
#include <chrono>
#include <cstring>
#include <vector>

// The global-scope ObjC classes below handle input and draw using the editor's
// types; this using-directive keeps their bodies readable.
using namespace leptid;

// --- helpers (global scope, internal linkage) --------------------------------

// Hardware keycode -> Key (specials first, then a QWERTY letter table —
// Apple's letter keycodes are NOT contiguous across rows).
static Key mapKeyCode(unsigned short kc) {
    switch (kc) {
        case 49: return Key::Space;
        case 123: return Key::Left;
        case 124: return Key::Right;
        case 126: return Key::Up;
        case 125: return Key::Down;
        case 36: case 76: return Key::Enter; // Return / keypad Enter
        case 53: return Key::Escape;
        case 117: return Key::Delete;        // Forward Delete
        case 51: return Key::Backspace;
        case 48: return Key::Tab;
        case 122: return Key::F1;
        case 18: return Key::Num1;
        case 19: return Key::Num2;
        case 20: return Key::Num3;
        case 21: return Key::Num4;
        case 23: return Key::Num5;
        case 22: return Key::Num6;
        case 26: return Key::Num7;
        case 28: return Key::Num8;
        case 25: return Key::Num9;
        case 29: return Key::Num0;
        default: break;
    }
    static const struct { unsigned short code; Key key; } letters[] = {
        {0, Key::A}, {11, Key::B}, {8, Key::C}, {2, Key::D}, {14, Key::E}, {3, Key::F},
        {5, Key::G}, {4, Key::H}, {34, Key::I}, {38, Key::J}, {40, Key::K}, {37, Key::L},
        {46, Key::M}, {45, Key::N}, {31, Key::O}, {35, Key::P}, {12, Key::Q}, {15, Key::R},
        {1, Key::S}, {17, Key::T}, {32, Key::U}, {9, Key::V}, {13, Key::W}, {7, Key::X},
        {16, Key::Y}, {6, Key::Z},
    };
    for (const auto& l : letters)
        if (l.code == kc) return l.key;
    return Key::Invalid;
}

static InputEvent mouseEvent(InputEvent::Type t, NSEvent* ev, int button) {
    InputEvent e;
    e.type = t;
    NSPoint p = ev.locationInWindow;
    // AppKit origin is bottom-left; the UI expects top-left like X11/Win32.
    NSRect bounds = ev.window.contentView.bounds;
    e.x = (float)p.x;
    e.y = (float)(bounds.size.height - p.y);
    e.button = button;
    return e;
}

// --- ObjC classes (global scope) ----------------------------------------------

// Window delegate: the red close button sets the quit flag.
@interface LeptidDelegate : NSObject <NSWindowDelegate>
@end
@implementation LeptidDelegate {
@public
    bool* quitFlag;
}
- (BOOL)windowShouldClose:(id)sender {
    (void)sender;
    if (quitFlag) *quitFlag = true;
    return YES;
}
@end

// The view owns the pixel buffer and blits it in drawRect; input handlers push
// InputEvents into a pending queue drained by pollEvents (single-threaded:
// pollEvents and AppKit callbacks run on the same main thread).
@interface LeptidView : NSView
@end
@implementation LeptidView {
@public
    NSBitmapImageRep* rep_;
    std::vector<InputEvent>* pending_;
    bool ctrlDown_, shiftDown_;
}

- (BOOL)acceptsFirstResponder { return YES; }

// Defined first so every caller below sees it.
- (void)pushEvent:(const InputEvent&)e {
    if (pending_) pending_->push_back(e);
}

- (void)keyEvent:(NSEvent*)ev down:(BOOL)down {
    Key k = mapKeyCode(ev.keyCode);
    InputEvent::Type t = down ? InputEvent::Type::KeyDown : InputEvent::Type::KeyUp;
    if (k != Key::Invalid) {
        InputEvent e;
        e.type = t;
        e.key = k;
        [self pushEvent:e];
    }
    // Printable char event, mirroring X11: skip when Ctrl/Cmd is held
    // (shortcuts are delivered as key events, not text).
    NSEventModifierFlags m = ev.modifierFlags;
    if (down && !(m & NSEventModifierFlagControl) && !(m & NSEventModifierFlagCommand)) {
        NSString* chars = ev.characters;
        if (chars.length > 0) {
            uint32_t cp = (uint32_t)[chars characterAtIndex:0];
            if (cp >= 32 && cp < 127) {
                InputEvent e;
                e.type = InputEvent::Type::Char;
                e.codepoint = cp;
                [self pushEvent:e];
            }
        }
    }
}

- (void)keyDown:(NSEvent*)ev  { [self keyEvent:ev down:YES]; }
- (void)keyUp:(NSEvent*)ev    { [self keyEvent:ev down:NO]; }

// Modifier transitions -> explicit Ctrl/Shift KeyDown/KeyUp, matching the
// X11 backend's behaviour so keyboard shortcuts behave identically.
- (void)flagsChanged:(NSEvent*)ev {
    NSEventModifierFlags m = ev.modifierFlags;
    bool ctrl = (m & NSEventModifierFlagControl) != 0;
    bool shift = (m & NSEventModifierFlagShift) != 0;
    if (ctrl != ctrlDown_) {
        InputEvent e;
        e.type = ctrl ? InputEvent::Type::KeyDown : InputEvent::Type::KeyUp;
        e.key = Key::Ctrl;
        [self pushEvent:e];
        ctrlDown_ = ctrl;
    }
    if (shift != shiftDown_) {
        InputEvent e;
        e.type = shift ? InputEvent::Type::KeyDown : InputEvent::Type::KeyUp;
        e.key = Key::Shift;
        [self pushEvent:e];
        shiftDown_ = shift;
    }
}

- (void)mouseMoved:(NSEvent*)ev        { [self pushEvent:mouseEvent(InputEvent::Type::MouseMove, ev, 0)]; }
- (void)mouseDragged:(NSEvent*)ev      { [self pushEvent:mouseEvent(InputEvent::Type::MouseMove, ev, 0)]; }
- (void)rightMouseDragged:(NSEvent*)ev { [self pushEvent:mouseEvent(InputEvent::Type::MouseMove, ev, 0)]; }
- (void)otherMouseDragged:(NSEvent*)ev { [self pushEvent:mouseEvent(InputEvent::Type::MouseMove, ev, 0)]; }

- (void)mouseDown:(NSEvent*)ev        { [self pushEvent:mouseEvent(InputEvent::Type::MouseDown, ev, 1)]; }
- (void)rightMouseDown:(NSEvent*)ev   { [self pushEvent:mouseEvent(InputEvent::Type::MouseDown, ev, 3)]; }
- (void)otherMouseDown:(NSEvent*)ev   { [self pushEvent:mouseEvent(InputEvent::Type::MouseDown, ev, 2)]; }
- (void)mouseUp:(NSEvent*)ev          { [self pushEvent:mouseEvent(InputEvent::Type::MouseUp, ev, 1)]; }
- (void)rightMouseUp:(NSEvent*)ev     { [self pushEvent:mouseEvent(InputEvent::Type::MouseUp, ev, 3)]; }
- (void)otherMouseUp:(NSEvent*)ev     { [self pushEvent:mouseEvent(InputEvent::Type::MouseUp, ev, 2)]; }

- (void)scrollWheel:(NSEvent*)ev {
    InputEvent e;
    e.type = InputEvent::Type::Wheel;
    NSPoint p = ev.locationInWindow;
    NSRect bounds = ev.window.contentView.bounds;
    e.x = (float)p.x;
    e.y = (float)(bounds.size.height - p.y);
    e.wheelDelta = ev.deltaY > 0 ? 1 : (ev.deltaY < 0 ? -1 : 0);
    [self pushEvent:e];
}

- (void)drawRect:(NSRect)dirty {
    (void)dirty;
    [[NSGraphicsContext currentContext] setImageInterpolation:NSImageInterpolationNone];
    if (rep_) {
        [rep_ drawInRect:self.bounds];
    } else {
        [[NSColor colorWithCalibratedRed:0.09 green:0.09 blue:0.11 alpha:1.0] setFill];
        NSBezierPath* b = [NSBezierPath bezierPathWithRect:self.bounds];
        [b fill];
    }
}

@end

// --- C++ window class ----------------------------------------------------------

namespace leptid {

class MacWindow : public PlatformWindow {
public:
    MacWindow(int w, int h, const char* title) {
        NSApplication* app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyRegular];
        [app activateIgnoringOtherApps:YES];

        // Minimal app menu: without it there is no Quit item / Cmd-Q handling.
        NSMenu* menubar = [[NSMenu alloc] init];
        NSMenuItem* appItem = [[NSMenuItem alloc] init];
        [menubar addItem:appItem];
        NSMenu* appMenu = [[NSMenu alloc] init];
        [appMenu addItemWithTitle:@"Quit Leptid" action:@selector(terminate:) keyEquivalent:@"q"];
        [appItem setSubmenu:appMenu];
        [app setMainMenu:menubar];

        NSUInteger style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                           NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
        NSRect frame = NSMakeRect(0, 0, (CGFloat)w, (CGFloat)h);
        NSWindow* win = [[NSWindow alloc] initWithContentRect:frame
                                                    styleMask:style
                                                      backing:NSBackingStoreBuffered
                                                        defer:NO];
        win.title = [NSString stringWithUTF8String:title];
        win.releasedWhenClosed = NO;
        win.acceptsMouseMovedEvents = YES;

        view_ = [[LeptidView alloc] initWithFrame:frame];
        view_->pending_ = &pending_;
        view_.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        win.contentView = view_;
        [win center];

        delegate_ = [[LeptidDelegate alloc] init];
        delegate_->quitFlag = &quit_;
        win.delegate = delegate_;

        [win makeKeyAndOrderFront:nil];
        [app finishLaunching];

        window_ = win;
        start_ = std::chrono::steady_clock::now();
        ok_ = win != nil;
    }
    ~MacWindow() override {
        if (window_) {
            [window_ setDelegate:nil];
            [window_ performClose:nil];
        }
    }
    bool ok() const { return ok_; }

    bool pollEvents(std::vector<InputEvent>& out) override {
        // Manual event pump: drain AppKit's queue, dispatch to the view, then
        // flush any pending draw requests synchronously.
        NSApplication* app = [NSApplication sharedApplication];
        while (true) {
            NSEvent* ev = [app nextEventMatchingMask:NSEventMaskAny
                                           untilDate:[NSDate distantPast]
                                              inMode:NSDefaultRunLoopMode
                                             dequeue:YES];
            if (!ev) break;
            [app sendEvent:ev];
        }
        [app updateWindows];
        for (const InputEvent& e : pending_) out.push_back(e);
        pending_.clear();
        return !quit_;
    }

    void present(const Image& fb) override {
        if (fb.empty()) return;
        if (!rep_ || rep_.pixelsWide != (NSInteger)fb.w || rep_.pixelsHigh != (NSInteger)fb.h) {
            rep_ = [[NSBitmapImageRep alloc]
                initWithBitmapDataPlanes:nil
                              pixelsWide:(NSInteger)fb.w
                              pixelsHigh:(NSInteger)fb.h
                           bitsPerSample:8
                         samplesPerPixel:4
                                hasAlpha:YES
                                isPlanar:NO
                          colorSpaceName:NSCalibratedRGBColorSpace
                            bytesPerRow:(NSInteger)fb.w * 4
                           bitsPerPixel:32];
        }
        // Our Image stores R,G,B,A — exactly NSCalibratedRGB + alpha byte order.
        unsigned char* dst = rep_.bitmapData;
        if (dst) memcpy(dst, fb.px.data(), (size_t)fb.w * fb.h * 4);
        view_->rep_ = rep_; // drawRect blits the view's ivar — keep it in sync
        [view_ setNeedsDisplay:YES];
        [view_ displayIfNeeded];
    }

    double timeSeconds() const override {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
    }

private:
    NSWindow* window_ = nil;
    LeptidView* view_ = nil;
    LeptidDelegate* delegate_ = nil;
    NSBitmapImageRep* rep_ = nil;
    std::vector<InputEvent> pending_;
    bool quit_ = false;
    bool ok_ = false;
    std::chrono::steady_clock::time_point start_;
};

std::unique_ptr<PlatformWindow> PlatformWindow::create(int w, int h, const char* title) {
    auto win = std::make_unique<MacWindow>(w, h, title);
    if (!win->ok()) return nullptr;
    return win;
}

} // namespace leptid

#endif // LEPTID_HAS_COCOA
