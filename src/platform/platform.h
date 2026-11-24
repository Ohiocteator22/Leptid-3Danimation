// Leptid — platform window abstraction (X11 on Linux, Win32 on Windows).
#pragma once
#include <memory>
#include <vector>
#include "core/image.h"
#include "ui/ui.h"

namespace leptid {

class PlatformWindow {
public:
    // Returns null when no display is available (headless environments).
    static std::unique_ptr<PlatformWindow> create(int w, int h, const char* title);
    virtual ~PlatformWindow() {}
    virtual bool pollEvents(std::vector<InputEvent>& out) = 0; // false on quit
    virtual void present(const Image& fb) = 0;
    virtual double timeSeconds() const = 0;
};

// Render one editor frame to a PNG without any window (--ui-shot).
int runUiShot(const std::string& path, int width, int height, const std::string& project = "");

} // namespace leptid
