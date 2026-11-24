// Leptid — rig tests: FK correctness, IK stability (the anti-fling contract),
// joint limits, bend-side preservation, pose tweening.
#include <vector>
#include "testfw.h"
#include "rig/bone.h"
#include "rig/ik.h"
#include "rig/keyframes.h"

using namespace leptid;
using namespace testfw;

namespace {

struct RigFixture {
    Skeleton sk;
    int armUp, armLo;      // left arm chain
    RigFixture() {
        sk = makeHumanoidSkeleton(1.0f);
        armUp = sk.findByName("armL_upper");
        armLo = sk.findByName("armL_lower");
        CHECK(sk.valid());
    }
    // Sweep a target along a polyline, applying one IK solve per step, and
    // verify: (a) no joint ever rotates more than maxStep + eps per solve,
    // (b) the end effector never leaves the chain's reachable disc by much.
    void sweepTest(const std::vector<Vec2>& path, const IKOptions& opt, float reachLen,
                   std::vector<int>* chainOverride = nullptr) {
        std::vector<int> chain{armUp, armLo};
        if (chainOverride) chain = *chainOverride;
        Pose pose;
        pose.rootPos = Vec2(200, 300);
        Vec2 prevTip = chainEndEffector(sk, pose, chain);
        float maxStep = opt.maxStepPerSolve;
        for (const Vec2& t : path) {
            Pose before = pose;
            solveChainIK(sk, pose, chain, t, opt);
            clampPoseToLimits(sk, pose);
            for (auto& kv : pose.angles) {
                float step = std::fabs(normalizeAngle(kv.second - before.delta(kv.first)));
                CHECK(step <= maxStep + 1e-3f);
            }
            Vec2 tip = chainEndEffector(sk, pose, chain);
            Vec2 S = FKEvaluator(sk).evaluate(pose)[(size_t)chain[0]].start;
            float dist = Vec2::dist(S, tip);
            CHECK(dist <= reachLen + 1.5f); // never flung past the reach
            prevTip = tip;
        }
    }
};

void testFKRestPose() {
    RigFixture f;
    Pose pose;
    pose.rootPos = Vec2(100, 200);
    FKEvaluator fk(f.sk);
    auto world = fk.evaluate(pose);
    int spine = f.sk.findByName("spine");
    int head = f.sk.findByName("head");
    int legLo = f.sk.findByName("legR_lower");
    // root points up (−PI/2): root tip is 8px above the anchor; spine continues up.
    CHECK_NEAR(world[(size_t)spine].angle, -1.5707963f, 1e-4);
    CHECK_NEAR(world[(size_t)spine].start.x, 100.0f, 1e-3);
    CHECK_NEAR(world[(size_t)spine].start.y, 200.0f - 8.0f, 1e-3);
    // chain lengths multiply out: spine(46) + neck(10) + head(26) from that point
    CHECK_NEAR(Vec2::dist(world[(size_t)head].start, world[(size_t)spine].start), 56.0f, 1e-3);
    CHECK_NEAR(Vec2::dist(world[(size_t)legLo].tip, world[(size_t)legLo].start), 38.0f, 1e-3);
}

void testTwoBoneReach() {
    RigFixture f;
    IKOptions opt;
    opt.maxStepPerSolve = degToRad(360); // disable the step clamp for exact-reach checks
    Pose pose;
    pose.rootPos = Vec2(200, 300);
    std::vector<int> chain{f.armUp, f.armLo};
    FKEvaluator fk(f.sk);
    Vec2 S = fk.evaluate(pose)[(size_t)f.armUp].start;
    float reach = 34.0f + 32.0f;

    // Mid-reach target: end effector lands on the target.
    Vec2 t1 = S + Vec2(40, 0);
    solveChainIK(f.sk, pose, chain, t1, opt);
    Vec2 tip = chainEndEffector(f.sk, pose, chain);
    CHECK_NEAR(Vec2::dist(tip, t1), 0.0f, 0.6f);

    // Far target: straighten toward it, never overshoot. (−45° is within the
    // arm's swing limits [world −59°..239°]; the limit clamp must NOT bend it.)
    Vec2 t2 = S + Vec2::fromAngle(degToRad(-45)) * 300.0f;
    solveChainIK(f.sk, pose, chain, t2, opt);
    tip = chainEndEffector(f.sk, pose, chain);
    CHECK_NEAR(Vec2::dist(S, tip), reach, 0.8f);
    // aligned with the target ray
    Vec2 dir = (t2 - S).normalized();
    CHECK_NEAR((tip - S).normalized().dot(dir), 1.0f, 0.01f);

    // Very close target (inside |L1-L2|): folds, no flip through the body.
    Vec2 t3 = S + Vec2(1.2f, 0.5f);
    solveChainIK(f.sk, pose, chain, t3, opt);
    const Bone* lo = f.sk.bone(f.armLo);
    float elbowBend = lo->restAngle + pose.delta(f.armLo);
    CHECK(clampAngleRange(elbowBend, lo->minAngle, lo->maxAngle) == elbowBend);
}

void testJointLimitsHold() {
    RigFixture f;
    IKOptions opt;
    opt.maxStepPerSolve = degToRad(360);
    Pose pose;
    pose.rootPos = Vec2(200, 300);
    std::vector<int> chain{f.armUp, f.armLo};
    // Hammer the elbow from many directions; limits must hold every time.
    for (int i = 0; i < 72; ++i) {
        Vec2 t = Vec2(200, 300) + Vec2::fromAngle((float)i * 0.35f) * (30.0f + (float)(i % 5) * 8.0f);
        solveChainIK(f.sk, pose, chain, t, opt);
        const Bone* lo = f.sk.bone(f.armLo);
        float bend = lo->restAngle + pose.delta(f.armLo);
        CHECK(clampAngleRange(bend, lo->minAngle, lo->maxAngle) == bend);
        // and the knee (right leg) stays untouched by an arm solve
        CHECK(pose.delta(f.sk.findByName("legR_lower")) == 0.0f);
    }
}

void testBendSidePreserved() {
    RigFixture f;
    IKOptions opt;
    opt.maxStepPerSolve = degToRad(360);
    Pose pose;
    pose.rootPos = Vec2(200, 300);
    std::vector<int> chain{f.armUp, f.armLo};
    // Establish a bent elbow, then drag the hand around; bend side must not flip.
    Vec2 start = Vec2(220, 330);
    solveChainIK(f.sk, pose, chain, start, opt, 1.0f);
    float s0 = currentBendSign(f.sk, pose, f.armUp, f.armLo);
    for (int i = 0; i < 40; ++i) {
        Vec2 t = start + Vec2::fromAngle((float)i * 0.28f) * (10.0f + (float)(i % 4) * 6.0f);
        solveChainIK(f.sk, pose, chain, t, opt); // hint 0 = preserve
        CHECK(currentBendSign(f.sk, pose, f.armUp, f.armLo) == s0);
    }
}

void testAntiFlingSweep() {
    RigFixture f;
    IKOptions opt; // defaults: maxStepPerSolve = 24°
    Pose pose;
    pose.rootPos = Vec2(200, 300);
    // Wild teleporting target: the solver must NEVER whirl joints.
    std::vector<Vec2> path;
    for (int i = 0; i < 300; ++i) {
        float a = (float)(i * 37 % 360) * 0.06f;
        float r = 10.0f + (float)(i * 53 % 200);
        path.push_back(Vec2(200, 300) + Vec2::fromAngle(a) * r);
    }
    f.sweepTest(path, opt, 66.0f);
}

void testAntiFlingConvergence() {
    RigFixture f;
    IKOptions opt; // step clamp ON
    Pose pose;
    pose.rootPos = Vec2(200, 300);
    std::vector<int> chain{f.armUp, f.armLo};
    FKEvaluator fk(f.sk);
    Vec2 S = fk.evaluate(pose)[(size_t)f.armUp].start;
    Vec2 t = S + Vec2(45, -30); // reachable
    // Solve repeatedly from a bent start: must glide to the target, not teleport,
    // and converge within a bounded number of solves.
    Vec2 start = S + Vec2(60, 20);
    solveChainIK(f.sk, pose, chain, start, opt, degToRad(360)); // place hand (no clamp)
    int frames = 0;
    while (frames < 60) {
        solveChainIK(f.sk, pose, chain, t, opt);
        ++frames;
        Vec2 tip = chainEndEffector(f.sk, pose, chain);
        if (Vec2::dist(tip, t) < 1.0f) break;
    }
    CHECK(frames < 60);
    CHECK_NEAR(Vec2::dist(chainEndEffector(f.sk, pose, chain), t), 0.0f, 1.0f);
}

void testFABRIKChainStable() {
    RigFixture f;
    // 3-bone chain: spine → neck → head target (e.g., "look at" or root-driven reach).
    std::vector<int> chain{f.sk.findByName("spine"), f.sk.findByName("neck"), f.sk.findByName("head")};
    IKOptions opt;
    Pose pose;
    pose.rootPos = Vec2(200, 300);
    float reach = 46.0f + 10.0f + 26.0f;
    std::vector<Vec2> path;
    for (int i = 0; i < 120; ++i) {
        float a = (float)i * 0.19f;
        path.push_back(Vec2(200, 300) + Vec2::fromAngle(a) * (20.0f + (float)(i % 3) * 30.0f));
    }
    f.sweepTest(path, opt, reach, &chain);
    // Beyond reach: chain straightens and never oscillates between solves.
    Pose p2;
    p2.rootPos = Vec2(200, 300);
    for (int i = 0; i < 30; ++i) {
        Pose before = p2;
        solveFABRIK(f.sk, p2, chain, Vec2(200 + 300, 300 + 400), opt);
        for (auto& kv : p2.angles) {
            float step = std::fabs(normalizeAngle(kv.second - before.delta(kv.first)));
            CHECK(step <= opt.maxStepPerSolve + 1e-3f);
        }
    }
}

void testTweening() {
    RigFixture f;
    PoseTrack track;
    Pose a, b;
    a.rootPos = Vec2(100, 100);
    b.rootPos = Vec2(200, 180);
    b.rootAngle = degToRad(10);
    b.setDelta(f.armUp, degToRad(-30));   // raise the arm
    b.setDelta(f.armLo, degToRad(-45));   // bend the elbow
    track.setKey(0, a);
    track.setKey(10, b, Ease::EaseInOut);
    Pose mid = track.sample(5);
    CHECK(mid.rootPos.x > 100 && mid.rootPos.x < 200);
    CHECK(mid.delta(f.armUp) < -1e-3f && mid.delta(f.armUp) > degToRad(-30));
    CHECK(mid.delta(f.armLo) < -1e-3f);
    // hold before/after keys
    Pose before = track.sample(-3);
    CHECK(before.rootPos == a.rootPos);
    Pose after = track.sample(25);
    CHECK(after.rootPos == b.rootPos);
    CHECK_NEAR(after.delta(f.armUp), degToRad(-30), 1e-5);
    // single key holds forever
    PoseTrack single;
    single.setKey(4, b);
    CHECK(single.sample(0).rootPos == b.rootPos);
    CHECK(single.sample(100).rootPos == b.rootPos);
    // shortest-path angle interpolation across ±PI
    PoseTrack wrap;
    Pose wa, wb;
    wa.rootAngle = degToRad(170);
    wb.rootAngle = degToRad(-170);
    wrap.setKey(0, wa);
    wrap.setKey(10, wb);
    float midAngle = wrap.sample(5).rootAngle;
    // must go through ±180, not sweep the long way through 0
    CHECK(std::fabs(radToDeg(normalizeAngle(midAngle))) > 175.0f);
    // key removal + move
    track.removeKey(0);
    CHECK(track.keyCount() == 1);
    track.moveKey(10, 20);
    CHECK(track.hasKey(20) && !track.hasKey(10));
}

} // namespace

void runRigTests() {
    testFKRestPose();
    testTwoBoneReach();
    testJointLimitsHold();
    testBendSidePreserved();
    testAntiFlingSweep();
    testAntiFlingConvergence();
    testFABRIKChainStable();
    testTweening();
}
