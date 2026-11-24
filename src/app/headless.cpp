// Leptid — headless operations: load a project, export renders, no window.
#include "export/exporter.h"
#include "model/lepser.h"
#include <cstdio>
#include <sys/stat.h>

namespace leptid {

static void ensureDir(const std::string& dir) {
    if (dir.empty()) return;
    std::string path;
    for (size_t i = 0; i <= dir.size(); ++i) {
        if (i == dir.size() || dir[i] == '/') {
            path = dir.substr(0, i);
            if (!path.empty()) {
                struct stat st;
                if (stat(path.c_str(), &st) != 0) mkdir(path.c_str(), 0755);
            }
        }
    }
}

int runHeadless(const std::string& project, const std::string& pngDir,
                const std::string& gif, const std::string& mp4) {
    if (project.empty()) {
        fprintf(stderr, "headless: no project given (--export FILE.lep)\n");
        return 2;
    }
    Project p;
    std::string err;
    if (!loadLep(project, p, &err)) {
        fprintf(stderr, "headless: load failed: %s\n", err.c_str());
        return 1;
    }
    printf("loaded %s: %d frames @ %d fps, %zu layers, %zu characters\n",
           project.c_str(), p.frameCount, p.fps, p.layers.size(), p.characters.size());
    ExportOptions opt;
    ensureDir(pngDir);
    if (!exportAll(p, pngDir, gif, mp4, opt, &err)) {
        fprintf(stderr, "headless: export failed: %s\n", err.c_str());
        return 1;
    }
    if (!pngDir.empty()) printf("png sequence  -> %s/frame_0000.png ..\n", pngDir.c_str());
    if (!gif.empty()) printf("gif           -> %s\n", gif.c_str());
    if (!mp4.empty()) printf("mp4           -> %s\n", mp4.c_str());
    return 0;
}

} // namespace leptid
