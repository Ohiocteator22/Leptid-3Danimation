// Leptid — exporter implementation.
#include "export/exporter.h"
#include "export/gif.h"
#include "model/compositor.h"
#include "formats/pngio.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>

#if defined(_MSC_VER) // MSVC names the pipe-to-process functions _popen/_pclose
#define popen _popen
#define pclose _pclose
#endif

namespace {
void ensureDirRecursive(const std::string& dir) {
    if (dir.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec); // cross-platform (MSVC/MinGW/POSIX)
}
} // namespace

namespace leptid {

static void frameRange(const Project& p, const ExportOptions& opt, int& from, int& to) {
    from = std::max(0, opt.fromFrame);
    to = opt.toFrame < 0 ? p.frameCount - 1 : std::min(opt.toFrame, p.frameCount - 1);
}

static Image renderFlat(const Project& p, const ExportOptions& opt, int f) {
    RenderOptions ro;
    ro.frame = f;
    ro.outW = opt.width;
    ro.outH = opt.height;
    ro.background = Color(0, 0, 0, 0);
    return renderFrame(p, ro);
}

bool exportPngSequence(const Project& p, const std::string& outDir,
                       const ExportOptions& opt, std::string* error) {
    ensureDirRecursive(outDir);
    int from, to;
    frameRange(p, opt, from, to);
    for (int f = from; f <= to; ++f) {
        Image img = renderFlat(p, opt, f);
        char name[512];
        snprintf(name, sizeof name, "%s/frame_%04d.png", outDir.c_str(), f);
        if (!savePng(name, img)) {
            if (error) *error = std::string("cannot write ") + name;
            return false;
        }
    }
    return true;
}

bool exportGif(const Project& p, const std::string& path,
               const ExportOptions& opt, std::string* error) {
    int from, to;
    frameRange(p, opt, from, to);
    GifEncoder enc;
    for (int f = from; f <= to; ++f) {
        Image img = renderFlat(p, opt, f);
        // GIF has no alpha: flatten onto black
        for (auto& px : img.px) {
            uint8_t a = (uint8_t)(px >> 24);
            if (a < 255) {
                uint8_t r = (uint8_t)(px & 0xFF), g = (uint8_t)((px >> 8) & 0xFF), b = (uint8_t)((px >> 16) & 0xFF);
                px = ((uint32_t)(r * a / 255)) | ((uint32_t)(g * a / 255) << 8) | ((uint32_t)(b * a / 255) << 16) | 0xFF000000u;
            }
        }
        enc.addFrame(img);
    }
    int delayCs = std::max(2, (int)std::lround(100.0 / std::max(1, p.fps)));
    return enc.write(path, delayCs, error);
}

bool ffmpegAvailable() {
    FILE* f = popen("ffmpeg -version 2>/dev/null", "r");
    if (!f) return false;
    char buf[64] = {0};
    bool ok = fgets(buf, sizeof buf, f) != nullptr;
    int rc = pclose(f);
    return ok && rc == 0;
}

bool exportMp4(const Project& p, const std::string& path,
               const ExportOptions& opt, std::string* error) {
    if (!ffmpegAvailable()) {
        if (error) *error = "ffmpeg not found on PATH; MP4 export unavailable";
        return false;
    }
    int from, to;
    frameRange(p, opt, from, to);
    char cmd[1024];
    snprintf(cmd, sizeof cmd,
             "ffmpeg -y -loglevel error -f rawvideo -pix_fmt rgb24 -s %dx%d -r %d -i - "
             "-c:v libx264 -pix_fmt yuv420p -movflags +faststart \"%s\"",
             opt.width, opt.height, std::max(1, p.fps), path.c_str());
    FILE* pipe = popen(cmd, "w");
    if (!pipe) { if (error) *error = "cannot spawn ffmpeg"; return false; }
    std::vector<uint8_t> rgb((size_t)opt.width * opt.height * 3);
    for (int f = from; f <= to; ++f) {
        Image img = renderFlat(p, opt, f);
        for (size_t i = 0; i < (size_t)opt.width * opt.height; ++i) {
            uint32_t px = img.px[i];
            uint8_t a = (uint8_t)(px >> 24);
            uint8_t r = (uint8_t)(px & 0xFF), g = (uint8_t)((px >> 8) & 0xFF), b = (uint8_t)((px >> 16) & 0xFF);
            if (a < 255) { r = (uint8_t)(r * a / 255); g = (uint8_t)(g * a / 255); b = (uint8_t)(b * a / 255); }
            rgb[i * 3] = r; rgb[i * 3 + 1] = g; rgb[i * 3 + 2] = b;
        }
        fwrite(rgb.data(), 1, rgb.size(), pipe);
    }
    int rc = pclose(pipe);
    if (rc != 0) { if (error) *error = "ffmpeg failed (exit " + std::to_string(rc) + ")"; return false; }
    return true;
}

bool exportAll(const Project& p, const std::string& pngDir, const std::string& gifPath,
               const std::string& mp4Path, const ExportOptions& opt, std::string* error) {
    if (!pngDir.empty() && !exportPngSequence(p, pngDir, opt, error)) return false;
    if (!gifPath.empty() && !exportGif(p, gifPath, opt, error)) return false;
    if (!mp4Path.empty() && !exportMp4(p, mp4Path, opt, error)) return false;
    return true;
}

} // namespace leptid
