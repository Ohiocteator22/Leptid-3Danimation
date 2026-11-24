// Leptid — stamp-based brush engine with presets (Krita-inspired).
// Each preset is a nib/tip mask asset (embedded PNG) plus stroke dynamics:
// spacing (distance between stamps as a fraction of tip size), flow
// (per-stamp opacity) and size scaling. Tips live in assets/brushes/ and are
// compiled into the binary so the app stays a single self-contained file.
#pragma once
#include <vector>
#include "core/image.h"

namespace leptid {

enum class BrushId {
    Pencil,     // tilted chisel nib, hard edges
    Ink,        // hard round, crisp lines
    Marker,     // flat square nib, semi-transparent
    Airbrush,   // very soft round, low flow
    Chalk,      // grainy textured nib
    Spray,      // scattered dots
    SoftRound,  // classic soft brush
    HardRound,  // classic hard brush
    EraserHard,
    EraserSoft,
};

struct BrushPreset {
    BrushId id;
    const char* name;
    const unsigned char* tipPng;
    unsigned tipLen;
    float spacing;     // stamp distance / tip size
    float flow;        // per-stamp opacity multiplier
    float sizeFactor;  // tip native size -> slider size mapping
    bool eraser;
};

struct BrushSettings {
    BrushId id = BrushId::HardRound;
    float size = 8.0f;         // stroke diameter in canvas px
    Color color = Color(240, 240, 240, 255);
    float opacity = 1.0f;      // master opacity over the whole stroke
    bool eraser = false;       // convenience override for the eraser tool
};

class BrushEngine {
public:
    static const std::vector<BrushPreset>& presets();
    static const BrushPreset& preset(BrushId id);

    // Paint a polyline with the preset (stamps spaced along the path).
    void paintStroke(Image& canvas, const std::vector<Vec2>& pts,
                     const BrushSettings& s) const;

    // One stamp (also used for single-click dabs and previews).
    void stamp(Image& canvas, Vec2 pos, const BrushSettings& s) const;

    // Small tinted preview of a preset's nib for the picker UI.
    Image previewIcon(BrushId id, int px, Color tint) const;

    // Krita-style picker icon: a short S-curve stroke sample rendered with the
    // preset (shows texture, flow and spacing at a glance).
    Image previewStroke(BrushId id, int w, int h, Color tint) const;

    // Bounding pad (size) helper for undo snapshots.
    static float padFor(const BrushSettings& s) { return s.size * 0.5f + 2.0f; }

private:
    // cached, tinted, scaled stamp for (preset, size bucket, color)
    const Image& stampFor(const BrushPreset& p, int sizePx, Color color) const;
};

// Stroke stabilization (unchanged from v0.1): moving average + Catmull-Rom.
std::vector<Vec2> smoothStroke(const std::vector<Vec2>& raw, int strength);

// Rect of interest for a stroke (undo snapshot bounds), clamped to canvas.
struct IRect { int x0, y0, x1, y1; };
IRect strokeBounds(const std::vector<Vec2>& pts, const BrushSettings& s, int canvasW, int canvasH);

} // namespace leptid
