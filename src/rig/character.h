// Leptid — OOA character: cut limb sprites bound to a skeleton + pose track.
#pragma once
#include <string>
#include <vector>
#include "core/image.h"
#include "rig/bone.h"
#include "rig/keyframes.h"

namespace leptid {

struct CharacterPart {
    std::string name;      // e.g. "head", "armL_upper"
    Image sprite;          // limb PNG cut from the T-pose artwork
    Vec2 pivot;            // anchor point in sprite pixels (bone start position)
};

struct Character {
    std::string id;        // stable within a project (e.g. "char1")
    std::string name = "Character";
    std::vector<CharacterPart> parts;
    Skeleton skeleton;
    PoseTrack track;       // whole-pose keyframes (the animation)
    std::vector<int> drawOrder; // bone ids in paint order (default: skeleton order)

    Pose poseAtFrame(int frame) const { return track.sample(frame); }
    int animationEndFrame() const { return track.lastKeyFrame(); }

    // Render the posed character (FK + limb sprites) into dst. Bone world
    // transforms map each limb sprite: pivot -> bone start, sprite bone
    // direction -> bone world angle, sprite bone length -> bone length.
    void render(Image& dst, const Pose& pose, float opacity = 1.0f) const;
    void ensureDrawOrder();
};

} // namespace leptid
