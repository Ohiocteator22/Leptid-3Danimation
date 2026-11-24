// Leptid — brush engine implementation (stamp tips, spacing, flow).
#include "model/brush.h"
#include "formats/pngio.h"
#include <algorithm>
#include <map>
#include <cmath>

// embedded brush tip assets (generated into bin/assets.cpp by the build)
namespace leptid_embed {
extern const unsigned char tip_pencil_png[];
extern const unsigned tip_pencil_png_len;
extern const unsigned char tip_ink_png[];
extern const unsigned tip_ink_png_len;
extern const unsigned char tip_marker_png[];
extern const unsigned tip_marker_png_len;
extern const unsigned char tip_airbrush_png[];
extern const unsigned tip_airbrush_png_len;
extern const unsigned char tip_chalk_png[];
extern const unsigned tip_chalk_png_len;
extern const unsigned char tip_spray_png[];
extern const unsigned tip_spray_png_len;
extern const unsigned char tip_soft_round_png[];
extern const unsigned tip_soft_round_png_len;
extern const unsigned char tip_hard_round_png[];
extern const unsigned tip_hard_round_png_len;
extern const unsigned char tip_eraser_hard_png[];
extern const unsigned tip_eraser_hard_png_len;
extern const unsigned char tip_eraser_soft_png[];
extern const unsigned tip_eraser_soft_png_len;
}

