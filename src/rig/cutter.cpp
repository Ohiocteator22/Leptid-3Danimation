// Leptid — cutter implementation.
#include "rig/cutter.h"
#include <algorithm>

namespace leptid {

Image CharacterCutter::extract(const CutSpec& c, Vec2& pivotInPart) const {
    Image part = src_.crop(c.x, c.y, c.w, c.h);
    if (useKey_) {
        for (auto& p : part.px) {
            int r = (int)(p & 0xFF), g = (int)((p >> 8) & 0xFF), b = (int)((p >> 16) & 0xFF);
            if (std::abs(r - key_.r) <= tol_ && std::abs(g - key_.g) <= tol_ &&
                std::abs(b - key_.b) <= tol_)
                p = 0;
        }
    }
    // pivot: user-set (source px) or auto by slot: limbs top-center, head/torso bottom-center
    Vec2 pivotSrc = c.pivot;
    if (pivotSrc.x < 0) {
        if (isSlotDown(c.slot)) pivotSrc = Vec2((float)c.x + c.w * 0.5f, (float)c.y + 2.0f);
        else pivotSrc = Vec2((float)c.x + c.w * 0.5f, (float)c.y + c.h - 2.0f);
    }
    pivotInPart = pivotSrc - Vec2((float)c.x, (float)c.y);
    pivotInPart.x = clampf(pivotInPart.x, 0, (float)c.w - 1);
    pivotInPart.y = clampf(pivotInPart.y, 0, (float)c.h - 1);
    return part;
}

bool CharacterCutter::isSlotDown(const std::string& slot) {
    // Limbs have their pivot at the shoulder/hip (top); bones point down.
    return slot.rfind("arm", 0) == 0 || slot.rfind("leg", 0) == 0;
}

int CharacterCutter::slotParent(const std::vector<std::string>& slots, const std::string& slot, int torsoIdx) {
    auto has = [&](const std::string& s) {
        return std::find(slots.begin(), slots.end(), s) != slots.end();
    };
    if (slot == "torso" || slot == "head") return torsoIdx; // torso = root; head follows torso
    if (slot == "armL_upper" || slot == "armR_upper" || slot == "legL_upper" || slot == "legR_upper")
        return torsoIdx;
    if (slot == "armL_lower" && has("armL_upper")) return (int)(std::find(slots.begin(), slots.end(), std::string("armL_upper")) - slots.begin());
    if (slot == "armR_lower" && has("armR_upper")) return (int)(std::find(slots.begin(), slots.end(), std::string("armR_upper")) - slots.begin());
    if (slot == "legL_lower" && has("legL_upper")) return (int)(std::find(slots.begin(), slots.end(), std::string("legL_upper")) - slots.begin());
    if (slot == "legR_lower" && has("legR_upper")) return (int)(std::find(slots.begin(), slots.end(), std::string("legR_upper")) - slots.begin());
    return torsoIdx; // armL / armR / legL / legR single-segment and anything else
}

bool CharacterCutter::build(Character& out, std::string* error) const {
    auto fail = [&](const std::string& m) { if (error) *error = m; return false; };
    if (src_.empty()) return fail("no source artwork");
    if (cuts_.empty()) return fail("no cut rectangles");

    // Paint order: back limbs → torso → front limbs → head.
    static const char* kOrder[] = {"legL_upper", "legL_lower", "legR_upper", "legR_lower",
                                   "legL", "legR", "torso",
                                   "armL_upper", "armL_lower", "armR_upper", "armR_lower",
                                   "armL", "armR", "head"};

    out = Character();
    out.name = "Cut Character";
    std::vector<std::string> slots;
    for (const CutSpec& c : cuts_) slots.push_back(c.slot);

    int torsoIdx = -1;
    for (size_t i = 0; i < cuts_.size(); ++i)
        if (cuts_[i].slot == "torso") torsoIdx = (int)i;
    if (torsoIdx < 0) {
        // fall back: first slot becomes the root
        torsoIdx = 0;
    }

    out.parts.clear();
    out.skeleton.bones.clear();
    std::vector<Vec2> pivotsSrc;
    std::vector<int> partOfSlot(cuts_.size(), -1);
    for (size_t i = 0; i < cuts_.size(); ++i) {
        CharacterPart part;
        part.name = cuts_[i].slot;
        Vec2 pivotInPart;
        part.sprite = extract(cuts_[i], pivotInPart);
        if (part.sprite.empty()) return fail("empty part: " + cuts_[i].slot);
        part.pivot = pivotInPart; // bone anchor inside the part sprite
        Vec2 pivotSrc = pivotInPart + Vec2((float)cuts_[i].x, (float)cuts_[i].y);
        pivotsSrc.push_back(pivotSrc);
        partOfSlot[i] = (int)out.parts.size();
        out.parts.push_back(std::move(part));
    }

    // Bones in slot order; bone length = sprite extent along the bone direction.
    std::map<std::string, int> boneBySlot;
    for (size_t i = 0; i < cuts_.size(); ++i) {
        const CutSpec& c = cuts_[i];
        Bone b;
        b.name = c.slot;
        b.spritePart = partOfSlot[i];
        const CharacterPart& part = out.parts[(size_t)partOfSlot[i]];
        b.spriteAnchor = part.name == c.slot ? Vec2(part.sprite.w, part.sprite.h) * 0.0f : Vec2();
        // recompute anchor properly below via pivotsSrc
        Vec2 pivotInPart = pivotsSrc[i] - Vec2((float)c.x, (float)c.y);
        b.spriteAnchor = pivotInPart;
        bool down = isSlotDown(c.slot);
        b.spriteDirAngle = down ? 1.5707963f : -1.5707963f;
        b.spriteLength = down ? ((float)c.h - pivotInPart.y) : (pivotInPart.y);
        if (b.spriteLength < 8.0f) b.spriteLength = down ? (float)c.h : (float)c.h;
        b.length = b.spriteLength;
        // rest pose + limits (relative to parent direction)
        if (c.slot == "torso") {
            b.restAngle = -1.5707963f; // torso points up from the hips
            b.hasLimits = true;
            b.minAngle = -1.5707963f - 0.6f;
            b.maxAngle = -1.5707963f + 0.6f;
        } else if (c.slot == "head") {
            b.restAngle = 0; // continue upward from the torso
            b.hasLimits = true;
            b.minAngle = -0.7f;
            b.maxAngle = 0.7f;
        } else {
            bool leftSide = c.slot.find("armL") == 0 || c.slot.find("legL") == 0;
            bool isLeg = c.slot.rfind("leg", 0) == 0;
            float splay = isLeg ? 0.12f : 0.35f;
            bool single = c.slot.find("_") == std::string::npos;
            const float PI = 3.14159265f;
            if (single) {
                // whole limb from the torso: hangs down = rest PI (parent points up)
                b.restAngle = PI + (leftSide ? splay : -splay);
                b.hasLimits = true;
                b.minAngle = PI - 2.6f;
                b.maxAngle = PI + 2.6f;
            } else {
                b.restAngle = 0; // lowers continue from the upper
                if (c.slot.find("upper") != std::string::npos) {
                    b.restAngle = PI + (leftSide ? splay : -splay); // hang down from the up-pointing torso
                    b.hasLimits = true;
                    b.minAngle = PI - 2.6f;
                    b.maxAngle = PI + 2.6f;
                } else {
                    bool isArm = c.slot.rfind("arm", 0) == 0;
                    bool isLeft = c.slot.find("armL") == 0 || c.slot.find("legL") == 0;
                    b.hasLimits = true;
                    if (isArm) { b.minAngle = isLeft ? -2.4f : -0.05f; b.maxAngle = isLeft ? 0.05f : 2.4f; }
                    else       { b.minAngle = isLeft ? -0.05f : -2.3f; b.maxAngle = isLeft ? 2.3f : 0.05f; }
                }
            }
        }
        int parentSlotIdx = slotParent(slots, c.slot, torsoIdx);
        if ((int)i == torsoIdx) {
            b.parentId = -1;
        } else {
            const std::string& pslot = slots[(size_t)parentSlotIdx];
            auto it = boneBySlot.find(pslot);
            b.parentId = (it != boneBySlot.end() && pslot != c.slot) ? it->second : -1;
            // Anatomical offsets in the parent bone's frame (x along bone, y perpendicular):
            // arms hang from the shoulders (near the torso tip), legs from the hips (torso start).
            if (c.slot.rfind("armL", 0) == 0 && c.slot.find("upper") != std::string::npos) {
                b.offset = Vec2(-6, -14);
            } else if (c.slot.rfind("armR", 0) == 0 && c.slot.find("upper") != std::string::npos) {
                b.offset = Vec2(-6, 14);
            } else if (c.slot.rfind("legL", 0) == 0 && c.slot.find("upper") != std::string::npos) {
                b.offset = Vec2(-(out.skeleton.bone(boneBySlot["torso"])->length) + 2.0f, -8);
            } else if (c.slot.rfind("legR", 0) == 0 && c.slot.find("upper") != std::string::npos) {
                b.offset = Vec2(-(out.skeleton.bone(boneBySlot["torso"])->length) + 2.0f, 8);
            }
        }
        int id = out.skeleton.add(b);
        boneBySlot[c.slot] = id;
    }
    // fix parents: second pass (some parents come later in slot order)
    for (size_t i = 0; i < cuts_.size(); ++i) {
        if ((int)i == torsoIdx) continue;
        int parentSlotIdx = slotParent(slots, cuts_[i].slot, torsoIdx);
        const std::string& pslot = slots[(size_t)parentSlotIdx];
        if (pslot != cuts_[i].slot) {
            auto it = boneBySlot.find(pslot);
            if (it != boneBySlot.end())
                out.skeleton.bones[i].parentId = it->second;
        }
    }
    if (!out.skeleton.valid()) return fail("built rig is invalid");

    // Draw order by slot priority.
    for (const char* slot : kOrder) {
        auto it = boneBySlot.find(slot);
        if (it != boneBySlot.end()) out.drawOrder.push_back(it->second);
    }
    for (const Bone& b : out.skeleton.bones)
        if (std::find(out.drawOrder.begin(), out.drawOrder.end(), b.id) == out.drawOrder.end())
            out.drawOrder.push_back(b.id);

    // Rest pose: torso pivot is the root anchor.
    out.track.setKey(0, Pose()); // empty key replaced below with real placement
    Pose rest;
    rest.rootPos = pivotsSrc[(size_t)torsoIdx];
    rest.rootAngle = 0;
    out.track.setKey(0, rest);
    return true;
}

} // namespace leptid
