// Leptid — headless fallback when no window backend is compiled.
// The static single-file build (make static) has no X11/Win32; GUI mode then
// prints a clear message while --demo / --headless / --ui-shot keep working.
#if !defined(LEPTID_HAS_X11) && !defined(_WIN32)

#include "platform/platform.h"

namespace leptid {
std::unique_ptr<PlatformWindow> PlatformWindow::create(int w, int h, const char* title) {
    (void)w; (void)h; (void)title;
    return nullptr;
}
}

#endif
