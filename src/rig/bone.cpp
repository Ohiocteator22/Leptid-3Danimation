// Leptid — skeleton + FK implementation, humanoid template.
#include "rig/bone.h"

namespace leptid {

const Bone* Skeleton::bone(int id) const {
    for (const Bone& b : bones) if (b.id == id) return &b;
    return nullptr;
}
Bone* Skeleton::bone(int id) {
    for (Bone& b : bones) if (b.id == id) return &b;
    return nullptr;
}
int Skeleton::add(Bone b) {
    b.id = bones.empty() ? 0 : bones.back().id + 1;
    bones.push_back(b);
    return b.id;
}
int Skeleton::findByName(const std::string& name) const {
    for (const Bone& b : bones) if (b.name == name) return b.id;
    return -1;
}
std::vector<int> Skeleton::chainToRoot(int boneId) const {
    std::vector<int> chain;
    int cur = boneId;
    int guard = 0;
    while (cur >= 0 && guard++ < 256) {
        chain.push_back(cur);
        const Bone* b = bone(cur);
        if (!b) break;
        cur = b->parentId;
    }
    return chain;
}
bool Skeleton::valid() const {
    for (size_t i = 0; i < bones.size(); ++i) {
        int p = bones[i].parentId;
        if (p < -1) return false;
        if (p >= 0) {
            // parent must exist and precede the child (no cycles by construction)
            int parentIdx = -1;
            for (size_t j = 0; j < bones.size(); ++j)
                if (bones[j].id == p) { parentIdx = (int)j; break; }
            if (parentIdx < 0 || parentIdx >= (int)i) return false;
        }
        if (bones[i].length <= 0) return false;
    }
    return true;
}

float Pose::delta(int boneId) const {
    auto it = angles.find(boneId);
    return it == angles.end() ? 0.0f : it->second;
}

void FKEvaluator::evaluate(const Pose& pose, std::vector<BoneWorld>& out) const {
    out.resize(sk_.bones.size());
    for (size_t i = 0; i < sk_.bones.size(); ++i) {
        const Bone& b = sk_.bones[i];
        float localAngle = b.restAngle + pose.delta(b.id); // direction in parent frame
        Vec2 start;
        if (b.parentId < 0) {
            // Root: offset is relative to the root's world frame.
            Mat3 rootRot = Mat3::translation(pose.rootPos.x, pose.rootPos.y) * Mat3::rotation(pose.rootAngle);
            start = rootRot.apply(b.offset.x, b.offset.y);
            out[i].angle = pose.rootAngle + localAngle;
        } else {
            const BoneWorld& parent = out[(size_t)b.parentId];
            // offset in parent bone's frame (x along parent direction, y perpendicular)
            float pa = parent.angle;
            Vec2 px = Vec2::fromAngle(pa), py = Vec2::fromAngle(pa + 1.5707963f);
            start = parent.tip + px * b.offset.x + py * b.offset.y;
            out[i].angle = parent.angle + localAngle;
        }
        out[i].start = start;
        out[i].tip = start + Vec2::fromAngle(out[i].angle) * b.length;
    }
}

float FKEvaluator::worldAngle(const Pose& pose, int boneId) const {
    const Bone* b = sk_.bone(boneId);
    if (!b) return 0;
    float angle = b->restAngle + pose.delta(boneId);
    int cur = b->parentId;
    int guard = 0;
    while (cur >= 0 && guard++ < 256) {
        const Bone* pb = sk_.bone(cur);
        if (!pb) break;
        angle += pb->restAngle + pose.delta(cur);
        cur = pb->parentId;
    }
    if (b->parentId < 0) angle += pose.rootAngle;
    return angle;
}

Skeleton makeHumanoidSkeleton(float scale) {
    Skeleton sk;
    auto add = [&](const std::string& name, int parent, float len, float rest,
                   bool limits, float lo, float hi) {
        Bone b;
        b.name = name;
        b.parentId = parent;
        b.length = len * scale;
        b.restAngle = rest;
        b.hasLimits = limits;
        b.minAngle = lo;
        b.maxAngle = hi;
        return sk.add(b);
    };
    // Geometry (y-down screen space). Child world angle = parent world + restAngle (+ delta).
    // The root bone points UP from the hips; the spine, neck and head continue upward;
    // arms hang down from the spine tip; legs point down from the root tip.
    // Joint limits constrain the LOCAL bend (rest + delta) so they are rotation-invariant.
    const float PI = 3.14159265f;
    const float UP = -1.5707963f;
    int root = add("root", -1, 8, UP, false, 0, 0);
    int spine = add("spine", root, 46, 0, true, -0.6f, 0.6f);          // ±34° sway
    int neck = add("neck", spine, 10, 0, true, -0.6f, 0.6f);
    int head = add("head", neck, 26, 0, true, -0.7f, 0.7f);
    // Arms: parent (spine) points UP, so hanging down = rest PI (± splay).
    // Offsets: shoulders slightly below the neck, legs at the hips.
    int armL_up = add("armL_upper", spine, 34, PI + 0.30f, true, PI - 2.6f, PI + 2.6f);
    sk.bones[armL_up].offset = Vec2(-6, -14);
    int armL_lo = add("armL_lower", armL_up, 32, 0, true, -2.4f, 0.05f);  // elbow bends one way
    int armR_up = add("armR_upper", spine, 34, PI - 0.30f, true, PI - 2.6f, PI + 2.6f);
    sk.bones[armR_up].offset = Vec2(-6, 14);
    int armR_lo = add("armR_lower", armR_up, 32, 0, true, -0.05f, 2.4f);
    // Legs: parent (root) points UP, so straight down = rest PI (± splay); hips offset.
    int legL_up = add("legL_upper", root, 38, PI + 0.12f, true, PI - 1.5f, PI + 1.5f);
    sk.bones[legL_up].offset = Vec2(-8, -8);
    int legL_lo = add("legL_lower", legL_up, 38, 0, true, -0.05f, 2.3f);
    int legR_up = add("legR_upper", root, 38, PI - 0.12f, true, PI - 1.5f, PI + 1.5f);
    sk.bones[legR_up].offset = Vec2(-8, 8);
    int legR_lo = add("legR_lower", legR_up, 38, 0, true, -2.3f, 0.05f);
    (void)neck; (void)head; (void)armL_up; (void)armL_lo; (void)armR_up; (void)armR_lo;
    (void)legL_up; (void)legL_lo; (void)legR_up; (void)legR_lo; (void)root; (void)spine;
    return sk;
}

} // namespace leptid