namespace leptid {

const std::vector<BrushPreset>& BrushEngine::presets() {
    static const std::vector<BrushPreset> presets = {
        {BrushId::Pencil,     "Pencil",    leptid_embed::tip_pencil_png,     leptid_embed::tip_pencil_png_len,     0.08f, 1.00f, 1.0f, false},
        {BrushId::Ink,        "Ink",       leptid_embed::tip_ink_png,        leptid_embed::tip_ink_png_len,        0.06f, 1.00f, 1.0f, false},
        {BrushId::Marker,     "Marker",    leptid_embed::tip_marker_png,     leptid_embed::tip_marker_png_len,     0.04f, 0.45f, 1.0f, false},
        {BrushId::Airbrush,   "Airbrush",  leptid_embed::tip_airbrush_png,   leptid_embed::tip_airbrush_png_len,   0.03f, 0.18f, 1.0f, false},
        {BrushId::Chalk,      "Chalk",     leptid_embed::tip_chalk_png,      leptid_embed::tip_chalk_png_len,      0.12f, 0.85f, 1.0f, false},
        {BrushId::Spray,      "Spray",     leptid_embed::tip_spray_png,      leptid_embed::tip_spray_png_len,      0.25f, 0.80f, 1.0f, false},
        {BrushId::SoftRound,  "Soft",      leptid_embed::tip_soft_round_png, leptid_embed::tip_soft_round_png_len, 0.08f, 0.70f, 1.0f, false},
        {BrushId::HardRound,  "Hard",      leptid_embed::tip_hard_round_png, leptid_embed::tip_hard_round_png_len, 0.06f, 1.00f, 1.0f, false},
        {BrushId::EraserHard, "Eraser",    leptid_embed::tip_eraser_hard_png, leptid_embed::tip_eraser_hard_png_len, 0.06f, 1.0f, 1.0f, true},
        {BrushId::EraserSoft, "Soft eraser", leptid_embed::tip_eraser_soft_png, leptid_embed::tip_eraser_soft_png_len, 0.06f, 0.9f, 1.0f, true},
    };
    return presets;
}

const BrushPreset& BrushEngine::preset(BrushId id) {
    for (const BrushPreset& p : presets())
        if (p.id == id) return p;
    return presets().back();
}

const Image& BrushEngine::stampFor(const BrushPreset& p, int sizePx, Color color) const {
    struct Key { int preset; int size; uint32_t color; bool operator<(const Key& o) const {
        return preset != o.preset ? preset < o.preset : size != o.size ? size < o.size : color < o.color; } };
    static std::map<Key, Image> cache;
    static std::map<int, Image> tipCache; // decoded mask per preset
    auto tipIt = tipCache.find((int)p.id);
    if (tipIt == tipCache.end()) {
        Image raw;
        if (!loadPngMemory(p.tipPng, p.tipLen, raw)) {
            raw.create(16, 16);
            raw.fillCircle(8, 8, 7, Color(0, 0, 0, 255));
        }
        // Tips are grayscale masks: luminance (red channel) is the alpha shape.
        Image mask;
        mask.create(raw.w, raw.h);
        for (size_t i = 0; i < raw.px.size(); ++i) {
            uint8_t g = (uint8_t)(raw.px[i] & 0xFF);
            mask.px[i] = ((uint32_t)g << 24) | 0x00FFFFFFu;
        }
        tipCache[(int)p.id] = std::move(mask);
        tipIt = tipCache.find((int)p.id);
    }
    const Image& mask = tipIt->second;
    int sz = std::max(1, sizePx);
    Key key{(int)p.id, sz, color.pack()};
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    Image stamp = mask.resized(sz, sz);
    for (auto& px : stamp.px) {
        uint8_t a = (uint8_t)(px >> 24);
        px = ((uint32_t)(uint8_t)(a * p.flow) << 24) | ((uint32_t)color.b << 16) | ((uint32_t)color.g << 8) | color.r;
    }
    auto ins = cache.emplace(key, std::move(stamp));
    return ins.first->second;
}

void BrushEngine::stamp(Image& canvas, Vec2 pos, const BrushSettings& s) const {
    const BrushPreset& p = preset(s.id);
    bool erase = p.eraser || s.eraser;
    int sz = std::max(1, (int)std::lround(s.size * p.sizeFactor));
    const Image& stamp = stampFor(p, sz, erase ? Color(0, 0, 0, 255) : s.color);
    int dx = (int)std::lround(pos.x - stamp.w * 0.5f);
    int dy = (int)std::lround(pos.y - stamp.h * 0.5f);
    if (erase) {
        int x0 = std::max(0, dx), y0 = std::max(0, dy);
        int x1 = std::min(canvas.w, dx + stamp.w), y1 = std::min(canvas.h, dy + stamp.h);
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x) {
                uint8_t ma = (uint8_t)(stamp.at(x - dx, y - dy) >> 24);
                uint32_t px = canvas.at(x, y);
                uint8_t a = (uint8_t)(px >> 24);
                uint8_t na = (uint8_t)(a * (1.0f - ma / 255.0f));
                canvas.at(x, y) = (px & 0x00FFFFFFu) | ((uint32_t)na << 24);
            }
    } else {
        canvas.blit(stamp, dx, dy, s.opacity);
    }
}

void BrushEngine::paintStroke(Image& canvas, const std::vector<Vec2>& pts,
                              const BrushSettings& s) const {
    if (pts.empty()) return;
    const BrushPreset& p = preset(s.id);
    int sz = std::max(1, (int)std::lround(s.size * p.sizeFactor));
    float spacing = std::max(0.5f, p.spacing * sz);
    // single point / short dab
    if (pts.size() == 1) { stamp(canvas, pts[0], s); return; }
    for (size_t i = 1; i < pts.size(); ++i) {
        Vec2 d = pts[i] - pts[i - 1];
        float segLen = d.len();
        if (segLen < 1e-6f) continue;
        float t = 0;
        while (t < segLen) {
            float next = std::min(t + spacing, segLen);
            Vec2 pnt = pts[i - 1] + d * (next / segLen);
            stamp(canvas, pnt, s);
            t = next;
            if (next >= segLen) break;
        }
    }
    stamp(canvas, pts.back(), s); // always end with a dab
}

