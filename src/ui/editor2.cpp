// Leptid — editor implementation (part 2: panels, timeline, dialogs, shortcuts).
#include "ui/editor.h"
#include "export/exporter.h"
#include "rig/ik.h"
#include "rig/charser.h"
#include "model/audio.h"
#include "ai/ollama.h"
#include <algorithm>
#include <filesystem>

namespace leptid {

extern const char* kVersion;

static std::vector<std::string> listDir(const std::string& path) {
    std::vector<std::string> out;
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(path, ec))
        out.push_back(e.path().filename().string());
    if (ec) return {};
    std::sort(out.begin(), out.end());
    return out;
}

static bool isDir(const std::string& p) {
    return std::filesystem::is_directory(p);
}

} // namespace leptid
#include <cmath>
#include <cstdio>

namespace leptid {

// ---------------- layers panel ----------------
void Editor::drawLayersPanel(Ui& ui, int x, int y, int w, int h) {
    const Theme& t = Theme::get();
    ui.panel(x, y, w, h, t.panelBg);
    ui.frame(x, y, w, h, t.border);
    ui.label("Layers", x + kPad, y + kPad, t.text, true);

    // add-buttons: equal widths, aligned row
    const int bh = 24, by = y + kPad;
    int bx = x + 64;
    const int bw = (w - 64 - kPad * 2 - 12) / 3;
    if (ui.button(wid(), "+Raster", bx, by, bw, bh)) {
        auto rl = std::make_shared<RasterLayer>();
        rl->id = Project::newLayerId();
        rl->name = "Layer " + std::to_string(project_->layers.size() + 1);
        rl->frames[0].create(1200, 900);
        project_->addLayer(rl);
        std::string newId = rl->id;
        project_->commands.pushExecuted(std::make_unique<FuncCommand>(
            "add layer",
            [p = project_.get(), newId]() { auto removed = p->removeLayer(newId); (void)removed; },
            [p = project_.get(), rl, newId]() { p->addLayer(rl); p->activeLayerId = newId; }));
        setStatus("raster layer added");
    }
    bx += bw + 6;
    if (ui.button(wid(), "+Group", bx, by, bw, bh)) {
        auto g = std::make_shared<GroupLayer>();
        g->id = Project::newLayerId();
        g->name = "Group";
        project_->addLayer(g);
        setStatus("group layer added");
    }
    bx += bw + 6;
    if (ui.button(wid(), "+Path", bx, by, bw, bh)) {
        auto pl = std::make_shared<PathLayer>();
        pl->id = Project::newLayerId();
        pl->name = "Path " + std::to_string(project_->layers.size() + 1);
        project_->addLayer(pl);
        setStatus("path layer added");
    }

    // header for the columns
    int rowY = by + bh + kGap;
    ui.label("EYE  NAME", x + kPad, rowY + 4, t.textDim);
    centerLabel("OPACITY", x + w - 108, rowY + 4, 60, 12, t.textDim);
    centerLabel("TYPE", x + w - 44, rowY + 4, 30, 12, t.textDim);
    rowY += 18;

    // layer rows (top layer first)
    const int rowH = 32;
    for (int i = (int)project_->layers.size() - 1; i >= 0; --i) {
        auto& l = project_->layers[(size_t)i];
        int ry = rowY + ((int)project_->layers.size() - 1 - i) * rowH;
        if (ry + rowH > y + h - 34) break;
        bool active = l->id == project_->activeLayerId;
        if (active) ui.rect(x + 2, ry + 1, w - 4, rowH - 2, t.accentDim);
        // visibility eye (custom, aligned)
        bool toggled = false;
        drawEye(ui, x + kPad, ry + (rowH - 18) / 2, l->visible, wid() + (unsigned)(i * 10 + 1), &toggled);
        if (toggled) l->visible = !l->visible;
        ui.label(l->name.substr(0, 16), x + kPad + 24, ry + (rowH - 9) / 2, active ? t.text : t.textDim);
        // type chip (centered)
        const char* tag = l->type == LayerType::Raster ? "R" : l->type == LayerType::Group ? "G"
                        : l->type == LayerType::Character ? "C" : "P";
        centerLabel(tag, x + w - 44, ry + (rowH - 10) / 2, 30, 12, t.accent);
        // opacity: track-only slider, aligned right
        float newOp = ui.slider(wid() + (unsigned)(i * 10 + 2), "", x + w - 108,
                                ry + (rowH - 14) / 2, 56, l->opacity, 0, 1);
        if (std::fabs(newOp - l->opacity) > 0.01f) l->opacity = newOp;
        // select row (name area only, so eye/slider keep working)
        if (inRect(ui.mouse().x, ui.mouse().y, x + kPad + 24, ry, w - 140, rowH) && ui.leftDown() &&
            ui.pressed(wid() + (unsigned)(i * 10 + 3))) {
            project_->activeLayerId = l->id;
        }
    }

    // bottom action row: equal widths
    int by2 = y + h - 30;
    const int bw2 = (w - kPad * 2 - 18) / 5;
    int bx2 = x + kPad;
    if (ui.button(wid(), "Dup", bx2, by2, bw2, 22)) {
        auto dup = project_->duplicateLayer(project_->activeLayerId);
        if (dup) setStatus("duplicated " + dup->name);
    }
    bx2 += bw2 + 6;
    if (ui.button(wid(), "Del", bx2, by2, bw2, 22)) {
        std::string id = project_->activeLayerId;
        auto removed = project_->removeLayer(id);
        if (removed) {
            int idx = project_->layerIndex(id);
            project_->commands.push(std::make_unique<FuncCommand>(
                "delete layer",
                [p = project_.get(), removed, idx]() { p->addLayer(removed, idx < 0 ? -1 : idx); },
                [p = project_.get(), id]() { p->removeLayer(id); }));
            setStatus("layer removed");
        }
    }
    bx2 += bw2 + 6;
    if (ui.button(wid(), "Up", bx2, by2, bw2, 22)) {
        int idx = project_->layerIndex(project_->activeLayerId);
        if (idx >= 0 && idx + 1 < (int)project_->layers.size()) project_->moveLayer(project_->activeLayerId, idx + 1);
    }
    bx2 += bw2 + 6;
    if (ui.button(wid(), "Dn", bx2, by2, bw2, 22)) {
        int idx = project_->layerIndex(project_->activeLayerId);
        if (idx > 0) project_->moveLayer(project_->activeLayerId, idx - 1);
    }
    bx2 += bw2 + 6;
    if (ui.button(wid(), "Sync", bx2, by2, bw2, 22)) {
        project_->syncTimelineToCharacters();
        setStatus(("timeline = " + std::to_string(project_->frameCount) + " frames").c_str());
    }
}

// ---------------- rig panel ----------------
void Editor::drawRigPanel(Ui& ui, int x, int y, int w, int h) {
    const Theme& t = Theme::get();
    ui.panel(x, y, w, h, t.panelBg);
    ui.frame(x, y, w, h, t.border);
    CharacterLayer* cl = activeCharacterLayer();
    Character* c = activeCharacter();
    if (!cl || !c) {
        ui.label("Rig / Pose", x + 10, y + 7, t.text, true);
        ui.label("Select a character layer,", x + 10, y + 30, t.textDim);
        ui.label("or cut one from artwork:", x + 10, y + 46, t.textDim);
        int yy = y + 68;
        ui.label("Cut slot:", x + 10, yy, t.textDim);
        yy += 18;
        // slot selector (prev/next)
        if (ui.button(wid(), "<", x + 8, yy, 22, 22)) { cutSlotIdx_ = (cutSlotIdx_ + cutSlots_.size() - 1) % cutSlots_.size(); cutSlot_ = cutSlots_[(size_t)cutSlotIdx_]; }
        ui.label(cutSlot_, x + 36, yy + 4, t.ok);
        if (ui.button(wid(), ">", x + 128, yy, 22, 22)) { cutSlotIdx_ = (cutSlotIdx_ + 1) % cutSlots_.size(); cutSlot_ = cutSlots_[(size_t)cutSlotIdx_]; }
        yy += 28;
        if (ui.button(wid(), "Build Character", x + 8, yy, 130, 24, true)) buildCutCharacter();
        if (ui.button(wid(), "Clear cuts", x + 144, yy, 90, 24)) { cuts_.clear(); setStatus("cut rectangles cleared"); }
        yy += 32;
        ui.label("Tool P: drag rects over", x + 10, yy, t.textDim);
        ui.label("the T-pose artwork.", x + 10, yy + 16, t.textDim);
        return;
    }

    ui.label(("Rig: " + c->name).substr(0, 24), x + 10, y + 7, t.text, true);
    // bone list
    int listY = y + 30;
    std::vector<std::string> boneNames;
    for (const Bone& b : c->skeleton.bones) boneNames.push_back(b.name);
    int sel = selectedBone_ >= 0 && selectedBone_ < (int)boneNames.size() ? selectedBone_ : -1;
    int clicked = ui.listbox(wid(), x + 6, listY, w - 12, (int)std::min(boneNames.size(), (size_t)6) * 22, boneNames, sel, &rigScroll_);
    if (clicked >= 0) selectedBone_ = clicked;

    int yy = listY + (int)std::min(boneNames.size(), (size_t)6) * 22 + 6;
    ui.label("Mode:", x + 10, yy, t.textDim);
    if (ui.button(wid(), "FK (K)", x + 52, yy - 4, 62, 22, tool_ == Tool::Bone)) tool_ = Tool::Bone;
    if (ui.button(wid(), "IK (I)", x + 118, yy - 4, 62, 22, tool_ == Tool::IK)) tool_ = Tool::IK;
    yy += 28;
    // key pose button
    if (ui.button(wid(), "Key Pose", x + 8, yy, 80, 22, true)) {
        Character* cc = c;
        cc->track.setKey(frame_, cc->track.sample(frame_));
        setStatus(("pose keyed @ " + std::to_string(frame_)).c_str());
    }
    if (ui.button(wid(), "Del Key", x + 92, yy, 80, 22)) {
        if (c->track.hasKey(frame_)) {
            PoseKey removed;
            c->track.getKey(frame_, removed);
            Character* cc = c;
            project_->commands.push(std::make_unique<FuncCommand>(
                "delete key",
                [cc, removedPose = removed.pose, f = frame_]() { cc->track.setKey(f, removedPose); },
                [cc, f = frame_]() { cc->track.removeKey(f); }));
            c->track.removeKey(frame_);
        }
    }
    // tween ease for next key segment
    yy += 28;
    ui.label("Keys: " + std::to_string(c->track.keyCount()), x + 10, yy, t.textDim);
    if (ui.button(wid(), "AI Assist", x + 92, yy - 4, 120, 22)) openDialog(Dialog::AI);
    if (ui.button(wid(), "IK wave demo", x + 92, yy - 4, 120, 22)) {
        // quick IK demo: reach along an arc with the stable solver
        int up = c->skeleton.findByName("armR_upper");
        int lo = c->skeleton.findByName("armR_lower");
        if (up >= 0 && lo >= 0) {
            std::vector<int> chain{up, lo};
            Pose base = c->track.sample(frame_);
            FKEvaluator fk(c->skeleton);
            auto bw = fk.evaluate(base);
            Vec2 hand = bw[(size_t)lo].tip;
            IKOptions opt;
            for (int i = 1; i <= 6; ++i) {
                Pose pose = base;
                float ang = -0.6f + 1.2f * (float)i / 6.0f;
                Vec2 target = hand + Vec2::fromAngle(ang) * 26.0f;
                solveChainIK(c->skeleton, pose, chain, target, opt);
                clampPoseToLimits(c->skeleton, pose);
                c->track.setKey(frame_ + i * 2, pose, Ease::EaseInOut);
            }
            project_->syncTimelineToCharacters();
            setStatus("IK arc keyed from current frame");
        }
    }
}

void Editor::buildCutCharacter() {
    // find the active raster layer as cut source
    RasterLayer* rl = project_->activeRasterLayer();
    if (!rl || rl->frames.empty()) { setStatus("select a raster layer with artwork first"); return; }
    Image* src = rl->frameAt(frame_);
    if (!src || src->isBlank()) { setStatus("no artwork on this frame to cut"); return; }
    if (cuts_.empty()) { setStatus("drag cut rectangles first (tool P)"); return; }
    CharacterCutter cutter;
    cutter.setSource(*src);
    cutter.setKeyColor(Color(255, 255, 255, 255), 12);
    for (const CutSpec& c : cuts_) cutter.addCut(c);
    Character ch;
    std::string err;
    if (!cutter.build(ch, &err)) { setStatus("cut failed: " + err); return; }
    ch.name = "Cut " + std::to_string(project_->characters.size() + 1);
    std::string cid = project_->addCharacter(std::make_shared<Character>(ch));
    auto cl = std::make_shared<CharacterLayer>();
    cl->id = Project::newLayerId();
    cl->name = ch.name;
    cl->characterId = cid;
    cl->position = Vec2(0, 0);
    cl->scale = 1.0f;
    project_->addLayer(cl);
    project_->activeLayerId = cl->id;
    selectedBone_ = -1;
    setStatus("character built: " + ch.name);
}

// ---------------- timeline ----------------
void Editor::drawTimeline(Ui& ui, int x, int y, int w, int h) {
    const Theme& t = Theme::get();
    ui.panel(x, y, w, h, t.timelineBg);
    ui.frame(x, y, w, h, t.border);

    // --- control row: labeled clusters, everything on one aligned baseline ---
    const int ch = 24;             // control height
    const int cy = y + kPad + 12;  // control row y (label sits above at y+kPad)
    auto clusterLabel = [&](int cx, const std::string& s) {
        ui.label(s, cx, y + kPad, t.textDim);
    };

    // playback
    int bx = x + kPad;
    clusterLabel(bx, "Playback");
    if (ui.button(wid(), playing_ ? "Pause" : "Play", bx, cy, 62, ch, playing_))
        playing_ = !playing_;
    bx += 66;
    if (ui.button(wid(), "<", bx, cy, 26, ch)) frame_ = std::max(0, frame_ - 1);
    bx += 30;
    if (ui.button(wid(), ">", bx, cy, 26, ch)) frame_ = std::min(project_->frameCount - 1, frame_ + 1);
    bx += 34;
    char frameLbl[64];
    snprintf(frameLbl, sizeof frameLbl, "%d / %d", frame_ + 1, project_->frameCount);
    centerLabel(frameLbl, bx, cy, 74, ch, t.text);
    bx += 82;

    // onion range
    clusterLabel(bx, "Onion before/after");
    int nb = onionBefore_, na = onionAfter_;
    if (ui.button(wid(), "-", bx, cy, 22, ch)) nb = std::max(0, onionBefore_ - 1);
    centerLabel(std::to_string(onionBefore_), bx + 24, cy, 26, ch, t.text);
    if (ui.button(wid(), "+", bx + 52, cy, 22, ch)) nb = std::min(5, onionBefore_ + 1);
    if (ui.button(wid(), "-", bx + 80, cy, 22, ch)) na = std::max(0, onionAfter_ - 1);
    centerLabel(std::to_string(onionAfter_), bx + 104, cy, 26, ch, t.text);
    if (ui.button(wid(), "+", bx + 132, cy, 22, ch)) na = std::min(5, onionAfter_ + 1);
    onionBefore_ = nb;
    onionAfter_ = na;
    bx += 166;

    // stabilizer (track-only slider, value readout centered to its right)
    clusterLabel(bx, "Stabilizer");
    int newStab = (int)ui.slider(wid(), "", bx, cy - 4, 110, (float)stabilizer_, 0, 10);
    if (newStab != stabilizer_) stabilizer_ = newStab;
    centerLabel(std::to_string(stabilizer_), bx + 114, cy, 30, ch, t.text);
    bx += 154;

    // audio
    clusterLabel(bx, "Audio");
    if (ui.button(wid(), project_->audio ? "Loaded" : "Import", bx, cy, 62, ch))
        openDialog(Dialog::ImportAudio);
    bx += 66;
    if (project_->audio) {
        if (ui.button(wid(), "Clear", bx, cy, 48, ch)) {
            project_->audio.reset();
            audioStripVisible_ = false;
        }
        bx += 52;
        char ainfo[96];
        snprintf(ainfo, sizeof ainfo, "%.1fs = %d frames", project_->audio->durationSec(),
                 project_->audio->durationFrames(project_->fps));
        ui.label(ainfo, bx, cy + 6, t.textDim);
    }
    bx += 150; // reserve space so long audio readouts never collide the next cluster

    // playback mode (OpenToonz flipbook: loop / ping-pong / once)
    clusterLabel(bx, "Play mode");
    {
        const char* modes[] = {"Loop", "Ping-pong", "Once"};
        int m = (int)project_->playMode;
        if (ui.button(wid(), modes[m], bx, cy, 72, ch, m != (int)PlayMode::Loop))
            project_->playMode = (PlayMode)((m + 1) % 3);
    }
    bx += 80;

    // play range markers (OpenToonz "Playback Start/End Marker")
    clusterLabel(bx, "Range");
    if (ui.button(wid(), "Set S", bx, cy, 48, ch, project_->playStart >= 0)) {
        project_->playStart = frame_;
        if (project_->playEnd >= 0 && project_->playEnd < project_->playStart)
            project_->playEnd = -1;
        setStatus("playback start marker set (OpenToonz-style)");
    }
    bx += 52;
    if (ui.button(wid(), "Set E", bx, cy, 48, ch, project_->playEnd >= 0)) {
        project_->playEnd = frame_;
        if (project_->playStart >= 0 && project_->playStart > project_->playEnd)
            project_->playStart = -1;
        setStatus("playback end marker set");
    }
    bx += 52;
    if (ui.button(wid(), "Clr", bx, cy, 38, ch)) {
        project_->playStart = -1;
        project_->playEnd = -1;
    }
    bx += 46;

    // camera keys (Synfig "animate everything": the camera has its own track)
    clusterLabel(bx, "Camera");
    if (ui.button(wid(), "Cam Key", bx, cy, 62, ch, project_->cameraTrack.hasKey(frame_))) {
        project_->cameraTrack.setKey(frame_, project_->camera);
        selCamKey_ = frame_;
        setStatus("camera key set at frame " + std::to_string(frame_ + 1));
    }
    bx += 66;
    if (ui.button(wid(), animAutoKey_ ? "Anim: on" : "Anim: off", bx, cy, 74, ch, animAutoKey_))
        animAutoKey_ = !animAutoKey_; // Synfig animate mode: camera drags auto-key
    bx += 82;

    // ease selector (Synfig waypoint interpolation): applies to the selected
    // camera key, else the pose key at the playhead on the active character layer
    {
        static const char* easeNames[] = {"Linear", "Ease In", "Ease Out", "Ease In/Out", "Constant"};
        Ease cur = Ease::Linear;
        bool have = false;
        CameraKey ck;
        if (selCamKey_ >= 0 && project_->cameraTrack.getKey(selCamKey_, ck)) {
            cur = ck.ease;
            have = true;
        } else {
            CharacterLayer* cl = activeCharacterLayer();
            Character* c = cl ? project_->findCharacter(cl->characterId) : nullptr;
            PoseKey pk;
            if (c && c->track.getKey(frame_, pk)) { cur = pk.ease; have = true; }
        }
        clusterLabel(bx, have ? "Ease (key)" : "Ease");
        int e = (int)cur;
        if (ui.button(wid(), "<", bx, cy, 20, ch)) e = (e + 4) % 5;
        centerLabel(easeNames[e], bx + 22, cy, 76, ch, t.text);
        if (ui.button(wid(), ">", bx + 100, cy, 20, ch)) e = (e + 1) % 5;
        if (have) {
            Ease ne = (Ease)e;
            if (ne != cur) {
                if (selCamKey_ >= 0 && project_->cameraTrack.getKey(selCamKey_, ck)) {
                    ck.ease = ne;
                    project_->cameraTrack.setKey(selCamKey_, ck.cam, ne);
                    setStatus("camera key ease changed");
                } else {
                    CharacterLayer* cl = activeCharacterLayer();
                    Character* c = cl ? project_->findCharacter(cl->characterId) : nullptr;
                    PoseKey pk;
                    if (c && c->track.getKey(frame_, pk)) {
                        c->track.setKey(frame_, pk.pose, ne);
                        setStatus("pose key ease changed");
                    }
                }
            }
        }
        bx += 128;
    }

    // --- ruler + tracks ---
    int tx = x + kPad, ty = y + kPad + 12 + ch + 10;
    int tw = w - kPad * 2, th = y + h - ty - kPad;
    ui.rect(tx, ty, tw, th, t.panelBg2);
    int frames = std::max(1, project_->frameCount);
    float pxPerFrame = (float)tw / frames;

    bool rulerHover = inRect(ui.mouse().x, ui.mouse().y, tx, ty, tw, 16);
    if (rulerHover && ui.leftDown())
        frame_ = clampf((ui.mouse().x - tx) / pxPerFrame, 0, frames - 1);
    int step = 1;
    while (step * pxPerFrame < 16) step *= 2;
    for (int f = 0; f < frames; f += step) {
        int lx = tx + (int)(f * pxPerFrame);
        ui.label(std::to_string(f + 1), lx, ty + 1, t.textDim);
        fb_.drawLine((float)lx, (float)ty + 14, (float)lx, (float)ty + th, t.border, 1);
    }
    // play range brackets (OpenToonz playback markers)
    if (project_->playStart >= 0) {
        int lx = tx + (int)(project_->playStart * pxPerFrame);
        Color c(90, 220, 160, 230);
        fb_.drawLine((float)lx, (float)ty + 13, (float)lx, (float)ty + th, c, 2.0f);
        fb_.drawLine((float)lx, (float)ty + 13, (float)lx + 6, (float)ty + 13, c, 2.0f);
    }
    if (project_->playEnd >= 0) {
        int lx = tx + (int)(project_->playEnd * pxPerFrame);
        Color c(90, 220, 160, 230);
        fb_.drawLine((float)lx, (float)ty + 13, (float)lx, (float)ty + th, c, 2.0f);
        fb_.drawLine((float)lx - 6, (float)ty + 13, (float)lx, (float)ty + 13, c, 2.0f);
    }

    int rowY = ty + 17;
    int rowH = advancedTimeline_ ? 24 : 36;

    auto drawKeyDiamond = [&](int f, int ryCenter, bool keyed, Color col) {
        int cx = tx + (int)(f * pxPerFrame) + (int)(pxPerFrame * 0.5f);
        for (int i = -3; i <= 3; ++i)
            fb_.drawLine((float)(cx - 3 + std::abs(i)), (float)(ryCenter - 3 + i),
                         (float)(cx - 3 + std::abs(i) + 1), (float)(ryCenter - 3 + i),
                         keyed ? col : t.border, 1);
        for (int i = 3; i >= -3; --i)
            fb_.drawLine((float)(cx + 3 - std::abs(i)), (float)(ryCenter + 3 - i),
                         (float)(cx + 3 - std::abs(i) + 1), (float)(ryCenter + 3 - i),
                         keyed ? col : t.border, 1);
    };

    // camera track row (compact strip above the layer rows; OpenToonz-style)
    if (!project_->cameraTrack.keys().empty()) {
        const int camH = 22;
        int camY = rowY;
        bool camHover = inRect(ui.mouse().x, ui.mouse().y, tx, camY, tw, camH);
        if (camHover) ui.rect(tx + 1, camY, tw - 2, camH, Color(60, 66, 84, 90));
        ui.label("camera", tx + 6, camY + (camH - 9) / 2, t.textDim);
        for (const CameraKey& k : project_->cameraTrack.keys()) {
            bool sel = k.frame == selCamKey_;
            drawKeyDiamond(k.frame, camY + camH / 2, true,
                           sel ? Color(240, 200, 90, 255) : t.keyframe);
            int dcx = tx + (int)(k.frame * pxPerFrame) + (int)(pxPerFrame * 0.5f);
            if (inRect(ui.mouse().x, ui.mouse().y, dcx - 5, camY, 10, camH) && ui.leftDown() &&
                ui.pressed(wid() + 5600 + (unsigned)k.frame)) {
                selCamKey_ = (selCamKey_ == k.frame) ? -1 : k.frame;
                frame_ = k.frame;
                project_->camera = k.cam; // load the key into the editable camera
                setStatus("camera key selected: frame " + std::to_string(k.frame + 1));
            }
        }
        rowY += camH;
        th -= camH;
    }


    int rowIndex = 0;
    for (auto& l : project_->layers) {
        if (rowY + rowIndex * rowH + rowH > ty + th) break;
        int ry = rowY + rowIndex * rowH;
        bool active = l->id == project_->activeLayerId;
        if (active) ui.rect(tx + 1, ry, tw - 2, rowH - 1, t.accentDim);
        ui.label(l->name.substr(0, 16), tx + 6, ry + (rowH - 9) / 2, active ? t.text : t.textDim);
        if (l->type == LayerType::Raster) {
            RasterLayer* rl = (RasterLayer*)l.get();
            for (auto& kv : rl->frames) {
                int lx = tx + (int)(kv.first * pxPerFrame);
                Color c = active ? Color(120, 170, 255, 200) : Color(90, 110, 150, 160);
                int wpx = std::max(2, (int)pxPerFrame - 1);
                bool painted = !kv.second.isBlank();
                ui.rect(lx, ry + (advancedTimeline_ ? 5 : 7), wpx, rowH - (advancedTimeline_ ? 10 : 14),
                        painted ? c : t.border);
            }
        } else if (l->type == LayerType::Character) {
            CharacterLayer* cl = (CharacterLayer*)l.get();
            Character* c = project_->findCharacter(cl->characterId);
            if (c)
                for (const PoseKey& k : c->track.keys())
                    drawKeyDiamond(k.frame, ry + rowH / 2, true, t.keyframe);
        }
        if (inRect(ui.mouse().x, ui.mouse().y, tx, ry, tw, rowH) && ui.leftDown() &&
            ui.pressed(wid() + 5000 + (unsigned)rowIndex)) {
            project_->activeLayerId = l->id;
        }
        rowIndex++;
    }

    // audio waveform strip (bottom of the track area)
    if (project_->audio && audioStripVisible_) {
        int wy = ty + th - 30;
        ui.rect(tx, wy, tw, 28, Color(20, 40, 30, 255));
        int cols = tw / 2;
        for (int i = 0; i < cols; ++i) {
            double t0 = (double)i / cols * project_->audio->durationSec();
            int s0 = (int)(t0 * project_->audio->sampleRate) * project_->audio->channels;
            int s1 = (int)((t0 + project_->audio->durationSec() / cols) * project_->audio->sampleRate) *
                     project_->audio->channels;
            float peak = 0;
            for (int sIdx = s0; sIdx < s1 && sIdx < (int)project_->audio->samples.size(); sIdx += 7)
                peak = std::max(peak, std::fabs(project_->audio->samples[(size_t)sIdx]));
            int hh = (int)(peak * 12);
            ui.rect(tx + i * 2, wy + 14 - hh, 1, hh * 2 + 1, Color(90, 220, 140, 220));
        }
    }

    // playhead
    int phx = tx + (int)(frame_ * pxPerFrame);
    fb_.drawLine((float)phx, (float)ty, (float)phx, (float)ty + th, t.playhead, 2);
    ui.rect(phx - 1, ty - 2, 4, 8, t.playhead);
}

// ---------------- status bar ----------------
void Editor::drawStatus(Ui& ui, int w, int h) {
    // dedicated bottom row (below the timeline — no more overlapping text)
    const Theme& t = Theme::get();
    int ry = h - kStatusH;
    ui.panel(0, ry, w, kStatusH, t.panelBg);
    ui.frame(0, ry, w, kStatusH, t.border);
    int ty = ry + (kStatusH - 9) / 2;
    if (statusFlash_ > 0) {
        statusFlash_--;
        uiFont().draw(fb_, statusText_, kPad, (float)ty, t.ok);
    } else {
        std::string s = "Ctrl+S save · Ctrl+O open · B/E brush · C camera · K bones · I IK · P cut · Space play";
        uiFont().draw(fb_, s, kPad, (float)ty, t.textDim);
    }
    std::string ver = std::string("Leptid ") + kVersion;
    float vw = uiFont().textWidth(ver);
    uiFont().draw(fb_, ver, (float)(w - vw - kPad), (float)ty, t.textDim);
}

// ---------------- dialogs ----------------
void Editor::openDialog(Dialog d) {
    dialog_ = d;
    dialogScroll_ = 0;
    dialogError_.clear();
    dialogFile_ = d == Dialog::SaveLep ? "untitled.lep" : d == Dialog::SaveLchar ? "hero.Lchar" : "";
    refreshDialogEntries();
}

void Editor::refreshDialogEntries() {
    dialogEntries_.clear();
    for (const std::string& n : listDir(dialogDir_)) dialogEntries_.push_back(n);
}

void Editor::drawDialog(Ui& ui, int w, int h) {
    const Theme& t = Theme::get();
    // dim backdrop
    for (auto& p : fb_.px) {
        uint8_t a = (uint8_t)(p >> 24);
        if (a) { p = (p & 0x00FFFFFFu) | ((uint32_t)(a * 60 / 100) << 24); }
    }
    int dw = 520, dh = 380;
    int dx = (w - dw) / 2, dy = (h - dh) / 2;
    ui.panel(dx, dy, dw, dh, t.panelBg);
    ui.frame(dx, dy, dw, dh, t.accent);

    const char* title = dialog_ == Dialog::Open ? "Open Project (.lep)"
                      : dialog_ == Dialog::SaveLep ? "Save Project (.lep)"
                      : dialog_ == Dialog::ImportChar ? "Import Character (.Lchar)"
                      : dialog_ == Dialog::ImportAudio ? "Import Audio (wav/mp3/ogg)"
                      : dialog_ == Dialog::Export ? "Export"
                      : dialog_ == Dialog::AI ? "AI Assistant (optional, local Ollama)"
                      : dialog_ == Dialog::Help ? "Quick Help" : "Dialog";
    ui.label(title, dx + 14, dy + 12, t.text, true);

    if (dialog_ == Dialog::AI) {
        int yy = dy + 44;
        ui.label("host:", dx + 14, yy + 4, t.textDim);
        ui.textbox(wid(), aiHost_, dx + 56, yy, 140);
        ui.label("model:", dx + 206, yy + 4, t.textDim);
        ui.textbox(wid(), aiModel_, dx + 258, yy, 130);
        yy += 30;
        ui.label("ask:", dx + 14, yy + 4, t.textDim);
        ui.textbox(wid(), aiQuestion_, dx + 56, yy, 340);
        yy += 30;
        if (ui.button(wid(), "Ask", dx + 14, yy, 90, 26, true)) {
            OllamaConfig cfg;
            cfg.host = aiHost_;
            cfg.port = 11434;
            cfg.model = aiModel_;
            if (!ollamaAvailable(cfg)) {
                aiStatus_ = "no Ollama server found (optional feature)";
                aiReply_.clear();
            } else {
                aiStatus_ = "thinking...";
                aiReply_.clear();
                std::string err;
                std::string reply;
                if (ollamaGenerate(cfg, leptidKnowledgeSummary(), aiQuestion_, reply, &err)) {
                    aiReply_ = reply.substr(0, 900);
                    aiStatus_.clear();
                } else {
                    aiStatus_ = err;
                }
            }
        }
        if (ui.button(wid(), "Close", dx + 112, yy, 80, 26)) dialog_ = Dialog::None;
        if (!aiStatus_.empty()) ui.label(aiStatus_, dx + 204, yy + 6, aiStatus_.find("no Ollama") == 0 ? t.textDim : t.danger);
        // reply box
        int by = yy + 36;
        ui.rect(dx + 14, by, dw - 28, dh - (by - dy) - 16, t.panelBg2);
        // word-wrap the reply
        std::string word, line;
        int ly = by + 8;
        for (char ch : aiReply_ + " ") {
            if (ch != ' ' && ch != '\n') { word += ch; continue; }
            if (uiFont().textWidth(line + word) > dw - 44) {
                uiFont().draw(fb_, line, (float)dx + 22, (float)ly, t.text);
                ly += 16;
                line.clear();
            }
            line += word + " ";
            word.clear();
            if (ly > by + (dh - (by - dy) - 30)) break;
        }
        if (!line.empty()) uiFont().draw(fb_, line, (float)dx + 22, (float)ly, t.text);
        return;
    }

    if (dialog_ == Dialog::Export) {
        int yy = dy + 44;
        bool png = expPng_;
        if (ui.checkbox(wid(), "PNG sequence -> " + expDir_ + "/frame_0000.png", dx + 14, yy, png)) expPng_ = !expPng_;
        yy += 26;
        bool gif = expGif_;
        if (ui.checkbox(wid(), "Animated GIF", dx + 14, yy, gif)) expGif_ = !expGif_;
        yy += 26;
        bool mp4 = expMp4_;
        if (ui.checkbox(wid(), "MP4 (needs ffmpeg)", dx + 14, yy, mp4)) expMp4_ = !expMp4_;
        yy += 34;
        ui.label("PNG dir:", dx + 14, yy + 4, t.textDim);
        ui.textbox(wid(), expDir_, dx + 80, yy, 160);
        ui.label("gif:", dx + 250, yy + 4, t.textDim);
        ui.textbox(wid(), expGifName_, dx + 280, yy, 140);
        yy += 30;
        ui.label("mp4:", dx + 14, yy + 4, t.textDim);
        ui.textbox(wid(), expMp4Name_, dx + 80, yy, 160);
        yy += 40;
        if (ui.button(wid(), "Export", dx + 14, yy, 110, 28, true)) doExport();
        if (ui.button(wid(), "Close", dx + 134, yy, 90, 28)) dialog_ = Dialog::None;
        if (!expStatus_.empty()) ui.label(expStatus_, dx + 240, yy + 8, t.ok);
        return;
    }

    if (dialog_ == Dialog::Help) {
        const char* lines[] = {
            "B brush   E eraser   C camera tool (drag = move camera, wheel = zoom camera)",
            "K bone tool: click a bone, drag to rotate (FK)",
            "I IK tool: drag a hand/foot; stable IK + limits, pose keys on release",
            "P cut tool: drag rectangles over a T-pose layer, then Build Character",
            "Space play/pause   arrows step frames   wheel = editor zoom",
            "Ctrl+Z / Ctrl+Y undo/redo   Ctrl+S save   Ctrl+O open   Ctrl+1 import character",
            "",
            ".lep v3 projects are self-contained ZIP archives (JSON + limb PNGs).",
            "Characters are saved/loaded as .Lchar assets."};
        int yy = dy + 44;
        for (const char* l : lines) {
            ui.label(l, dx + 14, yy, t.textDim);
            yy += 20;
        }
        if (ui.button(wid(), "Close", dx + 14, dy + dh - 40, 90, 26)) dialog_ = Dialog::None;
        return;
    }

    // file browser dialogs
    int listH = 210;
    int clicked = ui.listbox(wid(), dx + 14, dy + 44, dw - 28, listH, dialogEntries_, -1, &dialogScroll_);
    if (clicked >= 0) {
        std::string name = dialogEntries_[(size_t)clicked];
        std::string full = dialogDir_ + "/" + name;
        if (isDir(full)) { dialogDir_ = full; refreshDialogEntries(); }
        else dialogFile_ = name;
    }
    int yy = dy + 44 + listH + 12;
    ui.label("file:", dx + 14, yy + 4, t.textDim);
    ui.textbox(wid(), dialogFile_, dx + 56, yy, 250);
    if (ui.button(wid(), "..", dx + 316, yy, 36, 22)) {
        // parent dir
        size_t pos = dialogDir_.rfind('/');
        if (pos != std::string::npos && pos > 0) dialogDir_ = dialogDir_.substr(0, pos);
        refreshDialogEntries();
    }
    yy += 34;
    if (ui.button(wid(), dialog_ == Dialog::Open ? "Open" : "Save", dx + 14, yy, 100, 28, true)) {
        if (dialogFile_.empty()) { dialogError_ = "enter a file name"; return; }
        std::string full = dialogDir_ + "/" + dialogFile_;
        if (dialog_ == Dialog::Open || dialog_ == Dialog::ImportChar || dialog_ == Dialog::ImportAudio) {
            if (dialog_ == Dialog::Open) { openProject(full); dialog_ = Dialog::None; }
            else if (dialog_ == Dialog::ImportAudio) {
                std::shared_ptr<AudioClip> clip;
                std::string aerr;
                if (importAudio(full, clip, &aerr)) {
                    project_->audio = clip;
                    project_->syncTimelineToCharacters();
                    setStatus("audio imported: timeline synced to " + std::to_string(clip->durationFrames(project_->fps)) + " frames");
                    audioStripVisible_ = true;
                    dialog_ = Dialog::None;
                } else {
                    dialogError_ = aerr;
                }
            }
            else {
                // import character as layer (Ctrl+1 path)
                Character c;
                std::string err;
                if (loadLchar(full, c, &err)) {
                    std::string cid = project_->addCharacter(std::make_shared<Character>(c));
                    auto cl = std::make_shared<CharacterLayer>();
                    cl->id = Project::newLayerId();
                    cl->name = c.name;
                    cl->characterId = cid;
                    project_->addLayer(cl);
                    project_->activeLayerId = cl->id;
                    project_->syncTimelineToCharacters();
                    setStatus("character imported as layer");
                    dialog_ = Dialog::None;
                } else {
                    dialogError_ = err;
                }
            }
        } else {
            saveProject(full);
            dialog_ = Dialog::None;
        }
    }
    if (ui.button(wid(), "Cancel", dx + 124, yy, 80, 28)) dialog_ = Dialog::None;
    if (!dialogError_.empty()) ui.label(dialogError_, dx + 214, yy + 8, t.danger);
}

void Editor::doExport() {
    ExportOptions opt;
    opt.width = 800;
    opt.height = 600;
    std::string err;
    expStatus_ = "exporting...";
    std::string pngDir = expPng_ ? expDir_ : "";
    std::string gifPath = expGif_ ? expGifName_ : "";
    std::string mp4Path = expMp4_ ? expMp4Name_ : "";
    // simple relative paths
    if (!exportAll(*project_, pngDir, gifPath, mp4Path, opt, &err)) {
        expStatus_ = err;
        return;
    }
    expStatus_ = "done";
    setStatus("export complete");
}

// ---------------- shortcuts ----------------
void Editor::handleShortcuts(Ui& ui) {
    bool ctrl = ui.keyDown(Key::Ctrl);
    if (ui.keyPressed(Key::Space)) playing_ = !playing_;
    if (ui.keyPressed(Key::Left)) frame_ = std::max(0, frame_ - 1);
    if (ui.keyPressed(Key::Right)) frame_ = std::min(project_->frameCount - 1, frame_ + 1);
    if (ctrl && ui.keyPressed(Key::Z)) { project_->commands.undo(); setStatus("undo"); }
    if (ctrl && ui.keyPressed(Key::Y)) { project_->commands.redo(); setStatus("redo"); }
    if (ctrl && ui.keyPressed(Key::S)) {
        if (projectPath_.empty()) openDialog(Dialog::SaveLep);
        else saveProject(projectPath_);
    }
    if (ctrl && ui.keyPressed(Key::O)) openDialog(Dialog::Open);
    if (ctrl && ui.keyPressed(Key::Num1)) openDialog(Dialog::ImportChar);
    if (ui.keyPressed(Key::B)) tool_ = Tool::Brush;
    if (ui.keyPressed(Key::E)) tool_ = Tool::Eraser;
    if (ui.keyPressed(Key::C)) tool_ = Tool::Camera;
    if (ui.keyPressed(Key::K)) tool_ = Tool::Bone;
    if (ui.keyPressed(Key::I)) tool_ = Tool::IK;
    if (ui.keyPressed(Key::P)) tool_ = Tool::Cut;
    if (ui.keyPressed(Key::Escape)) dialog_ = Dialog::None;
}

} // namespace leptid
