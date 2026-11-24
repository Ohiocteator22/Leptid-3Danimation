// Leptid — application entry point.
// Leptid is a free/open-source 2D animation application.
//   leptid                — launch the GUI editor (dark mode)
//   leptid FILE.lep       — open a project
//   leptid --demo F.lep [--gif F.gif] [--mp4 F.mp4] [--out DIR]   — demo project + renders
//   leptid --headless --export F.lep --out DIR [--gif F.gif] [--mp4 F.mp4]
//   leptid --ui-shot F.png — render one editor frame to a PNG (no window; CI)
#include <cstdio>
#include <string>

namespace leptid {
extern const char* kVersion;
extern bool runDemo(const std::string& lepPath, const std::string& gifPath,
                    const std::string& mp4Path, const std::string& pngDir);
extern int runHeadless(const std::string& project, const std::string& pngDir,
                       const std::string& gif, const std::string& mp4);
extern int runGui(int argc, char** argv, const std::string& project);
extern int runUiShot(const std::string& path, int width, int height, const std::string& project);
}
// version string defined in src/core/version.cpp

static void printUsage() {
    printf("Leptid %s — 2D animation application\n", leptid::kVersion);
    printf("usage:\n");
    printf("  leptid                          open the editor (dark mode)\n");
    printf("  leptid FILE.lep                 open a project in the editor\n");
    printf("  leptid --demo F.lep [--gif F.gif] [--mp4 F.mp4] [--out PNGDIR]\n");
    printf("  leptid --headless --export F.lep --out PNGDIR [--gif F.gif] [--mp4 F.mp4]\n");
    printf("  leptid --ui-shot F.png          render one editor frame headlessly\n");
}

int main(int argc, char** argv) {
    std::string mode = "gui";
    std::string project, out, gif, mp4, uishot;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--headless") mode = "headless";
        else if (a == "--demo") { mode = "demo"; if (i + 1 < argc) project = argv[++i]; }
        else if (a == "--export") { mode = "headless"; if (i + 1 < argc) project = argv[++i]; }
        else if (a == "--ui-shot") { mode = "uishot"; if (i + 1 < argc) uishot = argv[++i]; }
        else if (a == "--out" && i + 1 < argc) out = argv[++i];
        else if (a == "--gif" && i + 1 < argc) gif = argv[++i];
        else if (a == "--mp4" && i + 1 < argc) mp4 = argv[++i];
        else if (a == "-h" || a == "--help") { printUsage(); return 0; }
        else if (a.size() > 4 && a.rfind(".lep") == a.size() - 4) project = a;
        else { fprintf(stderr, "unknown arg: %s\n", a.c_str()); printUsage(); return 2; }
    }
    if (mode == "demo") return leptid::runDemo(project, gif, mp4, out) ? 0 : 3;
    if (mode == "headless") return leptid::runHeadless(project, out, gif, mp4);
    if (mode == "uishot") return leptid::runUiShot(uishot, 1280, 800, project);
    return leptid::runGui(argc, argv, project);
}
