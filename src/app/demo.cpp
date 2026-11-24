// Leptid — demo project generator: builds a small animation exercising the
// engine end to end (raster background, frame-by-frame ball, rigged character
// with keyed IK poses, motion path layer). Written by `leptid --demo`.
#include "export/exporter.h"
#include "formats/pngio.h"
#include "model/compositor.h"
#include "model/lepser.h"
#include "rig/cutter.h"
#include "rig/ik.h"
#include <cmath>
#include <cstdio>

namespace leptid {

static Image makeBackground() {
    Image bg(1200, 900);
    // sky gradient
    for (int y = 0; y < 620; ++y) {
        float t = (float)y / 620.0f;
        uint8_t r = (uint8_t)lerpf(26, 66, t), g = (uint8_t)lerpf(30, 110, t), b = (uint8_t)lerpf(44, 160, t);
        for (int x = 0; x < 1200; ++x) bg.at(x, y) = Color(r, g, b, 255).pack();
    }
    // ground
    for (int y = 620; y < 900; ++y) {
        float t = (float)(y - 620) / 280.0f;
        uint8_t g = (uint8_t)lerpf(60, 38, t);
        for (int x = 0; x < 1200; ++x) bg.at(x, y) = Color(g, (uint8_t)(g + 14), 34, 255).pack();
    }
    bg.fillDisc(980, 130, 60, Color(250, 240, 170, 255)); // sun
    // a couple of hills
    for (int x = 0; x < 1200; ++x) {
        float h1 = 60 + 38 * std::sin(x * 0.004f + 1.2f);
        float h2 = 34 + 26 * std::sin(x * 0.006f + 3.6f);
        for (int y = (int)(620 - h1); y < 620; ++y) bg.at(x, y) = Color(38, 92, 52, 255).pack();
        for (int y = (int)(620 - h2); y < 620 - (int)h2 + 8 && y < 620; ++y) bg.at(x, y) = Color(46, 104, 58, 255).pack();
    }
    return bg;
}

static Character makeDemoCharacter() {
    // synthetic T-pose
    Image src(320, 480);
    src.clear(Color(255, 255, 255, 255));
    src.fillRect(140, 90, 40, 110, Color(60, 90, 200, 255));   // torso
    src.fillRect(140, 40, 40, 50, Color(230, 190, 120, 255));  // head
    src.fillRect(90, 90, 50, 34, Color(200, 60, 60, 255));     // armL_upper
    src.fillRect(40, 90, 50, 34, Color(220, 90, 90, 255));     // armL_lower
    src.fillRect(180, 90, 50, 34, Color(60, 160, 60, 255));    // armR_upper
    src.fillRect(230, 90, 50, 34, Color(90, 190, 90, 255));    // armR_lower
    src.fillRect(142, 200, 16, 120, Color(120, 80, 40, 255));  // legL_upper
    src.fillRect(142, 320, 16, 120, Color(150, 100, 60, 255)); // legL_lower
    src.fillRect(162, 200, 16, 120, Color(130, 90, 50, 255));  // legR_upper
    src.fillRect(162, 320, 16, 120, Color(160, 110, 70, 255)); // legR_lower

    CharacterCutter cutter;
    cutter.setSource(src);
    cutter.setKeyColor(Color(255, 255, 255, 255), 12);
    cutter.addCut({"torso", 140, 90, 40, 110});
    cutter.addCut({"head", 140, 40, 40, 50});
    cutter.addCut({"armL_upper", 90, 90, 50, 34});
    cutter.addCut({"armL_lower", 40, 90, 50, 34});
    cutter.addCut({"armR_upper", 180, 90, 50, 34});
    cutter.addCut({"armR_lower", 230, 90, 50, 34});
    cutter.addCut({"legL_upper", 142, 200, 16, 120});
    cutter.addCut({"legL_lower", 142, 320, 16, 120});
    cutter.addCut({"legR_upper", 162, 200, 16, 120});
    cutter.addCut({"legR_lower", 162, 320, 16, 120});
    Character c;
    std::string err;
    if (!cutter.build(c, &err)) {
        fprintf(stderr, "demo: cutter failed: %s\n", err.c_str());
        return c;
    }
    c.name = "Demo Hero";

    // IK-keyed wave: right hand reaches up over time, arm glides (stable IK).
    IKOptions opt;
    int armR_up = c.skeleton.findByName("armR_upper");
    int armR_lo = c.skeleton.findByName("armR_lower");
    std::vector<int> chain{armR_up, armR_lo};
    Pose rest = c.track.sample(0);
    c.track.setKey(0, rest);
    // Use FK deltas for the wave (predictable keyframes for the demo).
    Pose up = rest;
    up.setDelta(armR_up, degToRad(95));   // raise the arm
    up.setDelta(armR_lo, degToRad(-30));  // slight elbow bend
    c.track.setKey(14, up, Ease::EaseInOut);
    Pose down = rest;
    down.setDelta(armR_up, degToRad(80));
    down.setDelta(armR_lo, degToRad(-5));
    c.track.setKey(22, down, Ease::EaseInOut);
    Pose up2 = up;
    up2.setDelta(armR_lo, degToRad(-45));
    c.track.setKey(30, up2, Ease::EaseInOut);
    Pose rest2 = rest;
    c.track.setKey(40, rest2, Ease::EaseInOut);
    // head tilt for character
    Pose tilt = rest2;
    int head = c.skeleton.findByName("head");
    tilt.setDelta(head, degToRad(10));
    c.track.setKey(47, tilt, Ease::EaseInOut);
    return c;
}

std::shared_ptr<Project> buildDemoProject() {
    auto p = Project::createDefault();
    p->name = "Hello Leptid";
    p->fps = 24;
    p->frameCount = 48;

    // animated camera (Synfig-style): slow push-in toward the hero, then settle.
    // The camera track rides on the same ease system as pose keys. Keys frame
    // the artwork (world 0..1200 x 0..900), not the raw world default.
    {
        Camera cam0{600, 450, 0.62f};                  // wide establishing shot
        Camera camMid = cam0;
        camMid.x += 40.0f;
        camMid.y -= 20.0f;
        camMid.scale = 0.74f;                          // pushed in mid-shot
        Camera camEnd = cam0;
        camEnd.scale = 0.66f;                          // slight settle-out
        p->cameraTrack.setKey(0, cam0, Ease::EaseInOut);
        p->cameraTrack.setKey(24, camMid, Ease::EaseInOut);
        p->cameraTrack.setKey(47, camEnd, Ease::EaseInOut);
    }

    // 1) hand-drawn background (one drawing, held across the timeline)
    auto bg = std::make_shared<RasterLayer>();
    bg->id = Project::newLayerId();
    bg->name = "Background";
    Image bgImg = makeBackground();
    for (int f = 0; f < p->frameCount; ++f) bg->frames[f] = bgImg;
    p->layers.push_back(bg);

    // 2) frame-by-frame bouncing ball (DA)
    auto ball = std::make_shared<RasterLayer>();
    ball->id = Project::newLayerId();
    ball->name = "Ball (frame-by-frame)";
    for (int f = 0; f < p->frameCount; ++f) {
        float t = (float)f / (p->frameCount - 1);
        float x = 150 + 900 * t;
        float y = 500 - 260 * std::fabs(std::sin(t * 3.14159f * 2.0f));
        float squash = 1.0f + 0.25f * std::cos(t * 3.14159f * 8.0f);
        Image& img = ball->frames[f];
        img.create(1200, 900);
        img.clear(Color(0, 0, 0, 0));
        img.fillDisc(x, y, 34, Color(240, 120, 40, 255));
        // shadow
        float sy = 640;
        float shrink = clampf(1.0f - (640.0f - y) / 400.0f, 0.35f, 1.0f);
        img.fillDisc(x, sy, 26 * shrink, Color(0, 0, 0, 70));
        (void)squash;
    }
    p->layers.push_back(ball);

    // 3) rigged character layer (OOA in DA), waving
    Character hero = makeDemoCharacter();
    std::string cid = p->addCharacter(std::make_shared<Character>(hero));
    auto cl = std::make_shared<CharacterLayer>();
    cl->id = Project::newLayerId();
    cl->name = "Hero (rigged)";
    cl->characterId = cid;
    cl->position = Vec2(430, 520);
    cl->scale = 0.55f;
    p->layers.push_back(cl);

    // 4) motion path overlay
    auto path = std::make_shared<PathLayer>();
    path->id = Project::newLayerId();
    path->name = "Ball motion path";
    PathStroke s;
    s.color = Color(255, 220, 120, 140);
    s.width = 3.0f;
    for (int i = 0; i <= 60; ++i) {
        float t = (float)i / 60.0f;
        float x = 150 + 900 * t;
        float y = 500 - 260 * std::fabs(std::sin(t * 3.14159f * 2.0f));
        s.pts.push_back(Vec2(x, y));
    }
    path->paths.push_back(s);
    p->layers.push_back(path);

    p->activeLayerId = ball->id;
    // camera frames the action (world 4000x4000; content near top-left)
    p->camera = Camera{600, 450, 1.0f};
    return p;
}

bool runDemo(const std::string& lepPath, const std::string& gifPath,
             const std::string& mp4Path, const std::string& pngDir) {
    auto p = buildDemoProject();
    std::string err;
    if (!saveLep(lepPath, *p, &err)) {
        fprintf(stderr, "demo: save failed: %s\n", err.c_str());
        return false;
    }
    ExportOptions opt;
    opt.width = 800;
    opt.height = 600;
    if (!pngDir.empty() && !exportPngSequence(*p, pngDir, opt, &err)) {
        fprintf(stderr, "demo: png export failed: %s\n", err.c_str());
        return false;
    }
    if (!gifPath.empty() && !exportGif(*p, gifPath, opt, &err)) {
        fprintf(stderr, "demo: gif export failed: %s\n", err.c_str());
        return false;
    }
    if (!mp4Path.empty() && !exportMp4(*p, mp4Path, opt, &err)) {
        fprintf(stderr, "demo: mp4 export skipped: %s\n", err.c_str());
    }
    printf("demo project written: %s (%d frames @ %d fps)\n", lepPath.c_str(), p->frameCount, p->fps);
    return true;
}

} // namespace leptid
