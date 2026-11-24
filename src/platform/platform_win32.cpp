// Leptid — Win32 platform backend (Windows, the primary target).
// Compiled only under Windows (`#ifdef _WIN32`); the interactive backend is
// verified on Linux/X11 — see README for the honest test matrix.
#ifdef _WIN32

#include "platform/platform.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <chrono>
#include <cstring>
#include <vector>

namespace leptid {

namespace {

Key mapVk(WPARAM vk) {
    switch (vk) {
        case VK_LEFT: return Key::Left;
        case VK_RIGHT: return Key::Right;
        case VK_UP: return Key::Up;
        case VK_DOWN: return Key::Down;
        case VK_RETURN: return Key::Enter;
        case VK_ESCAPE: return Key::Escape;
        case VK_DELETE: return Key::Delete;
        case VK_BACK: return Key::Backspace;
        case VK_SPACE: return Key::Space;
        case VK_TAB: return Key::Tab;
        case VK_CONTROL: return Key::Ctrl;
        case VK_SHIFT: return Key::Shift;
        default: break;
    }
    if (vk >= 'A' && vk <= 'Z') return (Key)((int)Key::A + (int)(vk - 'A'));
    if (vk >= '0' && vk <= '3') return (Key)((int)Key::Num0 + (int)(vk - '0'));
    if (vk == VK_F1) return Key::F1;
    return Key::Invalid;
}

struct SharedState {
    HWND hwnd = nullptr;
    int w = 0, h = 0;
    BITMAPINFO bmi{};
    std::vector<uint32_t> pixels;  // converted for DIB (0x00BBGGRR)
    bool quit = false;
    std::vector<InputEvent> pending;
    std::chrono::steady_clock::time_point start;
};

static SharedState g;

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_DESTROY:
            g.quit = true;
            PostQuitMessage(0);
            return 0;
        case WM_SIZE:
            g.w = LOWORD(lp);
            g.h = HIWORD(lp);
            g.pixels.resize((size_t)g.w * g.h);
            return 0;
        case WM_MOUSEMOVE: {
            InputEvent ev;
            ev.type = InputEvent::Type::MouseMove;
            ev.x = (float)(short)LOWORD(lp);
            ev.y = (float)(short)HIWORD(lp);
            g.pending.push_back(ev);
            return 0;
        }
        case WM_LBUTTONDOWN: case WM_MBUTTONDOWN: case WM_RBUTTONDOWN: {
            InputEvent ev;
            ev.type = InputEvent::Type::MouseDown;
            ev.button = msg == WM_LBUTTONDOWN ? 1 : msg == WM_MBUTTONDOWN ? 2 : 3;
            ev.x = (float)(short)LOWORD(lp);
            ev.y = (float)(short)HIWORD(lp);
            g.pending.push_back(ev);
            SetCapture(hwnd);
            return 0;
        }
        case WM_LBUTTONUP: case WM_MBUTTONUP: case WM_RBUTTONUP: {
            InputEvent ev;
            ev.type = InputEvent::Type::MouseUp;
            ev.button = msg == WM_LBUTTONUP ? 1 : msg == WM_MBUTTONUP ? 2 : 3;
            ev.x = (float)(short)LOWORD(lp);
            ev.y = (float)(short)HIWORD(lp);
            g.pending.push_back(ev);
            ReleaseCapture();
            return 0;
        }
        case WM_MOUSEWHEEL: {
            InputEvent ev;
            ev.type = InputEvent::Type::Wheel;
            ev.wheelDelta = (float)GET_WHEEL_DELTA_WPARAM(wp) / (float)WHEEL_DELTA;
            POINT pt{(short)LOWORD(lp), (short)HIWORD(lp)};
            ScreenToClient(hwnd, &pt);
            ev.x = (float)pt.x;
            ev.y = (float)pt.y;
            g.pending.push_back(ev);
            return 0;
        }
        case WM_KEYDOWN: {
            Key k = mapVk(wp);
            if (k != Key::Invalid) {
                InputEvent ev;
                ev.type = InputEvent::Type::KeyDown;
                ev.key = k;
                g.pending.push_back(ev);
            }
            return 0;
        }
        case WM_KEYUP: {
            Key k = mapVk(wp);
            if (k != Key::Invalid) {
                InputEvent ev;
                ev.type = InputEvent::Type::KeyUp;
                ev.key = k;
                g.pending.push_back(ev);
            }
            return 0;
        }
        case WM_CHAR: {
            if (wp >= 32 && wp < 127) {
                InputEvent ev;
                ev.type = InputEvent::Type::Char;
                ev.codepoint = (uint32_t)wp;
                g.pending.push_back(ev);
            }
            return 0;
        }
        default: break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

class Win32Window : public PlatformWindow {
public:
    Win32Window(int w, int h, const char* title) {
        g.w = w;
        g.h = h;
        g.pixels.resize((size_t)w * h);
        g.start = std::chrono::steady_clock::now();
        WNDCLASSW wc{};
        wc.lpfnWndProc = wndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"LeptidWindow";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassW(&wc);
        RECT r{0, 0, w, h};
        AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
        std::wstring wtitle(title, title + strlen(title));
        g.hwnd = CreateWindowW(L"LeptidWindow", wtitle.c_str(), WS_OVERLAPPEDWINDOW,
                               CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                               nullptr, nullptr, wc.hInstance, nullptr);
        g.bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        g.bmi.bmiHeader.biWidth = w;
        g.bmi.bmiHeader.biHeight = -h; // top-down rows
        g.bmi.bmiHeader.biPlanes = 1;
        g.bmi.bmiHeader.biBitCount = 32;
        g.bmi.bmiHeader.biCompression = BI_RGB;
        ShowWindow(g.hwnd, SW_SHOW);
        ok_ = g.hwnd != nullptr;
    }
    ~Win32Window() override = default;
    bool ok() const { return ok_; }

    bool pollEvents(std::vector<InputEvent>& out) override {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) return false;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        for (const InputEvent& e : g.pending) out.push_back(e);
        g.pending.clear();
        return !g.quit;
    }

    void present(const Image& fb) override {
        if (!ok_ || fb.empty()) return;
        if (fb.w != g.w || fb.h != g.h) {
            g.w = fb.w;
            g.h = fb.h;
            g.pixels.resize((size_t)g.w * g.h);
            g.bmi.bmiHeader.biWidth = g.w;
            g.bmi.bmiHeader.biHeight = -g.h;
        }
        // DIB (BI_RGB, 32bpp) expects bytes B,G,R,A — our Image stores R,G,B,A.
        for (size_t i = 0; i < (size_t)g.w * g.h; ++i) {
            uint32_t p = fb.px[i];
            uint8_t r = (uint8_t)(p & 0xFF), gg = (uint8_t)((p >> 8) & 0xFF), b = (uint8_t)((p >> 16) & 0xFF);
            g.pixels[i] = ((uint32_t)b << 16) | ((uint32_t)gg << 8) | r;
        }
        HDC dc = GetDC(g.hwnd);
        StretchDIBits(dc, 0, 0, g.w, g.h, 0, 0, g.w, g.h, g.pixels.data(), &g.bmi,
                      DIB_RGB_COLORS, SRCCOPY);
        ReleaseDC(g.hwnd, dc);
    }

    double timeSeconds() const override {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - g.start).count();
    }

private:
    bool ok_ = false;
};

std::unique_ptr<PlatformWindow> PlatformWindow::create(int w, int h, const char* title) {
    auto win = std::make_unique<Win32Window>(w, h, title);
    if (!win->ok()) return nullptr;
    return win;
}

} // namespace leptid

#endif // _WIN32
