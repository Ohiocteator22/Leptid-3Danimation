// Leptid — pose keyframe tracks with tweening (Phase 4J / feature queue J).
// Fixes the old limitation where a character held its pose until the next key:
// poses now interpolate between keyed frames with easing.
#pragma once
#include <vector>
#include "rig/bone.h"

namespace leptid {

enum class Ease { Linear, EaseIn, EaseOut, EaseInOut, Step };

float applyEase(Ease e, float t);

struct PoseKey {
    int frame = 0;
    Pose pose;
    Ease ease = Ease::EaseInOut;
};

// One whole-pose track per character animation (matches the OOA keyframing model).
class PoseTrack {
public:
    void setKey(int frame, const Pose& pose, Ease ease = Ease::EaseInOut);
    void removeKey(int frame);                    // no-op if absent
    bool hasKey(int frame) const;
    void moveKey(int fromFrame, int toFrame);
    int keyCount() const { return (int)keys_.size(); }
    const std::vector<PoseKey>& keys() const { return keys_; }
    bool getKey(int frame, PoseKey& out) const;

    // Empty track or frame before first / after last key → nearest key's pose (hold).
    // Between two keys → eased interpolation (tween) of root pos/angle and per-bone
    // angles (shortest-path). A bone missing from a key is treated as at rest there.
    Pose sample(int frame) const;
    int lastKeyFrame() const;                     // -1 if empty
    int firstKeyFrame() const;                    // -1 if empty

private:
    std::vector<PoseKey> keys_;                   // sorted by frame
};

} // namespace leptid
