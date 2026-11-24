// Leptid — IK implementation.
#include "rig/ik.h"

namespace leptid {

static const float PI = 3.14159265358979f;

void clampPoseToLimits(const Skeleton& sk, Pose& pose) {
    // Limits are on the LOCAL bend angle (restAngle + delta): rotation-invariant.
    for (const Bone& b : sk.bones) {
        if (!b.hasLimits) continue;
        float local = b.restAngle + pose.delta(b.id);
        float clamped = clampAngleRange(local, b.minAngle, b.maxAngle);
        if (std::fabs(clamped - local) > 1e-6f)
            pose.angles[b.id] = clamped - b.restAngle;
    }
}

void clampPoseStep(const Pose& from, Pose& to, float maxStep) {
    if (maxStep <= 0) return;
    for (auto& kv : to.angles) {
        float cur = from.delta(kv.first);
        float step = normalizeAngle(kv.second - cur);
        if (std::fabs(step) > maxStep)
            kv.second = cur + (step > 0 ? maxStep : -maxStep);
    }
    // Root rotation and position get the same gentle treatment (translation in px).
    float rStep = normalizeAngle(to.rootAngle - from.rootAngle);
    if (std::fabs(rStep) > maxStep)
        to.rootAngle = from.rootAngle + (rStep > 0 ? maxStep : -maxStep);
    Vec2 dPos = to.rootPos - from.rootPos;
    float dLen = dPos.len();
    if (dLen > maxStep * 4.0f) // allow root to travel 4x further per solve (it carries the whole body)
        to.rootPos = from.rootPos + dPos.normalized() * (maxStep * 4.0f);
}

Pose smoothPose(const Pose& prev, const Pose& target, float alpha) {
    alpha = clampf(alpha, 0, 1);
    Pose out;
    out.rootPos = prev.rootPos + (target.rootPos - prev.rootPos) * alpha;
    out.rootAngle = lerpAngle(prev.rootAngle, target.rootAngle, alpha);
    // Union of both angle sets: bones only in one pose copy that value.
    for (auto& kv : target.angles) out.angles[kv.first] = kv.second;
    for (auto& kv : prev.angles) {
        auto it = out.angles.find(kv.first);
        if (it == out.angles.end()) out.angles[kv.first] = kv.second;
        else it->second = lerpAngle(kv.second, it->second, alpha);
    }
    return out;
}

Vec2 chainEndEffector(const Skeleton& sk, const Pose& pose, const std::vector<int>& chain) {
    if (chain.empty()) return pose.rootPos;
    FKEvaluator fk(sk);
    auto world = fk.evaluate(pose);
    return world[(size_t)chain.back()].tip;
}

float currentBendSign(const Skeleton& sk, const Pose& pose, int upperId, int lowerId) {
    FKEvaluator fk(sk);
    auto world = fk.evaluate(pose);
    const Bone* lower = sk.bone(lowerId);
    if (!lower) return 1;
    Vec2 S = world[(size_t)upperId].start;
    Vec2 T = world[(size_t)lowerId].tip;
    Vec2 E = world[(size_t)lowerId].start;
    Vec2 line = T - S;
    Vec2 elbow = E - S;
    float cr = line.cross(elbow);
    if (std::fabs(cr) < 1e-4f) return 1;
    return cr > 0 ? 1.0f : -1.0f;
}

bool solveTwoBoneIK(const Skeleton& sk, Pose& pose, int upperId, int lowerId,
                    const Vec2& target, float bendHint, const IKOptions& opt) {
    const bool enforceLimits = opt.enforceLimits;
    const Bone* B1 = sk.bone(upperId);
    const Bone* B2 = sk.bone(lowerId);
    if (!B1 || !B2 || B2->parentId != upperId) return false;

    FKEvaluator fk(sk);
    auto world = fk.evaluate(pose);
    Vec2 S = world[(size_t)upperId].start;
    float L1 = B1->length, L2 = B2->length;

    Vec2 D = target - S;
    float d = D.len();
    if (d < 1e-4f) {
        // Degenerate: keep current upper direction, fold the forearm.
        D = Vec2::fromAngle(world[(size_t)upperId].angle);
        d = 1e-4f;
    }
    Vec2 dir = D / d;

    // Clamp the reach: never overshoot (fixed lengths by construction).
    float dMin = std::fabs(L1 - L2) + 1e-3f;
    float dMax = L1 + L2 - 1e-3f;
    d = clampf(d, dMin, dMax);
    Vec2 bestPoint = S + dir * d;   // the closest the chain can get to the target

    float cosA = clampf((L1 * L1 + d * d - L2 * L2) / (2 * L1 * d + 1e-9f), -1, 1);
    float shoulderOff = std::acos(cosA);
    float cosE = clampf((L1 * L1 + L2 * L2 - d * d) / (2 * L1 * L2 + 1e-9f), -1, 1);
    float elbowTheta = std::acos(cosE);

    float parentW;
    if (B1->parentId >= 0) parentW = fk.worldAngle(pose, B1->parentId);
    else parentW = pose.rootAngle;

    // Try both bend sides; elbows/knees only bend one way, so the hinted side may
    // be illegal — after clamping to limits it would land far from the target.
    // Evaluate both candidates and keep the one that actually reaches best.
    float hint = bendHint;
    if (std::fabs(hint) < 1e-4f) hint = currentBendSign(sk, pose, upperId, lowerId);
    hint = hint >= 0 ? 1.0f : -1.0f;

    Pose best = pose;
    float bestErr = 1e30f;
    for (int pass = 0; pass < 2; ++pass) {
        float bend = (pass == 0) ? hint : -hint;
        Pose cand = pose;
        float upperWorld = Vec2::angleOf(dir) + bend * shoulderOff;
        float lowerWorld = upperWorld + PI + bend * elbowTheta;
        cand.angles[upperId] = normalizeAngle(upperWorld - parentW - B1->restAngle);
        cand.angles[lowerId] = normalizeAngle(lowerWorld - upperWorld - B2->restAngle);
        if (enforceLimits) clampPoseToLimits(sk, cand);
        FKEvaluator fk2(sk);
        auto w2 = fk2.evaluate(cand);
        float err = Vec2::dist(w2[(size_t)lowerId].tip, bestPoint);
        if (err < bestErr - 1e-4f) {
            bestErr = err;
            best = cand;
        }
    }
    clampPoseStep(pose, best, opt.maxStepPerSolve); // anti-fling: bounded rotation per solve
    pose = best;
    return Vec2::dist(chainEndEffector(sk, pose, {upperId, lowerId}), target) <= opt.tolerance * 2.0f;
}

bool solveFABRIK(const Skeleton& sk, Pose& pose, const std::vector<int>& chain,
                 const Vec2& target, const IKOptions& opt) {
    if (chain.empty()) return false;
    if (chain.size() == 2) return solveTwoBoneIK(sk, pose, chain[0], chain[1], target, 0, opt);

    FKEvaluator fk(sk);
    auto world = fk.evaluate(pose);
    size_t n = chain.size();
    std::vector<Vec2> p(n + 1);         // joint positions: p[0] = chain root start ... p[n] = end effector
    for (size_t i = 0; i < n; ++i) {
        const BoneWorld& bw = world[(size_t)chain[i]];
        p[i] = bw.start;
        if (i == n - 1) p[i + 1] = bw.tip;
    }
    Vec2 rootPinned = p[0];
    std::vector<float> L(n);
    for (size_t i = 0; i < n; ++i) {
        const Bone* b = sk.bone(chain[i]);
        L[i] = b ? b->length : 1.0f;
        if (L[i] < 1e-3f) L[i] = 1e-3f;
    }

    float total = 0;
    for (float l : L) total += l;
    Vec2 toTarget = target - rootPinned;
    bool reachable = toTarget.len() <= total;

    Pose startPose = pose;              // for the max-step clamp
    for (int iter = 0; iter < opt.maxIterations; ++iter) {
        if (!reachable) {
            // Straighten the whole chain toward the target (deterministic, no oscillation).
            Vec2 dir = toTarget.normalized();
            for (size_t i = 0; i < n; ++i)
                p[i + 1] = p[i] + dir * L[i];
            break;
        }
        // Forward pass: pull the end effector to the target.
        p[n] = target;
        for (size_t i = n; i > 0; --i) {
            Vec2 d = p[i - 1] - p[i];
            float len = d.len();
            if (len < 1e-6f) d = Vec2(1, 0);
            else d = d / len;
            p[i - 1] = p[i] + d * L[i - 1];
        }
        // Backward pass: restore the pinned root.
        p[0] = rootPinned;
        for (size_t i = 0; i < n; ++i) {
            Vec2 d = p[i + 1] - p[i];
            float len = d.len();
            if (len < 1e-6f) d = Vec2(1, 0);
            else d = d / len;
            p[i + 1] = p[i] + d * L[i];
        }
        float err = Vec2::dist(p[n], target);
        if (err < opt.tolerance) break;
    }

    // Convert joint positions back to local deltas, walking the chain.
    Pose out = pose;
    float prevWorld = 0;
    for (size_t i = 0; i < n; ++i) {
        const Bone* b = sk.bone(chain[i]);
        if (!b) continue;
        float worldA = Vec2::angleOf(p[i + 1] - p[i]);
        if (i == 0) {
            float parentW = b->parentId >= 0 ? fk.worldAngle(pose, b->parentId) : pose.rootAngle;
            out.angles[b->id] = normalizeAngle(worldA - parentW - b->restAngle);
        } else {
            out.angles[b->id] = normalizeAngle(worldA - prevWorld - b->restAngle);
        }
        prevWorld = worldA;
        (void)b;
    }
    if (opt.enforceLimits) clampPoseToLimits(sk, out);
    clampPoseStep(startPose, out, opt.maxStepPerSolve);
    pose = out;
    return Vec2::dist(chainEndEffector(sk, pose, chain), target) <= opt.tolerance * 2.0f;
}

bool solveChainIK(const Skeleton& sk, Pose& pose, const std::vector<int>& chain,
                  const Vec2& target, const IKOptions& opt, float bendHint) {
    if (chain.size() == 2)
        return solveTwoBoneIK(sk, pose, chain[0], chain[1], target, bendHint, opt);
    return solveFABRIK(sk, pose, chain, target, opt);
}

} // namespace leptid
