// Leptid — Image implementation.
#include "core/image.h"

namespace leptid {

void Image::create(int W, int H) {
    w = W; h = H;
    px.assign((size_t)std::max(0, W) * std::max(0, H), 0);
}

void Image::clear(Color c) {
    uint32_t p = c.pack();
    std::fill(px.begin(), px.end(), p);
}

Color Image::sampleBilinear(float fx, float fy) const {
    if (w <= 0 || h <= 0) return Color(0, 0, 0, 0);
    fx = clampf(fx, 0, w - 1);
    fy = clampf(fy, 0, h - 1);
    int x0 = (int)fx, y0 = (int)fy;
    int x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
    float tx = fx - x0, ty = fy - y0;
    Color c00 = Color::unpack(at(x0, y0)), c10 = Color::unpack(at(x1, y0));
    Color c01 = Color::unpack(at(x0, y1)), c11 = Color::unpack(at(x1, y1));
    // Alpha-weighted RGB interpolation so transparent texels don't darken edges.
    float a0 = lerpf(c00.a, c10.a, tx) / 255.0f;
    float a1 = lerpf(c01.a, c11.a, tx) / 255.0f;
    float a = lerpf(a0, a1, ty);
    if (a < 1e-5f) return Color(0, 0, 0, 0);
    auto ch = [&](float cA, float aA, float cB, float aB, float t) {
        float aa = lerpf(aA, aB, t);
        if (aa < 1e-5f) return 0.0f;
        return lerpf(cA * aA, cB * aB, t) / aa;
    };
    float rTop = ch(c00.r, c00.a, c10.r, c10.a, tx);
    float rBot = ch(c01.r, c01.a, c11.r, c11.a, tx);
    float r = lerpf(rTop, rBot, ty);
    float gTop = ch(c00.g, c00.a, c10.g, c10.a, tx);
    float gBot = ch(c01.g, c01.a, c11.g, c11.a, tx);
    float g = lerpf(gTop, gBot, ty);
    float bTop = ch(c00.b, c00.a, c10.b, c10.b, tx);
    float bBot = ch(c01.b, c01.a, c11.b, c11.a, tx);
    float b = lerpf(bTop, bBot, ty);
    return Color::fromFloats(r / 255.0f, g / 255.0f, b / 255.0f, a);
}

void Image::blit(const Image& src, int dx, int dy, float opacity) {
    if (src.empty() || empty() || opacity <= 0) return;
    int x0 = std::max(0, dx), y0 = std::max(0, dy);
    int x1 = std::min(w, dx + src.w), y1 = std::min(h, dy + src.h);
    float oa = clampf(opacity, 0, 1);
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            uint32_t sp = src.at(x - dx, y - dy);
            if ((sp >> 24) == 0) continue; // fully transparent: skip
            Color s = Color::unpack(sp);
            float sa = (s.a / 255.0f) * oa;
            if (sa >= 1.0f) { at(x, y) = s.pack(); continue; }
            Color d = Color::unpack(at(x, y));
            float da = d.a / 255.0f;
            float outA = sa + da * (1 - sa);
            if (outA < 1e-5f) { at(x, y) = 0; continue; }
            float outR = (s.r * sa + d.r * da * (1 - sa)) / outA;
            float outG = (s.g * sa + d.g * da * (1 - sa)) / outA;
            float outB = (s.b * sa + d.b * da * (1 - sa)) / outA;
            at(x, y) = Color::fromFloats(outR / 255.0f, outG / 255.0f, outB / 255.0f, outA).pack();
        }
    }
}

void Image::blitTransformed(const Image& src, const Mat3& srcToDst, float opacity) {
    if (src.empty() || empty() || opacity <= 0) return;
    // dst->src mapping for inverse lookup
    Mat3 dstToSrc = srcToDst.inverse();
    Vec2 corners[4] = {
        srcToDst.apply(0, 0), srcToDst.apply((float)src.w, 0),
        srcToDst.apply(0, (float)src.h), srcToDst.apply((float)src.w, (float)src.h)};
    float minX = w, minY = h, maxX = 0, maxY = 0;
    for (auto& c : corners) {
        minX = std::min(minX, c.x); minY = std::min(minY, c.y);
        maxX = std::max(maxX, c.x); maxY = std::max(maxY, c.y);
    }
    int x0 = std::max(0, (int)std::floor(minX)), y0 = std::max(0, (int)std::floor(minY));
    int x1 = std::min(w, (int)std::ceil(maxX) + 1), y1 = std::min(h, (int)std::ceil(maxY) + 1);
    float oa = clampf(opacity, 0, 1);
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            Vec2 s = dstToSrc.apply((float)x + 0.5f, (float)y + 0.5f);
            // Center-coordinate bounds: sampleBilinear clamps at the edges, so
            // anything outside [0,w)x[0,h) must be skipped to avoid smearing.
            if (s.x < 0.0f || s.y < 0.0f || s.x >= (float)src.w || s.y >= (float)src.h) continue;
            Color sc = src.sampleBilinear(s.x - 0.5f, s.y - 0.5f);
            if (sc.a == 0) continue;
            float sa = (sc.a / 255.0f) * oa;
            Color d = Color::unpack(at(x, y));
            float da = d.a / 255.0f;
            float outA = sa + da * (1 - sa);
            if (outA < 1e-5f) { at(x, y) = 0; continue; }
            float outR = (sc.r * sa + d.r * da * (1 - sa)) / outA;
            float outG = (sc.g * sa + d.g * da * (1 - sa)) / outA;
            float outB = (sc.b * sa + d.b * da * (1 - sa)) / outA;
            at(x, y) = Color::fromFloats(outR / 255.0f, outG / 255.0f, outB / 255.0f, outA).pack();
        }
    }
}

