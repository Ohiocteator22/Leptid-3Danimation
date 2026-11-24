// Leptid — character cutting / rig preparation (blueprint §4).
// Takes T-pose artwork plus user cut rectangles, extracts limb parts while
// preserving source pixel coordinates and transparency, and builds a rigged
// character with pivots and sprite bindings.
#pragma once
#include <string>
#include <vector>
#include "core/image.h"
#include "rig/character.h"

namespace leptid {

struct CutSpec {
    std::string slot;  // humanoid slot name: head, torso, armL_upper, armL_lower,
                       // armR_upper, armR_lower, legL_upper, legL_lower,
                       // legR_upper, legR_lower, or single-segment armL/armR/legL/legR
    int x = 0, y = 0, w = 0, h = 0;  // rectangle in source artwork pixels
    Vec2 pivot{-1, -1};              // bone anchor in SOURCE pixels; {-1,-1} = auto
};

class CharacterCutter {
public:
    void setSource(const Image& src) { src_ = src; }
    void addCut(const CutSpec& c) { cuts_.push_back(c); }
    void clearCuts() { cuts_.clear(); }
    // Key-color transparency: pixels matching `key` within `tolerance` (0..255
    // per channel distance) become transparent in every part.
    void setKeyColor(Color key, int tolerance) { key_ = key; tol_ = tolerance; useKey_ = true; }
    void clearKeyColor() { useKey_ = false; }

    // Builds the character. Returns false with a message on invalid input.
    bool build(Character& out, std::string* error = nullptr) const;

private:
    Image src_;
    std::vector<CutSpec> cuts_;
    Color key_{0, 0, 0, 255};
    int tol_ = 40;
    bool useKey_ = false;

    Image extract(const CutSpec& c, Vec2& pivotInPart) const;
    static bool isSlotDown(const std::string& slot);     // pivot at top, bone points down
    static int slotParent(const std::vector<std::string>& slots, const std::string& slot, int torsoIdx);
};

} // namespace leptid
