// Leptid — shared compositing pipeline. One renderer for canvas preview,
// playback and export (blueprint rule: reuse the same pipeline everywhere).
#pragma once
#include "model/project.h"

namespace leptid {

struct RenderOptions {
    int frame = 0;
    int outW = 800, outH = 600;
    const ViewTransform* view = nullptr;   // editor view; null = render through project camera
    Color background = Color(0, 0, 0, 0);  // export = transparent; preview sets dark gray
    // Onion skin (canvas preview only; applies to the active raster layer):
    bool onion = false;
    int onionBefore = 1, onionAfter = 1;
    float onionOpacity = 0.3f;
    bool onionShowMotionPath = false;
    // Rig overlays (preview only):
    bool showBones = false;
    bool showCameraRect = false;
};

// Render one frame through the project camera (or an editor view transform).
Image renderFrame(const Project& p, const RenderOptions& opt);

// Tinted copy for onion skinning (colorizes opaque pixels, keeps alpha shape).
Image tintImage(const Image& src, Color tint, float opacity);

} // namespace leptid