Image BrushEngine::previewIcon(BrushId id, int px, Color tint) const {
    const BrushPreset& p = preset(id);
    Image mask;
    if (!loadPngMemory(p.tipPng, p.tipLen, mask)) {
        mask.create(px, px);
        mask.fillCircle(px / 2, px / 2, px * 0.4f, Color(0, 0, 0, 255));
        return mask;
    }
    // grayscale mask -> tinted alpha icon (luminance = alpha)
    Image icon = mask.resized(px, px);
    for (auto& v : icon.px) {
        uint8_t lum = (uint8_t)(v & 0xFF);
        v = ((uint32_t)lum << 24) | ((uint32_t)tint.b << 16) | ((uint32_t)tint.g << 8) | tint.r;
    }
    return icon;
}

Image BrushEngine::previewStroke(BrushId id, int w2, int h2, Color tint) const {
    const BrushPreset& p = preset(id);
    Image out;
    out.create(w2, h2);
    out.clear(Color(0, 0, 0, 0));
    if (w2 < 8 || h2 < 8) return out;
    BrushSettings s;
    s.id = id;
    s.color = tint;
    s.opacity = 1.0f;
    s.size = h2 * 0.42f;
    // S-curve across the tile
    std::vector<Vec2> pts;
    int n = std::max(12, w2);
    for (int i = 0; i <= n; ++i) {
        float t = (float)i / n;
        float x = w2 * 0.12f + t * w2 * 0.76f;
        float y = h2 * 0.62f - std::sin(t * 3.14159f) * h2 * 0.30f;
        pts.push_back(Vec2(x, y));
    }
    paintStroke(out, pts, s);
    return out;
}

// ---------------- stabilization (unchanged) ----------------
std::vector<Vec2> smoothStroke(const std::vector<Vec2>& raw, int strength) {
    if (raw.size() < 3 || strength <= 0) return raw;
    std::vector<Vec2> smooth;
    smooth.reserve(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        int lo = std::max(0, (int)i - strength);
        int hi = std::min((int)raw.size() - 1, (int)i + strength);
        Vec2 acc;
        for (int j = lo; j <= hi; ++j) acc += raw[(size_t)j];
        smooth.push_back(acc / (float)(hi - lo + 1));
    }
    std::vector<Vec2> out;
    if (smooth.size() < 3) return smooth;
    for (size_t i = 0; i + 1 < smooth.size(); ++i) {
        Vec2 p0 = smooth[i == 0 ? 0 : i - 1];
        Vec2 p1 = smooth[i];
        Vec2 p2 = smooth[i + 1];
        Vec2 p3 = smooth[i + 2 <= smooth.size() - 1 ? i + 2 : smooth.size() - 1];
        float seg = Vec2::dist(p1, p2);
        int steps = std::max(1, (int)(seg / 1.5f));
        for (int s = 0; s < steps; ++s) {
            float t = (float)s / steps;
            float t2 = t * t, t3 = t2 * t;
            Vec2 p = 0.5f * ((2.0f * p1) + (-p0 + p2) * t +
                             (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
                             (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
            out.push_back(p);
        }
    }
    out.push_back(smooth.back());
    return out;
}

IRect strokeBounds(const std::vector<Vec2>& pts, const BrushSettings& s, int canvasW, int canvasH) {
    IRect r{canvasW, canvasH, 0, 0};
    float pad = BrushEngine::padFor(s);
    for (const Vec2& p : pts) {
        r.x0 = std::min(r.x0, (int)std::floor(p.x - pad));
        r.y0 = std::min(r.y0, (int)std::floor(p.y - pad));
        r.x1 = std::max(r.x1, (int)std::ceil(p.x + pad));
        r.y1 = std::max(r.y1, (int)std::ceil(p.y + pad));
    }
    r.x0 = std::max(0, r.x0); r.y0 = std::max(0, r.y0);
    r.x1 = std::min(canvasW - 1, r.x1); r.y1 = std::min(canvasH - 1, r.y1);
    if (r.x1 < r.x0 || r.y1 < r.y0) { r.x0 = r.y0 = r.x1 = r.y1 = 0; }
    return r;
}

} // namespace leptid
