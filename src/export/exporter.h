// Leptid — render/export pipeline: PNG sequence, GIF, MP4 (via ffmpeg if present).
// One shared frame-rendering pipeline feeds all formats (blueprint §7-A).
#pragma once
#include <string>
#include "model/project.h"

namespace leptid {

struct ExportOptions {
    int fromFrame = 0;
    int toFrame = -1;          // -1 = last project frame
    int width = 800, height = 600;
    bool transparentPng = true; // PNG sequence keeps alpha; GIF/MP4 flatten to black
};

// Renders every frame through the project camera and writes frame_%04d.png.
bool exportPngSequence(const Project& p, const std::string& outDir,
                       const ExportOptions& opt, std::string* error = nullptr);

// Animated GIF (own encoder, always available).
bool exportGif(const Project& p, const std::string& path,
               const ExportOptions& opt, std::string* error = nullptr);

// MP4 via ffmpeg (rawvideo pipe); fails gracefully if ffmpeg is missing.
bool ffmpegAvailable();
bool exportMp4(const Project& p, const std::string& path,
               const ExportOptions& opt, std::string* error = nullptr);

// Export everything requested in one call; missing parts skipped when paths empty.
bool exportAll(const Project& p, const std::string& pngDir, const std::string& gifPath,
               const std::string& mp4Path, const ExportOptions& opt, std::string* error = nullptr);

} // namespace leptid
