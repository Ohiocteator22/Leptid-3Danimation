// Leptid — X11 platform backend (Linux). Simple, dependency-light:
// XPutImage presentation, full input mapping.
#ifdef LEPTID_HAS_X11

#include "platform/platform.h"
#include "formats/pngio.h"
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <cmath>
#include <cstring>
#include <thread>
#include <chrono>

namespace leptid_embed {
extern const unsigned char leptid_icon_64_png[];
extern const unsigned leptid_icon_64_png_len;
}

namespace leptid {

namespace {

Key mapKeysym(KeySym ks) {
    switch (ks) {
        case XK_Left: return Key::Left;
        case XK_Right: return Key::Right;
        case XK_Up: return Key::Up;
        case XK_Down: return Key::Down;
        case XK_Return: return Key::Enter;
        case XK_Escape: return Key::Escape;
        case XK_Delete: return Key::Delete;
        case XK_BackSpace: return Key::Backspace;
        case XK_Tab: return Key::Tab;
        case XK_space: return Key::Space;
        case XK_Control_L: case XK_Control_R: return Key::Ctrl;
        case XK_Shift_L: case XK_Shift_R: return Key::Shift;
        default: break;
    }
    if (ks >= XK_a && ks <= XK_z) return (Key)((int)Key::A + (int)(ks - XK_a));
    if (ks >= XK_A && ks <= XK_Z) return (Key)((int)Key::A + (int)(ks - XK_A));
    if (ks >= XK_0 && ks <= XK_9) return (Key)((int)Key::Num0 + (int)(ks - XK_0));
    return Key::Invalid;
}

class X11Window : public PlatformWindow {
public:
    X11Window(int w, int h, const char* title) : w_(w), h_(h) {
        d_ = XOpenDisplay(nullptr);
        if (!d_) { ok_ = false; return; }
        Window root = DefaultRootWindow(d_);
        win_ = XCreateSimpleWindow(d_, root, 40, 40, w, h, 1,
                                   BlackPixel(d_, DefaultScreen(d_)),
                                   WhitePixel(d_, DefaultScreen(d_)));
        XStoreName(d_, win_, title);
        XSelectInput(d_, win_, ExposureMask | KeyPressMask | KeyReleaseMask |
                     ButtonPressMask | ButtonReleaseMask | PointerMotionMask |
                     StructureNotifyMask);
        Atom del = XInternAtom(d_, "WM_DELETE_WINDOW", False);
        XSetWMProtocols(d_, win_, &del, 1);
        XMapWindow(d_, win_);
        XFlush(d_);
        // window icon (_NET_WM_ICON): embedded app icon as ARGB cardinals
        {
            Image icon;
            if (loadPngMemory(leptid_embed::leptid_icon_64_png, leptid_embed::leptid_icon_64_png_len, icon)) {
                std::vector<unsigned long> data;
                data.push_back((unsigned long)icon.w);
                data.push_back((unsigned long)icon.h);
                for (uint32_t p2 : icon.px) data.push_back((unsigned long)p2); // 0xAARRGGBB as required
                Atom netIcon = XInternAtom(d_, "_NET_WM_ICON", False);
                XChangeProperty(d_, win_, netIcon, XA_CARDINAL, 32, PropModeReplace,
                                (unsigned char*)data.data(), (int)data.size());
                XFlush(d_);
            }
        }
        // scratch buffer in X's expected byte order (BGRA on little-endian ZPixmap)
        scratch_.resize((size_t)w * h);
        start_ = std::chrono::steady_clock::now();
        ok_ = true;
    }
    ~X11Window() override {
        if (d_) {
            XDestroyWindow(d_, win_);
            XCloseDisplay(d_);
        }
    }
    bool ok() const { return ok_; }

