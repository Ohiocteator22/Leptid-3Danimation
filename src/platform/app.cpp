// Leptid — GUI application runner: platform loop + headless UI capture.
#include "platform/platform.h"
#include "ui/editor.h"
#include "formats/pngio.h"
#include <chrono>
#include <thread>
#include <cstdio>

namespace leptid {

int runGui(int argc, char** argv, const std::string& project) {
    (void)argc;
    (void)argv;
    const int W = 1280, H = 800;
    auto win = PlatformWindow::create(W, H, "Leptid — 2D Animation");
    if (!win) {
        fprintf(stderr,
                "GUI: no display available (X11 not reachable and not on Windows).\n"
                "Run a desktop session, or use headless mode:\n"
                "  leptid --demo FILE.lep --gif FILE.gif --out PNGDIR\n"
                "  leptid --headless --export FILE.lep --out PNGDIR\n");
        return 4;
    }
    Editor editor;
    if (!project.empty()) editor.openProject(project);

    Ui ui;
    std::vector<InputEvent> events;
    auto last = std::chrono::steady_clock::now();
    bool running = true;
    float mouseX = -100, mouseY = -100;
    bool leftDown = false, rightDown = false;
    while (running) {
        events.clear();
        running = win->pollEvents(events);
        for (const InputEvent& e : events) {
            ui.onEvent(e);
            switch (e.type) {
                case InputEvent::Type::MouseMove:
                    mouseX = e.x; mouseY = e.y;
                    break;
                case InputEvent::Type::MouseDown:
                    if (e.button == 1) leftDown = true;
                    if (e.button == 3) rightDown = true;
                    break;
                case InputEvent::Type::MouseUp:
                    if (e.button == 1) leftDown = false;
                    if (e.button == 3) rightDown = false;
                    break;
                default: break;
            }
        }
        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - last).count();
        last = now;

        // one UI frame over the editor framebuffer
        ui.begin(&editor.framebuffer(), mouseX, mouseY, leftDown, rightDown);
        editor.update(ui, W, H, dt);
        win->present(editor.framebuffer());
        if (editor.wantsQuit()) running = false;
        std::this_thread::sleep_for(std::chrono::milliseconds(12)); // ~80 fps cap
    }
    return 0;
}

int runUiShot(const std::string& path, int width, int height, const std::string& project) {
    Editor editor;
    if (!project.empty()) editor.openProject(project);
    Ui ui;
    editor.renderOnce(ui, width, height);
    if (!savePng(path, editor.framebuffer())) {
        fprintf(stderr, "ui-shot: cannot write %s\n", path.c_str());
        return 1;
    }
    printf("ui screenshot -> %s\n", path.c_str());
    return 0;
}

} // namespace leptid
