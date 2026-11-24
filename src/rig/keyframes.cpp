// Leptid — pose track implementation (tweening).
#include "rig/keyframes.h"
#include <algorithm>

namespace leptid {

float applyEase(Ease e, float t) {
    t = clampf(t, 0, 1);
    switch (e) {
        case Ease::Linear: return t;
        case Ease::EaseIn: return easeInCubic(t);
        case Ease::EaseOut: return easeOutCubic(t);
        case Ease::EaseInOut: return easeInOutCubic(t);
        case Ease::Step: return t >= 1.0f ? 1.0f : 0.0f;
    }
    return t;
}

void PoseTrack::setKey(int frame, const Pose& pose, Ease ease) {
    for (PoseKey& k : keys_)
        if (k.frame == frame) { k.pose = pose; k.ease = ease; return; }
    PoseKey k;
    k.frame = frame;
    k.pose = pose;
    k.ease = ease;
    keys_.push_back(k);
    std::sort(keys_.begin(), keys_.end(),
              [](const PoseKey& a, const PoseKey& b) { return a.frame < b.frame; });
}

void PoseTrack::removeKey(int frame) {
    keys_.erase(std::remove_if(keys_.begin(), keys_.end(),
                               [frame](const PoseKey& k) { return k.frame == frame; }),
                keys_.end());
}

bool PoseTrack::hasKey(int frame) const {
    for (const PoseKey& k : keys_) if (k.frame == frame) return true;
    return false;
}

void PoseTrack::moveKey(int fromFrame, int toFrame) {
    for (PoseKey& k : keys_)
        if (k.frame == fromFrame) k.frame = toFrame;
    std::sort(keys_.begin(), keys_.end(),
              [](const PoseKey& a, const PoseKey& b) { return a.frame < b.frame; });
}

bool PoseTrack::getKey(int frame, PoseKey& out) const {
    for (const PoseKey& k : keys_)
        if (k.frame == frame) { out = k; return true; }
    return false;
}

int PoseTrack::lastKeyFrame() const {
    return keys_.empty() ? -1 : keys_.back().frame;
}
int PoseTrack::firstKeyFrame() const {
    return keys_.empty() ? -1 : keys_.front().frame;
}

Pose PoseTrack::sample(int frame) const {
    if (keys_.empty()) return Pose();
    if (frame <= keys_.front().frame) return keys_.front().pose;
    if (frame >= keys_.back().frame) return keys_.back().pose;
    // find surrounding keys
    size_t hi = 0;
    while (hi < keys_.size() && keys_[hi].frame < frame) ++hi;
    const PoseKey& a = keys_[hi - 1];
    const PoseKey& b = keys_[hi];
    if (a.frame == b.frame) return b.pose;
    float t = applyEase(b.ease, (float)(frame - a.frame) / (float)(b.frame - a.frame));
    // Tween: shortest-path angle interpolation per bone. A bone missing from a
    // key is treated as being at REST (delta 0) in that key, so partial keyframes
    // still tween correctly. Root pos/angle interpolate linearly/shortest-path.
    Pose out;
    out.rootPos = a.pose.rootPos + (b.pose.rootPos - a.pose.rootPos) * t;
    out.rootAngle = lerpAngle(a.pose.rootAngle, b.pose.rootAngle, t);
    for (auto& kv : b.pose.angles) out.angles[kv.first] = kv.second;
    for (auto& kv : a.pose.angles) out.angles[kv.first] = kv.second;
    for (auto& kv : out.angles)
        kv.second = lerpAngle(a.pose.delta(kv.first), b.pose.delta(kv.first), t);
    return out;
}

} // namespace leptid
