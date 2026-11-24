// Leptid — OOA rig: bones, skeleton, pose. The IK/FK contract lives here.
//
// Pose representation: each bone stores its LOCAL rotation delta from the rest
// pose (restAngle + delta = direction relative to the parent bone's direction).
// Deltas (not absolute angles) make poses stable under parent motion, tween
// cleanly, and clamp naturally against per-bone joint limits.
#pragma once
#include <map>
#include <string>
#include <vector>
#include "core/math.h"

namespace leptid {

struct Bone {
    int id = -1;
    std::string name;
    int parentId = -1;              // -1 = root
    float length = 60;              // px, rest length
    float restAngle = 0;            // direction relative to parent bone direction (radians)
    Vec2 offset;                    // joint offset from parent tail (usually 0; root uses world pos)
    // Joint limits (radians) applied to the bone's WORLD angle in FK/IK,
    // expressed as limits on (parentWorldAngle + restAngle + delta) — i.e. the
    // bend relative to the parent direction. Set hasLimits=false for free joints.
    bool hasLimits = false;
    float minAngle = -3.14159f, maxAngle = 3.14159f;
    // Sprite binding (cut character parts). -1 = no sprite (plain bone).
    int spritePart = -1;
    Vec2 spriteAnchor;              // pivot in sprite pixel coords (bone start)
    float spriteDirAngle = 0;       // bone direction inside the sprite (radians, sprite-local)
    float spriteLength = 0;         // px in sprite from anchor to bone tip; 0 = use bone length as-is scale 1
};

struct Skeleton {
    std::vector<Bone> bones;               // parents always precede children
    const Bone* bone(int id) const;
    Bone* bone(int id);
    int add(Bone b);                       // returns id
    int findByName(const std::string& name) const;
    std::vector<int> chainToRoot(int boneId) const;   // boneId ... root (inclusive)
    bool valid() const;                    // parents precede children, no cycles
};

// A posed state: bone id -> local rotation delta (radians). Root position/rotation
// live separately so a character can be moved without touching joint angles.
struct Pose {
    Vec2 rootPos;                 // world position of root bone start
    float rootAngle = 0;          // world rotation of the root bone's rest frame
    std::map<int, float> angles;  // bone id -> delta from restAngle

    float delta(int boneId) const;
    void setDelta(int boneId, float d) { angles[boneId] = d; }
    bool operator==(const Pose& o) const {
        if (angles.size() != o.angles.size()) return false;
        for (auto& kv : angles) {
            auto it = o.angles.find(kv.first);
            if (it == o.angles.end() || std::fabs(it->second - kv.second) > 1e-6f) return false;
        }
        return std::fabs(rootPos.x - o.rootPos.x) < 1e-5f &&
               std::fabs(rootPos.y - o.rootPos.y) < 1e-5f &&
               std::fabs(normalizeAngle(rootAngle - o.rootAngle)) < 1e-5f;
    }
};

// Result of FK evaluation: world-space start/tip/angle per bone (indexed by bone id).
struct BoneWorld {
    Vec2 start, tip;
    float angle = 0;      // world angle of the bone direction
};

class FKEvaluator {
public:
    explicit FKEvaluator(const Skeleton& sk) : sk_(sk) {}
    // Fills `out` (must be sized >= bones.size()) from pose. BoneWorld indexed by bone id.
    void evaluate(const Pose& pose, std::vector<BoneWorld>& out) const;
    std::vector<BoneWorld> evaluate(const Pose& pose) const {
        std::vector<BoneWorld> out(sk_.bones.size());
        evaluate(pose, out);
        return out;
    }
    // World angle a bone would have with a given delta (no full evaluation).
    float worldAngle(const Pose& pose, int boneId) const;

private:
    const Skeleton& sk_;
};

// Build a standard humanoid skeleton (Phase 4B rig): root/hips → spine → torso/head,
// two arms (upper/lower, L/R) and two legs (upper/lower, L/R), with sensible
// bend limits so IK never folds joints the wrong way. Bone lengths come from
// `scale` (px per unit); callers bind sprites afterwards.
Skeleton makeHumanoidSkeleton(float scale = 1.0f);

} // namespace leptid
