// Leptid — stable 2D IK solvers.
//
// Design goals (the "doesn't fling limbs everywhere" contract):
//   1. Analytic two-bone solver for arm/leg chains: closed-form, deterministic,
//      no iteration overshoot, elbow keeps its bend side.
//   2. Damped FABRIK for longer chains, with a capped iteration count.
//   3. Per-joint angle limits (elbows/knees only bend one way) enforced on the
//      LOCAL bend angle — rotation-invariant, so moving the character root
//      never breaks limits.
//   4. Per-solve maximum angular step: no solver call can rotate a joint more
//      than IKOptions::maxStepPerSolve radians, so a wild target jump produces
//      a smooth glide, never a teleport or whirl.
//   5. Unreachable targets straighten the chain toward the target instead of
//      oscillating (fixed segment lengths by construction).
#pragma once
#include <vector>
#include "rig/bone.h"

namespace leptid {

struct IKOptions {
    int maxIterations = 14;        // FABRIK iteration cap
    float tolerance = 0.4f;        // px — early-out distance
    float maxStepPerSolve = degToRad(24.0f); // anti-fling: max per-joint rotation per solve
    bool enforceLimits = true;
};

// Solve IK for a chain of bones (parent -> ... -> end effector at last bone's tip).
// Chooses the analytic two-bone solver for chains of length 2, FABRIK otherwise.
// `bendHint` (2-bone): preferred elbow side, +1 or -1; 0 = keep the current bend side.
// Returns true if the end effector reached the target within tolerance.
bool solveChainIK(const Skeleton& sk, Pose& pose, const std::vector<int>& chain,
                  const Vec2& target, const IKOptions& opt = {}, float bendHint = 0);

// Analytic two-bone IK (upper + lower). Stable, closed-form; result is clamped
// by the joint limits and by maxStepPerSolve (anti-fling).
bool solveTwoBoneIK(const Skeleton& sk, Pose& pose, int upperId, int lowerId,
                    const Vec2& target, float bendHint = 0, const IKOptions& opt = {});

// Damped FABRIK for longer chains. Root of the chain stays pinned.
bool solveFABRIK(const Skeleton& sk, Pose& pose, const std::vector<int>& chain,
                 const Vec2& target, const IKOptions& opt = {});

// Clamp every bone's LOCAL bend angle (restAngle + delta) into its joint limits.
void clampPoseToLimits(const Skeleton& sk, Pose& pose);

// Anti-fling clamp: no joint may rotate more than `maxStep` between the two poses.
void clampPoseStep(const Pose& from, Pose& to, float maxStep);

// Frame-rate independent pose blending (tweening + smoothing): alpha in [0,1].
Pose smoothPose(const Pose& prev, const Pose& target, float alpha);

// End-effector position of a chain in world space (FK).
Vec2 chainEndEffector(const Skeleton& sk, const Pose& pose, const std::vector<int>& chain);

// Current bend side of a two-bone chain: +1 / -1 via cross product of the
// shoulder->target line against the elbow offset; 0 if degenerate.
float currentBendSign(const Skeleton& sk, const Pose& pose, int upperId, int lowerId);

} // namespace leptid