    bool pollEvents(std::vector<InputEvent>& out) override {
        while (XPending(d_)) {
            XEvent e;
            XNextEvent(d_, &e);
            switch (e.type) {
                case ClientMessage:
                    if ((Atom)e.xclient.data.l[0] == deleteAtom()) return false;
                    break;
                case MotionNotify:
                    out.push_back({InputEvent::Type::MouseMove, (float)e.xmotion.x, (float)e.xmotion.y, 0, 0, Key::Invalid, 0, 0, 0});
                    break;
                case ButtonPress: {
                    if (e.xbutton.button == Button4 || e.xbutton.button == Button5) {
                        InputEvent ev;
                        ev.type = InputEvent::Type::Wheel;
                        ev.x = (float)e.xbutton.x; ev.y = (float)e.xbutton.y;
                        ev.wheelDelta = e.xbutton.button == Button4 ? 1 : -1;
                        out.push_back(ev);
                    } else {
                        InputEvent ev;
                        ev.type = InputEvent::Type::MouseDown;
                        ev.x = (float)e.xbutton.x; ev.y = (float)e.xbutton.y;
                        ev.button = e.xbutton.button;
                        out.push_back(ev);
                    }
                    break;
                }
                case ButtonRelease: {
                    InputEvent ev;
                    ev.type = InputEvent::Type::MouseUp;
                    ev.x = (float)e.xbutton.x; ev.y = (float)e.xbutton.y;
                    ev.button = e.xbutton.button;
                    out.push_back(ev);
                    break;
                }
                case KeyPress:
                case KeyRelease: {
                    char buf[16] = {0};
                    KeySym ks = 0;
                    int n = XLookupString(&e.xkey, buf, sizeof buf - 1, &ks, nullptr);
                    Key k = mapKeysym(ks);
                    bool ctrl = (e.xkey.state & ControlMask) != 0;
                    bool shift = (e.xkey.state & ShiftMask) != 0;
                    if (ctrl) {
                        InputEvent ev; ev.type = e.type == KeyPress ? InputEvent::Type::KeyDown : InputEvent::Type::KeyUp;
                        ev.key = Key::Ctrl; out.push_back(ev);
                    }
                    if (shift) {
                        InputEvent ev; ev.type = e.type == KeyPress ? InputEvent::Type::KeyDown : InputEvent::Type::KeyUp;
                        ev.key = Key::Shift; out.push_back(ev);
                    }
                    InputEvent ev;
                    ev.type = e.type == KeyPress ? InputEvent::Type::KeyDown : InputEvent::Type::KeyUp;
                    ev.key = k;
                    out.push_back(ev);
                    if (e.type == KeyPress && n > 0 && buf[0] >= 32 && buf[0] < 127) {
                        InputEvent cev;
                        cev.type = InputEvent::Type::Char;
                        cev.codepoint = (uint32_t)buf[0];
                        out.push_back(cev);
                    }
                    break;
                }
                default: break;
            }
        }
        return true;
    }

    void present(const Image& fb) override {
        if (fb.w != w_ || fb.h != h_) {
            // resized: rescale scratch
            w_ = fb.w; h_ = fb.h;
            scratch_.resize((size_t)w_ * h_);
        }
        // X11 ZPixmap (24-bit, little-endian) stores pixel values as 0x00RRGGBB,
        // i.e. bytes [B, G, R, x].
        for (size_t i = 0; i < (size_t)w_ * h_; ++i) {
            uint32_t p = fb.px[i];
            uint8_t r = (uint8_t)(p & 0xFF), g = (uint8_t)((p >> 8) & 0xFF),
                    b = (uint8_t)((p >> 16) & 0xFF);
            scratch_[i] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        }
        XImage* img = XCreateImage(d_, DefaultVisual(d_, DefaultScreen(d_)), 24, ZPixmap, 0,
                                   (char*)scratch_.data(), w_, h_, 32, 0);
        if (!img) return;
        XPutImage(d_, win_, DefaultGC(d_, DefaultScreen(d_)), img, 0, 0, 0, 0, w_, h_);
        // XDestroyImage frees img->data; detach ours first (scratch_ owns it).
        img->data = nullptr;
        XDestroyImage(img);
        XFlush(d_);
    }

    double timeSeconds() const override {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<double>(now - start_).count();
    }

private:
    Atom deleteAtom() {
        if (!del_) del_ = XInternAtom(d_, "WM_DELETE_WINDOW", False);
        return del_;
    }
    Display* d_ = nullptr;
    Window win_ = 0;
    Atom del_ = 0;
    int w_, h_;
    bool ok_ = false;
    std::vector<uint32_t> scratch_;
    std::chrono::steady_clock::time_point start_;
};

} // namespace

std::unique_ptr<PlatformWindow> PlatformWindow::create(int w, int h, const char* title) {
    auto win = std::make_unique<X11Window>(w, h, title);
    if (!win->ok()) return nullptr;
    return win;
}

} // namespace leptid

#endif // LEPTID_HAS_X11