void Image::fillRect(int x, int y, int rw, int rh, Color c) {
    int x0 = std::max(0, x), y0 = std::max(0, y);
    int x1 = std::min(w, x + rw), y1 = std::min(h, y + rh);
    for (int yy = y0; yy < y1; ++yy)
        for (int xx = x0; xx < x1; ++xx) at(xx, yy) = c.pack();
}

void Image::strokeRect(int x, int y, int rw, int rh, Color c, int thickness) {
    fillRect(x, y, rw, thickness, c);
    fillRect(x, y + rh - thickness, rw, thickness, c);
    fillRect(x, y, thickness, rh, c);
    fillRect(x + rw - thickness, y, thickness, rh, c);
}

void Image::drawLine(float x0, float y0, float x1, float y1, Color c, float thickness) {
    float dx = x1 - x0, dy = y1 - y0;
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-6f) { fillDisc(x0, y0, thickness * 0.5f, c); return; }
    int steps = (int)std::ceil(len * 2) + 1;
    float r = thickness * 0.5f;
    for (int i = 0; i <= steps; ++i) {
        float t = (float)i / steps;
        fillDisc(x0 + dx * t, y0 + dy * t, r, c);
    }
}

void Image::fillDisc(float cx, float cy, float radius, Color c) {
    if (radius <= 0 || c.a == 0) return;
    // Half-pixel expansion: a 1px line/disc centered on an integer coordinate
    // straddles a pixel boundary — pixel centers sit exactly at radius. Without
    // the expansion such primitives rasterize to nothing (keyframe diamonds,
    // ruler ticks...). The ramp gives a clean anti-aliased edge.
    float r = radius + 0.5f;
    int x0 = std::max(0, (int)std::floor(cx - r)), x1 = std::min(w - 1, (int)std::ceil(cx + r));
    int y0 = std::max(0, (int)std::floor(cy - r)), y1 = std::min(h - 1, (int)std::ceil(cy + r));
    float oa = c.a / 255.0f;
    float inner = r - 1.0f;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            float ddx = (x + 0.5f) - cx, ddy = (y + 0.5f) - cy;
            float dist = std::sqrt(ddx * ddx + ddy * ddy);
            float cov = 1.0f;
            if (dist >= r) continue;
            if (dist > inner && inner >= 0) cov = clampf(r - dist, 0, 1);
            float sa = oa * cov;
            Color d = Color::unpack(at(x, y));
            float da = d.a / 255.0f;
            float outA = sa + da * (1 - sa);
            if (outA < 1e-5f) { at(x, y) = 0; continue; }
            float oR = (c.r * sa + d.r * da * (1 - sa)) / outA;
            float oG = (c.g * sa + d.g * da * (1 - sa)) / outA;
            float oB = (c.b * sa + d.b * da * (1 - sa)) / outA;
            at(x, y) = Color::fromFloats(oR / 255.0f, oG / 255.0f, oB / 255.0f, outA).pack();
        }
    }
}

void Image::fillCircle(float cx, float cy, float radius, Color c) {
    if (radius <= 0 || c.a == 0) return;
    int x0 = std::max(0, (int)std::floor(cx - radius)), x1 = std::min(w - 1, (int)std::ceil(cx + radius));
    int y0 = std::max(0, (int)std::floor(cy - radius)), y1 = std::min(h - 1, (int)std::ceil(cy + radius));
    float r2 = radius * radius;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            float ddx = (x + 0.5f) - cx, ddy = (y + 0.5f) - cy;
            if (ddx * ddx + ddy * ddy <= r2) at(x, y) = c.pack();
        }
}

Image Image::resized(int W, int H) const {
    Image out;
    if (W <= 0 || H <= 0 || empty()) return out;
    out.create(W, H);
    // Box-filtered downscale; nearest upscale (fast, good enough for preview; camera renders via bilinear blit).
    float sx = (float)w / W, sy = (float)h / H;
    for (int y = 0; y < H; ++y) {
        float fy0 = y * sy, fy1 = (y + 1) * sy;
        for (int x = 0; x < W; ++x) {
            float fx0 = x * sx, fx1 = (x + 1) * sx;
            float sumR = 0, sumG = 0, sumB = 0, sumA = 0, cnt = 0;
            for (int yy = (int)fy0; yy < std::min(h, (int)std::ceil(fy1)); ++yy)
                for (int xx = (int)fx0; xx < std::min(w, (int)std::ceil(fx1)); ++xx) {
                    Color c = Color::unpack(at(xx, yy));
                    float a = c.a / 255.0f;
                    sumR += c.r * a; sumG += c.g * a; sumB += c.b * a; sumA += a;
                    cnt += 1;
                }
            if (sumA < 1e-5f) { out.at(x, y) = 0; continue; }
            float a = sumA / cnt;
            out.at(x, y) = Color::fromFloats(sumR / sumA / 255.0f, sumG / sumA / 255.0f,
                                              sumB / sumA / 255.0f, a).pack();
        }
    }
    return out;
}

Image Image::crop(int x, int y, int W, int H) const {
    Image out;
    if (W <= 0 || H <= 0) return out;
    out.create(W, H);
    for (int yy = 0; yy < H; ++yy)
        for (int xx = 0; xx < W; ++xx) {
            int sx = x + xx, sy = y + yy;
            out.at(xx, yy) = inBounds(sx, sy) ? at(sx, sy) : 0;
        }
    return out;
}

size_t Image::opaquePixelCount() const {
    size_t n = 0;
    for (uint32_t p : px) if ((p >> 24) > 8) ++n;
    return n;
}

} // namespace leptid
