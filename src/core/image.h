// Leptid — RGBA8 raster image with software compositing primitives.
// Straight (non-premultiplied) alpha; "source-over" blending everywhere.
#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include "core/math.h"

namespace leptid {

struct Color {
    uint8_t r = 0, g = 0, b = 0, a = 0;
    Color() {}
    Color(uint8_t R, uint8_t G, uint8_t B, uint8_t A = 255) : r(R), g(G), b(B), a(A) {}
    static Color fromFloats(float R, float G, float B, float A) {
        return Color((uint8_t)clampf(R * 255, 0, 255), (uint8_t)clampf(G * 255, 0, 255),
                     (uint8_t)clampf(B * 255, 0, 255), (uint8_t)clampf(A * 255, 0, 255));
    }
    bool operator==(const Color& o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
    bool operator!=(const Color& o) const { return !(*this == o); }
    uint32_t pack() const { return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | r; }
    static Color unpack(uint32_t p) {
        return Color((uint8_t)(p & 0xFF), (uint8_t)((p >> 8) & 0xFF),
                     (uint8_t)((p >> 16) & 0xFF), (uint8_t)((p >> 24) & 0xFF));
    }
};

struct Image {
    int w = 0, h = 0;
    std::vector<uint32_t> px; // 0xRRGGBBAA, row-major

    Image() {}
    Image(int W, int H) { create(W, H); }
    void create(int W, int H);
    bool empty() const { return w <= 0 || h <= 0; }
    void clear(Color c);
    uint32_t at(int x, int y) const { return px[(size_t)y * w + x]; }
    uint32_t& at(int x, int y) { return px[(size_t)y * w + x]; }
    bool inBounds(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h; }

    // Sampling with edge-clamped coordinates; bilinear, alpha-weighted.
    Color sampleBilinear(float fx, float fy) const;

    // Source-over composite of `src` onto this image at offset (dx,dy), clipped.
    // `opacity` multiplies src alpha (0..1).
    void blit(const Image& src, int dx, int dy, float opacity = 1.0f);

    // UNCONDITIONAL pixel copy (no alpha compositing): overwrites dst pixels,
    // including fully transparent ones. Required for stroke commit/undo — an
    // erase must be able to write alpha=0 back over painted pixels, which
    // source-over blit can never do (it skips transparent src pixels).
    void blitReplace(const Image& src, int dx, int dy);

    // Source-over composite with transform (maps src pixel coords -> dst coords),
    // inverse-mapped with bilinear sampling. Used for limb sprites, group transforms, camera rendering.
    void blitTransformed(const Image& src, const Mat3& srcToDst, float opacity = 1.0f);

    void fillRect(int x, int y, int w, int h, Color c);
    void strokeRect(int x, int y, int w, int h, Color c, int thickness = 1);
    void drawLine(float x0, float y0, float x1, float y1, Color c, float thickness = 1.0f);
    void fillDisc(float cx, float cy, float radius, Color c); // soft falloff near edge
    void fillCircle(float cx, float cy, float radius, Color c); // hard edge

    // Nearest-neighbor and box-filtered rescale.
    Image resized(int W, int H) const;
    Image crop(int x, int y, int W, int H) const;

    // Blend a whole layer onto this with opacity (same as blit at 0,0 but explicit).
    void compositeOver(const Image& src, float opacity = 1.0f) { blit(src, 0, 0, opacity); }

    // Count of non-transparent pixels (blank-check for tests/verification).
    size_t opaquePixelCount() const;
    bool isBlank() const { return opaquePixelCount() == 0; }

    bool operator==(const Image& o) const { return w == o.w && h == o.h && px == o.px; }
};

} // namespace leptid
