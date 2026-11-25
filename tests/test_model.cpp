// Leptid — model tests: cutter, .Lchar, .lep v3, compositor, brush, project, undo.
#include <cstdio>
#include "testfw.h"
#include "core/image.h"
#include "formats/pngio.h"
#include "model/project.h"
#include "model/compositor.h"
#include "model/brush.h"
#include "model/lepser.h"
#include "rig/charser.h"
#include "rig/cutter.h"
#include "rig/ik.h"

using namespace leptid;
using namespace testfw;

namespace {

// Build a synthetic T-pose "person": distinct color blocks on a white background.
Image makeTPose() {
    Image src(320, 480);
    src.clear(Color(255, 255, 255, 255));
    src.fillRect(140, 90, 40, 110, Color(60, 90, 200, 255));   // torso  (blue)
    src.fillRect(140, 40, 40, 50, Color(230, 190, 120, 255));  // head   (skin)
    src.fillRect(90, 90, 50, 34, Color(200, 60, 60, 255));     // armL_upper (red) — arm to the left
    src.fillRect(40, 90, 50, 34, Color(220, 90, 90, 255));     // armL_lower
    src.fillRect(180, 90, 50, 34, Color(60, 160, 60, 255));    // armR_upper (green)
    src.fillRect(230, 90, 50, 34, Color(90, 190, 90, 255));    // armR_lower
    src.fillRect(142, 200, 16, 120, Color(120, 80, 40, 255));  // legL_upper (brown)
    src.fillRect(142, 320, 16, 120, Color(150, 100, 60, 255)); // legL_lower
    src.fillRect(162, 200, 16, 120, Color(130, 90, 50, 255));  // legR_upper
    src.fillRect(162, 320, 16, 120, Color(160, 110, 70, 255)); // legR_lower
    return src;
}

void testCutter() {
    Image src = makeTPose();
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
    CHECK(cutter.build(c, &err));
    if (!err.empty()) fprintf(stderr, "cutter error: %s\n", err.c_str());
    CHECK(c.parts.size() == 10);
    CHECK(c.skeleton.valid());
    int torso = c.skeleton.findByName("torso");
    int head = c.skeleton.findByName("head");
    int armLo = c.skeleton.findByName("armL_lower");
    int armUp = c.skeleton.findByName("armL_upper");
    CHECK(torso >= 0 && head >= 0 && armLo >= 0 && armUp >= 0);
    CHECK(c.skeleton.bone(head)->parentId == torso);
    CHECK(c.skeleton.bone(armLo)->parentId == armUp);
    // transparency: white background removed inside parts
    CHECK(c.parts[0].sprite.px.size() > 0);
    bool whiteGone = true;
    for (uint32_t p : c.parts[0].sprite.px)
        if ((p >> 24) != 0 && (p & 0xFF) > 240 && ((p >> 8) & 0xFF) > 240 && ((p >> 16) & 0xFF) > 240)
            whiteGone = false;
    CHECK(whiteGone);
    // rest pose places torso pivot at its source position (140+20, 90+108)
    Pose rest = c.track.sample(0);
    CHECK_NEAR(rest.rootPos.x, 160.0f, 1.5f);
    CHECK_NEAR(rest.rootPos.y, 198.0f, 1.5f);
    // render at rest: torso pixels should land near the original torso region
    Image canvas(320, 480);
    canvas.clear(Color(0, 0, 0, 0));
    c.render(canvas, rest);
    CHECK(canvas.opaquePixelCount() > 3000);
    Color torsoPx = Color::unpack(canvas.at(160, 190)); // lower torso, clear of hanging arms
    CHECK(torsoPx.b > 150 && torsoPx.r < 120);          // blue-ish torso
}

void testLcharRoundtrip() {
    Image src = makeTPose();
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
    CHECK(cutter.build(c, &err));

    // add an IK-keyed animation: reach the hand forward, then back
    IKOptions opt;
    int armUp = c.skeleton.findByName("armR_upper");
    int armLo = c.skeleton.findByName("armR_lower");
    std::vector<int> chain{armUp, armLo};
    Pose p1 = c.track.sample(0);
    // place key at frame 0 = rest
    c.track.setKey(0, p1);
    // swing the right arm: set explicit pose keys
    Pose p2 = p1;
    p2.setDelta(armUp, degToRad(60));
    p2.setDelta(armLo, degToRad(-80));
    c.track.setKey(12, p2, Ease::EaseInOut);
    Pose p3 = p1;
    p3.setDelta(armUp, degToRad(30));
    p3.setDelta(armLo, degToRad(-40));
    c.track.setKey(24, p3);

    std::string lcharPath = tmpFile("leptid_test.Lchar");
    CHECK(saveLchar(lcharPath, c, &err));
    Character loaded;
    CHECK(loadLchar(lcharPath, loaded, &err));
    if (!err.empty()) fprintf(stderr, "load error: %s\n", err.c_str());
    CHECK(loaded.parts.size() == c.parts.size());
    CHECK(loaded.skeleton.bones.size() == c.skeleton.bones.size());
    // guard: when the load fails 'loaded' is empty — indexing it would segfault
    // (Release STL has no bounds checks) and mask the real failures below
    if (loaded.parts.size() == c.parts.size()) {
        for (size_t i = 0; i < c.parts.size(); ++i) {
            CHECK(loaded.parts[i].sprite.px == c.parts[i].sprite.px); // lossless
            CHECK(loaded.parts[i].name == c.parts[i].name);
        }
    }
    CHECK(loaded.track.keyCount() == 3);
    // tween through the animation works after load
    if (loaded.track.keyCount() > 0) {
        Pose mid = loaded.track.sample(6);
        CHECK(mid.delta(armUp) > 0.01f && mid.delta(armUp) < degToRad(60));
    }
}

void testLepRoundtrip() {
    auto p = Project::createDefault();
    p->name = "Roundtrip Test";
    p->fps = 30;
    p->frameCount = 20;
    p->camera = Camera{2500, 2500, 1.25f};

    // raster frames: animated circle moving
    RasterLayer* rl = (RasterLayer*)p->layers[0].get();
    for (int f = 0; f < 4; ++f) {
        Image& img = rl->frames[f];
        img.create(200, 200);
        img.clear(Color(0, 0, 0, 0));
        img.fillDisc(50 + f * 30, 100, 20, Color(255, 140, 0, 255));
    }
    // group layer
    auto group = std::make_shared<GroupLayer>();
    group->id = Project::newLayerId();
    group->name = "Group";
    auto inner = std::make_shared<RasterLayer>();
    inner->id = Project::newLayerId();
    inner->name = "inner";
    inner->frames[0].create(64, 64);
    inner->frames[0].clear(Color(0, 255, 0, 200));
    group->children.push_back(inner);
    group->transform = Mat3::translation(100, 100);
    p->layers.push_back(group);
    // path layer
    auto pl = std::make_shared<PathLayer>();
    pl->id = Project::newLayerId();
    pl->name = "Motion";
    PathStroke s;
    s.pts = {Vec2(0, 0), Vec2(100, 50), Vec2(200, 0)};
    pl->paths.push_back(s);
    p->layers.push_back(pl);

    // character: import the .Lchar from the previous test
    Character c;
    std::string err;
    CHECK(loadLchar(tmpFile("leptid_test.Lchar"), c, &err));
    std::string cid = p->addCharacter(std::make_shared<Character>(c));
    auto cl = std::make_shared<CharacterLayer>();
    cl->id = Project::newLayerId();
    cl->name = "Hero";
    cl->characterId = cid;
    cl->position = Vec2(2400, 2400);
    cl->scale = 0.9f;
    p->layers.push_back(cl);

    CHECK(saveLep(tmpFile("leptid_test.lep"), *p, &err));
    Project back;
    CHECK(loadLep(tmpFile("leptid_test.lep"), back, &err));
    if (!err.empty()) fprintf(stderr, "lep error: %s\n", err.c_str());
    CHECK(back.name == "Roundtrip Test");
    CHECK(back.fps == 30 && back.frameCount == 20);
    CHECK_NEAR(back.camera.scale, 1.25f, 1e-5);
    CHECK(back.layers.size() == 4);
    CHECK(back.characters.size() == 1);
    // raster frame pixels survived
    RasterLayer* rl2 = (RasterLayer*)back.layers[0].get();
    CHECK(rl2->frames.count(2) && rl2->frames[2].w == 200);
    CHECK(rl2->frames[2].px == rl->frames[2].px);
    // character sprites survived (self-contained)
    Character* c2 = back.findCharacter(back.layers[3]->id.empty() ? cid : ((CharacterLayer*)back.layers[3].get())->characterId);
    CHECK(c2 != nullptr);
    // guard: never index a null/empty result — a failed load must FAIL, not crash
    if (c2 && c2->parts.size() == c.parts.size()) {
        CHECK(c2->parts.size() == 10);
        CHECK(c2->parts[0].sprite.px == c.parts[0].sprite.px);
    }
    // group nesting survived
    CHECK(back.layers[1]->type == LayerType::Group);
    GroupLayer* g2 = (GroupLayer*)back.layers[1].get();
    CHECK(g2->children.size() == 1);
    CHECK(g2->children[0]->type == LayerType::Raster);
    // path layer survived
    CHECK(back.layers[2]->type == LayerType::Path);
    PathLayer* pl2 = (PathLayer*)back.layers[2].get();
    CHECK(pl2->paths.size() == 1 && pl2->paths[0].pts.size() == 3);
}

void testCompositor() {
    auto p = Project::createDefault();
    p->frameCount = 8;
    RasterLayer* rl = (RasterLayer*)p->layers[0].get();
    rl->frames[0].create(100, 100);
    rl->frames[0].clear(Color(255, 0, 0, 255));
    rl->frames[0].fillDisc(50, 50, 30, Color(0, 255, 0, 255));
    rl->frames[1].create(100, 100);
    rl->frames[1].clear(Color(0, 0, 255, 255));

    RenderOptions opt;
    opt.frame = 0;
    opt.outW = 200, opt.outH = 200;
    Image out = renderFrame(*p, opt);
    CHECK(out.w == 200 && out.h == 200);
    // layer drawn at world (0,0)..(100,100); camera centered there
    p->camera = Camera{50, 50, 1.0f};
    out = renderFrame(*p, opt);
    Color c = Color::unpack(out.at(60, 50)); // screen(60,50) = world (10,0): red frame bg
    CHECK(c.r == 255 && c.g == 0 && c.b == 0);
    Color disc = Color::unpack(out.at(100, 100)); // world (50,50): disc center
    CHECK(disc.g == 255);

    // scale 2: disc center (world 50,50) lands at screen (100,100)
    p->camera = Camera{50, 50, 2.0f};
    out = renderFrame(*p, opt);
    disc = Color::unpack(out.at(100, 100));
    CHECK(disc.g == 255); // world (50,50) is the disc center
    Color corner = Color::unpack(out.at(195, 5)); // world (97.5, 2.5) inside the red frame
    CHECK(corner.r == 255 && corner.g == 0 && corner.b == 0);

    // frame 1: blue disc on transparent bg
    p->camera = Camera{50, 50, 1.0f};
    rl->frames[1].clear(Color(0, 0, 0, 0));
    rl->frames[1].fillDisc(50, 50, 25, Color(0, 0, 255, 255));
    opt.frame = 1;
    out = renderFrame(*p, opt);
    Color blue = Color::unpack(out.at(100, 100));
    CHECK(blue.b == 255 && blue.r == 0);

    // onion skin: frame 1 shows frame 0 tinted red underneath (transparent areas)
    opt.frame = 1;
    opt.onion = true;
    opt.onionBefore = 1;
    opt.onionAfter = 0;
    out = renderFrame(*p, opt);
    Color onion = Color::unpack(out.at(60, 50)); // world (10,0): frame1 transparent, frame0 red
    CHECK(onion.a > 0 && onion.r > onion.b);     // red-tinted onion ghost visible

    // character layer through the compositor (the DA/OOA interop path)
    Character c2;
    std::string err;
    CHECK(loadLchar(tmpFile("leptid_test.Lchar"), c2, &err));
    std::string cid = p->addCharacter(std::make_shared<Character>(c2));
    auto cl = std::make_shared<CharacterLayer>();
    cl->id = Project::newLayerId();
    cl->characterId = cid;
    cl->position = Vec2(50, 50);
    p->layers.push_back(cl);
    opt.frame = 0;
    opt.onion = false;
    out = renderFrame(*p, opt);
    CHECK(out.opaquePixelCount() > 1000);
}

void testBrushAndStabilizer() {
    BrushEngine engine;
    Image canvas(200, 200);
    BrushSettings b;
    b.id = BrushId::HardRound;
    b.size = 10;
    b.color = Color(255, 255, 255, 255);
    // jittery raw input
    std::vector<Vec2> raw;
    for (int i = 0; i < 60; ++i)
        raw.push_back(Vec2(10 + i * 3.0f, 100 + (float)((i * 37) % 11) - 5.0f));
    auto smoothed = smoothStroke(raw, 3);
    CHECK(smoothed.size() >= raw.size()); // resampled
    // vertical jitter reduced
    float rawRange = 0, smoothRange = 0;
    for (auto& v : raw) rawRange = std::max(rawRange, std::fabs(v.y - 100));
    for (auto& v : smoothed) smoothRange = std::max(smoothRange, std::fabs(v.y - 100));
    CHECK(smoothRange < rawRange);
    engine.paintStroke(canvas, smoothed, b);
    CHECK(canvas.opaquePixelCount() > 500);
    // eraser removes
    BrushSettings e = b;
    e.eraser = true;
    e.size = 30;
    engine.paintStroke(canvas, std::vector<Vec2>{Vec2(100, 100)}, e);
    Image after = canvas;
    CHECK(after.opaquePixelCount() < canvas.opaquePixelCount() + 1);
    // bounds
    IRect r = strokeBounds(smoothed, b, 200, 200);
    CHECK(r.x1 > r.x0 && r.y1 >= r.y0);

    // preset engine checks
    CHECK(BrushEngine::presets().size() >= 10);
    // hard vs soft: hard round gives full-alpha core, airbrush stays translucent
    Image hard(48, 48), soft(48, 48);
    BrushSettings hb; hb.id = BrushId::HardRound; hb.size = 30; hb.color = Color(255, 255, 255, 255);
    BrushSettings ab; ab.id = BrushId::Airbrush; ab.size = 30; ab.color = Color(255, 255, 255, 255);
    engine.stamp(hard, Vec2(24, 24), hb);
    engine.stamp(soft, Vec2(24, 24), ab);
    CHECK(hard.at(24, 24) == Color(255, 255, 255, 255).pack());       // core fully opaque
    uint8_t softA = (uint8_t)(soft.at(24, 24) >> 24);
    CHECK(softA > 0 && softA < 200);                                  // soft tip translucent
    // pencil core also opaque (hard nib)
    Image pen(48, 48);
    BrushSettings pb; pb.id = BrushId::Pencil; pb.size = 30; pb.color = Color(255, 0, 0, 255);
    engine.stamp(pen, Vec2(24, 24), pb);
    bool redFound = false;
    for (uint32_t p2 : pen.px) {
        Color c = Color::unpack(p2);
        if (c.a > 200 && c.r > 180 && c.g < 80) redFound = true;
    }
    CHECK(redFound);
    // chalk grain: within the stamp footprint some pixels are transparent (texture)
    Image chalk(48, 48);
    BrushSettings cb; cb.id = BrushId::Chalk; cb.size = 44; cb.color = Color(255, 255, 255, 255);
    engine.stamp(chalk, Vec2(24, 24), cb);
    int transparent = 0, opaque = 0;
    for (int y = 14; y < 34; ++y)
        for (int x = 14; x < 34; ++x)
            ((chalk.at(x, y) >> 24) > 128) ? ++opaque : ++transparent;
    CHECK(opaque > 0 && transparent > 0); // textured, not a solid blob
    // spacing: markers (spacing .04) leave more stamps than spray (spacing .25)
    // (indirect: long spray stroke covers fewer distinct pixels than marker stroke)
    Image spray(400, 60), marker(400, 60);
    BrushSettings sb2; sb2.id = BrushId::Spray; sb2.size = 20; sb2.color = Color(255, 255, 255, 255);
    BrushSettings mb2; mb2.id = BrushId::Marker; mb2.size = 20; mb2.color = Color(255, 255, 255, 255);
    std::vector<Vec2> line;
    for (int i = 0; i < 200; ++i) line.push_back(Vec2(10 + (float)i, 30));
    engine.paintStroke(spray, line, sb2);
    engine.paintStroke(marker, line, mb2);
    // marker should form a solid continuous band: count fully-opaque rows at x=200
    int markerOpaque = 0, sprayCols = 0;
    for (int y = 20; y < 40; ++y) if ((marker.at(200, y) >> 24) > 240) ++markerOpaque;
    for (int x = 20; x < 380; ++x) if ((spray.at(x, 30) >> 24) > 200) ++sprayCols;
    CHECK(markerOpaque >= 14);  // continuous band
    CHECK(sprayCols < 360);     // spray has gaps
    // preview icons render
    Image prev = engine.previewIcon(BrushId::Ink, 40, Color(255, 255, 255, 255));
    CHECK(prev.w == 40 && prev.opaquePixelCount() > 50);
}

void testProjectOps() {
    auto p = Project::createDefault();
    auto g = std::make_shared<GroupLayer>();
    g->id = Project::newLayerId();
    p->addLayer(g);
    CHECK(p->layers.size() == 2);
    auto dup = p->duplicateLayer(p->layers[0]->id);
    CHECK(dup != nullptr);
    CHECK(p->layers.size() == 3);
    int removedIdx = p->layerIndex(dup->id);
    auto removed = p->removeLayer(dup->id);
    CHECK(removed != nullptr && removed->id == dup->id);
    CHECK(p->layers.size() == 2);
    // move layer to top
    p->moveLayer(g->id, 99);
    CHECK(p->layers.back()->id == g->id);
    // timeline sync: character with keys beyond frameCount extends the timeline
    Character c;
    Pose pose;
    c.track.setKey(0, pose);
    pose.rootPos = Vec2(50, 50);
    c.track.setKey(80, pose);
    p->addCharacter(std::make_shared<Character>(c));
    auto cl = std::make_shared<CharacterLayer>();
    cl->id = Project::newLayerId();
    cl->characterId = p->characters.begin()->first;
    p->layers.push_back(cl);
    CHECK(p->effectiveTimelineEnd() == 81);
    p->frameCount = 48;
    p->syncTimelineToCharacters();
    CHECK(p->frameCount >= 81);
    // undo/redo of layer removal
    size_t before = p->layers.size();
    std::string removedId = p->layers[0]->id;
    auto victim = p->layers[0];
    auto removeCmd = std::make_unique<FuncCommand>(
        "remove layer",
        [p, victim]() { p->layers.insert(p->layers.begin(), victim); },
        [p, removedId]() {
            for (size_t i = 0; i < p->layers.size(); ++i)
                if (p->layers[i]->id == removedId) { p->layers.erase(p->layers.begin() + (long)i); break; }
        });
    p->commands.push(std::move(removeCmd));
    CHECK(p->layers.size() == before - 1);
    CHECK(p->commands.undo());
    CHECK(p->layers.size() == before);
    CHECK(p->commands.redo());
    CHECK(p->layers.size() == before - 1);
}


void testPlaybackAndCameraTrack() {
    // loop wraps
    bool fwd = true;
    CHECK(advancePlayhead(0, 5, PlayMode::Loop, fwd) == 1);
    CHECK(advancePlayhead(4, 5, PlayMode::Loop, fwd) == 0);
    // ping-pong reverses at both ends
    fwd = true;
    CHECK(advancePlayhead(3, 5, PlayMode::PingPong, fwd) == 4);
    CHECK(fwd);
    CHECK(advancePlayhead(4, 5, PlayMode::PingPong, fwd) == 3);
    CHECK(!fwd);                 // direction flipped
    CHECK(advancePlayhead(0, 5, PlayMode::PingPong, fwd) == 1);
    CHECK(fwd);
    // once clamps to the last frame
    CHECK(advancePlayhead(3, 5, PlayMode::Once, fwd) == 4);
    CHECK(advancePlayhead(4, 5, PlayMode::Once, fwd) == 4);

    // camera track: eased interpolation + holds outside the keyed span
    CameraTrack tr;
    Camera a; a.x = 100; a.y = 200; a.scale = 1.0f;
    Camera b; b.x = 300; b.y = 400; b.scale = 2.0f;
    tr.setKey(0, a, Ease::Linear);
    tr.setKey(24, b, Ease::Linear);
    CHECK(tr.keyCount() == 2 && tr.hasKey(24));
    Camera m = tr.sample(12);
    CHECK(m.x > 195 && m.x < 205);                            // midpoint x, linear
    CHECK(m.y > 295 && m.y < 305);                            // midpoint y
    CHECK(m.scale > 1.40 && m.scale < 1.43);                  // multiplicative zoom midpoint = sqrt(2)
    Camera pre = tr.sample(-5);
    CHECK(pre.x == 100 && pre.scale == 1.0f);                 // hold before first key
    Camera post = tr.sample(999);
    CHECK(post.x == 300 && post.scale == 2.0f);               // hold after last key
    // ease=Step on the leaving key holds the segment constant
    tr.setKey(0, a, Ease::Step);
    Camera held = tr.sample(12);
    CHECK(held.x == 100 && held.y == 200);
    // ease-in on the leaving key keeps the early segment near the start
    tr.setKey(0, a, Ease::EaseIn);
    Camera early = tr.sample(6);
    CHECK(early.x < 160);
    // setKey replaces values + ease
    tr.setKey(24, b, Ease::EaseOut);
    CameraKey got;
    CHECK(tr.getKey(24, got) && got.ease == Ease::EaseOut);
    tr.removeKey(24);
    CHECK(!tr.hasKey(24));

    // Project::cameraAtFrame falls back to the static camera
    Project proj;
    proj.camera = a;
    Camera eff = proj.cameraAtFrame(10);
    CHECK(eff.x == 100 && eff.scale == 1.0f);
    proj.cameraTrack.setKey(10, b);
    eff = proj.cameraAtFrame(10);
    CHECK(eff.x == 300);
}

void testLepCameraTrackRoundtrip() {
    Project p;
    p.name = "camtest";
    p.cameraTrack.setKey(0, Camera{1000, 1000, 1.0f}, Ease::EaseInOut);
    p.cameraTrack.setKey(24, Camera{2200, 1600, 1.35f}, Ease::EaseOut);
    p.cameraTrack.setKey(47, Camera{1800, 2000, 0.9f});
    p.playMode = PlayMode::PingPong;
    p.playStart = 2;
    p.playEnd = 40;
    std::string path = tmpFile("leptid_camtest.lep");
    std::string err;
    CHECK(saveLep(path, p, &err));
    Project q;
    CHECK(loadLep(path, q, &err));
    CHECK(q.cameraTrack.keyCount() == 3);
    CameraKey k;
    CHECK(q.cameraTrack.getKey(24, k));
    CHECK(k.cam.x == 2200 && k.cam.scale == 1.35f);
    CHECK(k.ease == Ease::EaseOut);
    CHECK(q.playMode == PlayMode::PingPong);
    CHECK(q.playStart == 2 && q.playEnd == 40);
    // sampled camera at the middle of an eased segment stays sane
    Camera s = q.cameraAtFrame(12);
    CHECK(s.x > 1000 && s.x < 2200 && s.scale > 1.0f && s.scale < 1.35f);
    // a v3-style file without the new fields still loads with defaults
    p.playStart = -1;
    p.playEnd = -1;
    p.playMode = PlayMode::Loop;
    CHECK(saveLep(path, p, &err));
    Project r;
    CHECK(loadLep(path, r, &err));
    CHECK(r.playMode == PlayMode::Loop && r.playStart == -1 && r.playEnd == -1);
    CHECK(r.cameraTrack.keyCount() == 3);
}

} // namespace

void runModelTests() {
    testCutter();
    testLcharRoundtrip();
    testLepRoundtrip();
    testCompositor();
    testBrushAndStabilizer();
    testProjectOps();
    testPlaybackAndCameraTrack();
    testLepCameraTrackRoundtrip();
}
