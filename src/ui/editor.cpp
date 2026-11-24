// Leptid — editor implementation (part 1: shell, canvas, tools).
#include "ui/editor.h"
#include "export/exporter.h"
#include "formats/pngio.h"
#include "rig/ik.h"
#include <cmath>
#include <cstring>

namespace leptid_embed {
extern const unsigned char leptid_icon_256_png[];
extern const unsigned leptid_icon_256_png_len;
}

namespace leptid {


Editor::Editor() {
    project_ = Project::createDefault();
    // default raster canvas size
    for (auto& l : project_->layers)
        if (l->type == LayerType::Raster) ((RasterLayer*)l.get())->frames[0].create(1200, 900);
    view_ = ViewTransform::fromCamera(project_->camera, 950, 596);
    view_.cx = 600; view_.cy = 450; view_.scale = 0.6f;
    palette_ = {
        Color(240, 240, 240, 255), Color(30, 30, 34, 255), Color(220, 90, 90, 255),
        Color(90, 180, 90, 255), Color(90, 140, 240, 255), Color(240, 200, 90, 255),
        Color(160, 90, 220, 255), Color(240, 150, 60, 255)};
    brush_.color = palette_[0];
    brush_.size = 8;
    cutSlots_ = {"torso", "head", "armL_upper", "armL_lower", "armR_upper", "armR_lower",
                 "legL_upper", "legL_lower", "legR_upper", "legR_lower", "armL", "armR", "legL", "legR"};
    cutSlotIdx_ = 0;
    cutSlot_ = cutSlots_[0];
    project_->activeLayerId = project_->layers[0]->id;
}

Editor::~Editor() = default;

bool Editor::openProject(const std::string& path) {
    auto p = std::make_shared<Project>();
    std::string err;
    if (!loadLep(path, *p, &err)) { setStatus("open failed: " + err); return false; }
    project_ = p;
    projectPath_ = path;
    view_ = ViewTransform::fromCamera(project_->camera, (int)view_.viewW, (int)view_.viewH);
    setStatus("opened " + path);
    return true;
}

bool Editor::saveProject(const std::string& path) {
    std::string err;
    if (!saveLep(path, *project_, &err)) { setStatus("save failed: " + err); return false; }
    projectPath_ = path;
    setStatus("saved " + path);
    return true;
}

void Editor::renderOnce(Ui& ui, int width, int height) {
    fb_.create(width, height);
    fb_.clear(Theme::get().windowBg);
    ui.begin(&fb_, -100, -100, false, false);
    update(ui, width, height, 0.0);
}

void Editor::update(Ui& ui, int width, int height, double dt) {
    if (fb_.w != width || fb_.h != height || fb_.empty()) {
        fb_.create(width, height);
    }
    fb_.clear(Theme::get().windowBg);
    handleShortcuts(ui);

    // playback: loop / ping-pong / once, bounded by the play range when set
    // (OpenToonz "Playback Start/End Marker" behavior)
    if (playing_) {
        playAccum_ += dt;
        double spf = 1.0 / std::max(1, project_->fps);
        int f0 = project_->playStart >= 0 ? std::max(0, project_->playStart) : 0;
        int f1 = project_->playEnd >= 0
                     ? std::min(project_->playEnd, project_->frameCount - 1)
                     : project_->frameCount - 1;
        int count = std::max(1, f1 - f0 + 1);
        while (playAccum_ >= spf && playing_) {
            playAccum_ -= spf;
            int rel = std::min(std::max(frame_, f0), f1) - f0;
            int next = advancePlayhead(rel, count, project_->playMode, playForward_);
            frame_ = next + f0;
            if (project_->playMode == PlayMode::Once && frame_ >= f1) playing_ = false;
        }
    }

    // layout: top bar / rail | canvas | right column / timeline / status row
    int canvasX = kRailW + kGap;
    int canvasY = kTopH + kGap;
    int canvasW = width - canvasX - kRightW - kGap;
    int canvasH = height - canvasY - kTimelineH - kStatusH - kGap;
    int layersH = (int)((canvasH - kGap) * 0.50f);
    drawTopBar(ui, width);
    drawToolbar(ui, kGap, canvasY, canvasH);
    drawCanvas(ui, canvasX, canvasY, canvasW, canvasH);
    drawLayersPanel(ui, width - kRightW, canvasY, kRightW, layersH);
    drawRigPanel(ui, width - kRightW, canvasY + layersH + kGap, kRightW,
                 canvasH - layersH - kGap);
    drawTimeline(ui, kGap, canvasY + canvasH + kGap, width - kGap * 2, kTimelineH);
    drawStatus(ui, width, height);
    if (dialog_ != Dialog::None) drawDialog(ui, width, height);
}

// ---------------- shared helpers ----------------
void Editor::centerLabel(const std::string& text, int x, int y, int w, int h, Color c, bool bold) {
    float tw = uiFont().textWidth(text);
    float th = uiFont().height() * 0.72f;
    if (bold) uiFontBig().draw(fb_, text, x + (w - tw) * 0.5f, y + (h - th) * 0.5f, c);
    else uiFont().draw(fb_, text, x + (w - tw) * 0.5f, y + (h - th) * 0.5f, c);
}

void Editor::drawLogo(Ui& ui, int x, int y, int px) {
    Image icon;
    if (!loadPngMemory(leptid_embed::leptid_icon_256_png,
                       leptid_embed::leptid_icon_256_png_len, icon))
        return;
    Image small = icon.resized(px, px);
    fb_.blit(small, x, y, 1.0f);
}

void Editor::drawEye(Ui& ui, int x, int y, bool visible, unsigned id, bool* toggled) {
    const Theme& t = Theme::get();
    int s = 18;
    bool hover = inRect(ui.mouse().x, ui.mouse().y, x, y, s, s);
    ui.rect(x, y, s, s, hover ? t.panelBg2 : Color(0, 0, 0, 0));
    if (hover) ui.frame(x, y, s, s, t.border);
    // eye glyph: almond outline + pupil
    Color c = visible ? (hover ? t.text : t.textDim) : Color(90, 92, 100, 120);
    int cx = x + s / 2, cy = y + s / 2;
    fb_.drawLine((float)x + 2, (float)cy, (float)x + s - 2, (float)cy, c, 1.6f);
    fb_.fillCircle((float)cx, (float)cy, visible ? 3.0f : 2.0f, c);
    if (!visible) {
        fb_.drawLine((float)x + 3, (float)y + s - 4, (float)x + s - 3, (float)y + 3, c, 1.2f);
    }
    if (hover && ui.leftDown() && ui.pressed(id)) *toggled = true;
}

// ---------------- top bar ----------------
void Editor::drawTopBar(Ui& ui, int w) {
    const Theme& t = Theme::get();
    ui.panel(0, 0, w, kTopH, t.panelBg);
    ui.frame(0, 0, w, kTopH, t.border);
    drawLogo(ui, kPad, 4, 32);
    ui.label("Leptid", kPad + 38, 4, t.accent, true);
    ui.label("0.1", kPad + 38, 20, t.textDim);

    const int bh = 28, by = (kTopH - bh) / 2;
    int x = kPad + 92;
    if (ui.button(wid(), "New", x, by, 62, bh)) {
        project_ = Project::createDefault();
        projectPath_.clear();
        for (auto& l : project_->layers)
            if (l->type == LayerType::Raster) ((RasterLayer*)l.get())->frames[0].create(1200, 900);
        frame_ = 0;
        setStatus("new project");
    }
    x += 66;
    if (ui.button(wid(), "Open", x, by, 62, bh)) openDialog(Dialog::Open);
    x += 66;
    if (ui.button(wid(), "Save", x, by, 62, bh)) {
        if (projectPath_.empty()) openDialog(Dialog::SaveLep);
        else saveProject(projectPath_);
    }
    x += 70; // group separator gap
    if (ui.button(wid(), "Import .Lchar", x, by, 110, bh)) openDialog(Dialog::ImportChar);
    x += 114;
    if (ui.button(wid(), "Export", x, by, 76, bh, true)) openDialog(Dialog::Export);
    x += 84;
    if (ui.button(wid(), "Onion", x, by, 64, bh, onionOn_)) onionOn_ = !onionOn_;
    x += 68;
    if (ui.button(wid(), "Bones", x, by, 64, bh, showBones_)) showBones_ = !showBones_;
    x += 68;
    if (ui.button(wid(), advancedTimeline_ ? "Advanced" : "Simple", x, by, 84, bh))
        advancedTimeline_ = !advancedTimeline_;
    // right-aligned readout
    char fps[40];
    snprintf(fps, sizeof fps, "%d fps · frame %d / %d", project_->fps, frame_ + 1, project_->frameCount);
    float tw = uiFont().textWidth(fps);
    ui.label(fps, (int)(w - tw - kPad), (kTopH - 9) / 2, t.textDim);
}

// ---------------- toolbar (tools + brush presets + palette) ----------------
void Editor::brushBar(Ui& ui, int x, int y, int w) {
    const Theme& t = Theme::get();
    const auto& presets = BrushEngine::presets();
    ui.label("Brushes", x, y, t.text);
    ui.label("", x, y, t.textDim);
    int yy = y + 16;
    int cell = (w - kPad * 2 - 6) / 2;
    int i = 0;
    for (const BrushPreset& p : presets) {
        if (p.eraser) continue; // erasers live under the eraser tool
        int cx = x + kPad + (i % 2) * (cell + 6);
        int cy = yy + (i / 2) * (cell + 20);
        bool selected = (tool_ == Tool::Brush && brush_.id == p.id) ||
                        (tool_ == Tool::Eraser && p.id == eraserId_);
        bool hover = inRect(ui.mouse().x, ui.mouse().y, cx, cy, cell, cell);
        ui.rect(cx, cy, cell, cell, selected ? t.accentDim : (hover ? t.panelBg2 : t.panelBg));
        ui.frame(cx, cy, cell, cell, selected ? t.accent : t.border);
        // stroke-sample preview (Krita style): shows texture/flow at a glance
        Image prev = engine_.previewStroke(p.id, cell - 8, cell - 8,
                                           selected ? Color(235, 238, 245, 255) : Color(185, 190, 200, 255));
        fb_.blit(prev, cx + 4, cy + 4, 1.0f);
        centerLabel(p.name, cx, cy + cell, cell, 16, selected ? t.text : t.textDim);
        if (hover && ui.leftDown() && ui.pressed(wid() + 7000 + (unsigned)i)) {
            if (tool_ != Tool::Brush && tool_ != Tool::Eraser) tool_ = Tool::Brush;
            if (p.eraser) { eraserId_ = p.id; tool_ = Tool::Eraser; }
            else brush_.id = p.id;
        }
        i++;
    }
    yy += ((i + 1) / 2) * (cell + 20) + 6;
    // size
    ui.label("Size", x + kPad, yy, t.textDim);
    yy += 14;
    if (ui.button(wid(), "-", x + kPad, yy, 22, 22)) brush_.size = std::max(1.f, brush_.size - 2);
    if (ui.button(wid(), "+", x + kPad + 26, yy, 22, 22)) brush_.size = std::min(200.f, brush_.size + 2);
    char sz[16];
    snprintf(sz, sizeof sz, "%d px", (int)brush_.size);
    centerLabel(sz, x + kPad + 52, yy, w - kPad * 2 - 52, 22, t.text);
    yy += 30;
    // opacity
    ui.label("Opacity", x + kPad, yy, t.textDim);
    yy += 14;
    float op = ui.slider(wid(), "", x + kPad, yy, w - kPad * 2, brush_.opacity, 0.05f, 1.0f);
    brush_.opacity = op;
    yy += 26;
    // palette (aligned 2-col grid)
    ui.label("Color", x + kPad, yy, t.textDim);
    yy += 14;
    for (size_t ci = 0; ci < palette_.size(); ++ci) {
        int px = x + kPad + (int)(ci % 2) * (cell + 6);
        int py = yy + (int)(ci / 2) * 24;
        ui.rect(px, py, cell, 18, palette_[ci]);
        ui.frame(px, py, cell, 18, brush_.color == palette_[ci] ? t.text : t.border);
        if (inRect(ui.mouse().x, ui.mouse().y, px, py, cell, 18) && ui.leftDown() && ui.pressed(wid() + 8000 + (unsigned)ci))
            brush_.color = palette_[ci];
    }
}

void Editor::drawToolbar(Ui& ui, int x, int y, int h) {
    const Theme& t = Theme::get();
    int w = kRailW;
    ui.panel(x, y, w, h, t.panelBg);
    ui.frame(x, y, w, h, t.border);
    // tools: 2-column grid with labels
    struct B { const char* c; const char* name; Tool tl; };
    B tools[] = {{"B", "Draw", Tool::Brush},   {"E", "Erase", Tool::Eraser},
                 {"C", "Camera", Tool::Camera}, {"K", "Bone", Tool::Bone},
                 {"I", "IK", Tool::IK},        {"P", "Cut", Tool::Cut}};
    int cell = (w - kPad * 2 - 6) / 2;
    int yy = y + kPad;
    for (size_t i = 0; i < 6; ++i) {
        int cx = x + kPad + (int)(i % 2) * (cell + 6);
        int cy = yy + (int)(i / 2) * 46;
        bool hl = tool_ == tools[i].tl;
        bool clicked = ui.button(wid() + 6000 + (unsigned)i, tools[i].c, cx, cy, cell, 30, hl);
        centerLabel(tools[i].name, cx, cy + 30, cell, 14, hl ? t.text : t.textDim);
        if (clicked) tool_ = tools[i].tl;
    }
    yy += 3 * 46 + 6;
    ui.frame(x + kPad, yy, w - kPad * 2, 1, t.border);
    yy += kGap;
    brushBar(ui, x, yy, w);
}

// ---------------- canvas ----------------
void Editor::drawCanvas(Ui& ui, int x, int y, int w, int h) {
    const Theme& t = Theme::get();
    ui.panel(x, y, w, h, t.canvasBg);
    view_.viewW = w;
    view_.viewH = h;
    // With camera keys present the preview follows the animated camera
    // (playback/export path). The Camera tool keeps the manual view for editing.
    if (!project_->cameraTrack.keys().empty() && (tool_ != Tool::Camera || playing_)) {
        view_ = ViewTransform::fromCamera(project_->cameraAtFrame(frame_), w, h);
    }

    // composite through the editor view
    RenderOptions ro;
    ro.frame = frame_;
    ro.outW = w;
    ro.outH = h;
    ro.view = &view_;
    ro.background = Color(46, 48, 58, 255);
    ro.onion = onionOn_ && !playing_;
    ro.onionBefore = onionBefore_;
    ro.onionAfter = onionAfter_;
    ro.onionOpacity = onionOpacity_;
    ro.showBones = showBones_;
    ro.showCameraRect = showCameraRect_ && tool_ == Tool::Camera;
    Image world = renderFrame(*project_, ro);
    fb_.blit(world, x, y, 1.0f);
    ui.frame(x, y, w, h, t.border);

    // ruler
    ui.panel(x, y, w, 0, t.panelBg);

    if (dialog_ != Dialog::None) return; // no canvas interaction under dialogs
    handleCanvasInput(ui, x, y, w, h);

    // tool cursor hint: circle matching the active preset size
    if (tool_ == Tool::Brush || tool_ == Tool::Eraser) {
        Vec2 m = ui.mouse();
        if (inRect(m.x, m.y, x, y, w, h)) {
            const BrushPreset& p = BrushEngine::preset(tool_ == Tool::Eraser ? eraserId_ : brush_.id);
            float s = brush_.size * p.sizeFactor * view_.scale;
            Color c = tool_ == Tool::Eraser ? Color(255, 90, 90, 130)
                                            : Color(brush_.color.r, brush_.color.g, brush_.color.b, 130);
            fb_.fillCircle(m.x, m.y, s * 0.5f, Color(0, 0, 0, 0));
            fb_.fillDisc(m.x, m.y, s * 0.5f, c);
            // two-tone ring so it reads on any background
            fb_.drawLine(m.x - s * 0.5f, m.y - s * 0.5f, m.x + s * 0.5f, m.y - s * 0.5f,
                         Color(240, 240, 240, 180), 1.0f);
            fb_.drawLine(m.x - s * 0.5f, m.y + s * 0.5f, m.x + s * 0.5f, m.y + s * 0.5f,
                         Color(30, 30, 34, 180), 1.0f);
        }
    }
    // status chips: tool name + brush, world position (aligned baseline)
    const char* names[] = {"Brush", "Eraser", "Camera", "Bone", "IK", "Cut"};
    std::string chip = std::string(names[(int)tool_]);
    if (tool_ == Tool::Brush) chip += std::string(" · ") + BrushEngine::preset(brush_.id).name;
    if (tool_ == Tool::Eraser) chip += std::string(" · ") + BrushEngine::preset(eraserId_).name;
    ui.label(chip, x + 8, y + h - 20, t.text);
    char pos[64];
    Vec2 wp = view_.screenToWorldPoint(ui.mouse());
    snprintf(pos, sizeof pos, "world %.0f, %.0f   zoom %.2fx", wp.x, wp.y, view_.scale);
    float pw = uiFont().textWidth(chip);
    ui.label(pos, x + 8 + (int)pw + 16, y + h - 20, t.textDim);
}

void Editor::handleCanvasInput(Ui& ui, int x, int y, int w, int h) {
    Vec2 m = ui.mouse();
    bool overCanvas = inRect(m.x, m.y, x, y, w, h);
    if (!overCanvas) {
        rawStroke_.clear();
        stroking_ = false;
        boneDragging_ = false;
        ikDragging_ = false;
        panning_ = false;
        return;
    }
    Vec2 world = view_.screenToWorldPoint(m);

    // zoom (wheel): editor zoom around cursor
    if (ui.wheel()) {
        float factor = std::pow(1.15f, ui.wheelDelta());
        Vec2 anchor = view_.screenToWorldPoint(m);
        view_.scale = clampf(view_.scale * factor, 0.05f, 8.0f);
        // keep the world point under the cursor fixed
        Vec2 after = view_.worldToScreenPoint(anchor);
        view_.cx -= (after.x - m.x) / view_.scale;
        view_.cy -= (after.y - m.y) / view_.scale;
    }

    // middle-drag or camera tool: pan (camera tool moves the PROJECT camera, not the view)
    if (ui.active() == 0 && ui.leftDown() && tool_ == Tool::Camera) {
        if (!panning_) { panning_ = true; panStartWorld_ = world; }
        Vec2 d = (world - panStartWorld_);
        project_->camera.x -= d.x;
        project_->camera.y -= d.y;
        if (animAutoKey_) project_->cameraTrack.setKey(frame_, project_->camera); // animate mode
        return;
    }
    if (!ui.leftDown()) panning_ = false;

    switch (tool_) {
        case Tool::Brush:
        case Tool::Eraser: {
            RasterLayer* rl = project_->activeRasterLayer();
            if (!rl) break;
            if (ui.leftDown() && ui.active() == 0) {
                // begin stroke on the active layer's current frame
                Image& img = rl->ensureFrame(frame_);
                if (img.w < 2) img.create(1200, 900);
                if (rl->frames.find(frame_) != rl->frames.end() && rl->frames[frame_].w >= 2) {
                    rawStroke_.clear();
                    rawStroke_.push_back(world);
                    stroking_ = true;
                    strokeSnapshot_ = img;
                    ui.setActive(wid()); // capture drag
                }
            } else if (stroking_ && ui.active() != 0 && ui.leftDown()) {
                rawStroke_.push_back(world);
            }
            if (stroking_ && !ui.leftDown()) {
                commitStroke();
                stroking_ = false;
            }
            break;
        }
        case Tool::Bone: {
            CharacterLayer* cl = activeCharacterLayer();
            Character* c = activeCharacter();
            if (!cl || !c) break;
            if (ui.leftDown() && ui.active() == 0) {
                selectCharacterAt(world);
                cl = activeCharacterLayer();
                c = activeCharacter();
                if (c && cl) {
                    // hit-test bones (screen distance)
                    Pose pose = c->track.sample(frame_);
                    Mat3 place = view_.worldToScreen()
                               * Mat3::translation(cl->position.x, cl->position.y)
                               * Mat3::rotation(cl->rotation)
                               * Mat3::scale(cl->scale);
                    FKEvaluator fk(c->skeleton);
                    auto bw = fk.evaluate(pose);
                    int best = -1;
                    float bestD = 12;
                    for (const BoneWorld& b : bw) {
                        Vec2 a = place.apply(b.start), bb = place.apply(b.tip);
                        // point-segment distance
                        Vec2 ab = bb - a, am = m - a;
                        float tt = clampf(am.dot(ab) / (ab.len2() + 1e-6f), 0, 1);
                        float d = Vec2::dist(a + ab * tt, m);
                        if (d < bestD) { bestD = d; best = (int)(&b - &bw[0]); }
                    }
                    selectedBone_ = best;
                    if (best >= 0) {
                        poseBeforeDrag_ = c->track.sample(frame_);
                        boneDragging_ = true;
                        ui.setActive(wid());
                        selectCharacterAt(world);
                    }
                }
            }
            if (boneDragging_ && ui.leftDown() && selectedBone_ >= 0 && c) {
                // FK rotate: angle from bone start to cursor, in character-local space
                Mat3 placeInv = (Mat3::translation(cl->position.x, cl->position.y)
                              * Mat3::rotation(cl->rotation)
                              * Mat3::scale(cl->scale)).inverse();
                Vec2 local = placeInv.apply(world);
                Pose pose = c->track.sample(frame_);
                FKEvaluator fk(c->skeleton);
                auto bw = fk.evaluate(pose);
                Vec2 startLocal = bw[(size_t)selectedBone_].start;
                float want = Vec2::angleOf(local - startLocal);
                const Bone* b = c->skeleton.bone(selectedBone_);
                float parentW = b->parentId >= 0 ? bw[(size_t)b->parentId].angle : pose.rootAngle;
                float delta = normalizeAngle(want - parentW - b->restAngle);
                c->track.setKey(frame_, pose); // keep base pose at this frame
                Pose np = pose;
                np.setDelta(selectedBone_, delta);
                c->track.setKey(frame_, np);
            }
            if (!ui.leftDown() && boneDragging_) {
                boneDragging_ = false;
                if (c) {
                    Pose after = c->track.sample(frame_);
                    Pose before = poseBeforeDrag_;
                    Character* cc = c;
                    int f = frame_;
                    project_->commands.push(std::make_unique<FuncCommand>(
                        "bone edit",
                        [cc, before, f]() { cc->track.setKey(f, before); },
                        [cc, after, f]() { cc->track.setKey(f, after); }));
                }
            }
            break;
        }
        case Tool::IK: {
            CharacterLayer* cl = activeCharacterLayer();
            Character* c = activeCharacter();
            if (!cl || !c) break;
            if (ui.leftDown() && ui.active() == 0) {
                selectCharacterAt(world);
                cl = activeCharacterLayer();
                c = activeCharacter();
                if (c) {
                    // choose nearest limb chain end (hand/foot)
                    Pose pose = c->track.sample(frame_);
                    FKEvaluator fk(c->skeleton);
                    auto bw = fk.evaluate(pose);
                    struct ChainOpt { const char* lo; const char* up; };
                    ChainOpt opts[] = {{"armL_lower", "armL_upper"}, {"armR_lower", "armR_upper"},
                                       {"armL", "armL"}, {"armR", "armR"},
                                       {"legL_lower", "legL_upper"}, {"legR_lower", "legR_upper"},
                                       {"legL", "legL"}, {"legR", "legR"}};
                    int bestIdx = -1;
                    float bestD = 1e9f;
                    Mat3 placeInv = (Mat3::translation(cl->position.x, cl->position.y)
                                  * Mat3::rotation(cl->rotation)
                                  * Mat3::scale(cl->scale)).inverse();
                    Vec2 local = placeInv.apply(world);
                    for (int i = 0; i < 8; ++i) {
                        int up = c->skeleton.findByName(opts[i].up);
                        int lo = c->skeleton.findByName(opts[i].lo);
                        if (up < 0 || lo < 0 || lo == up) continue;
                        float d = Vec2::dist(bw[(size_t)lo].tip, local);
                        if (d < bestD) { bestD = d; bestIdx = i; }
                    }
                    if (bestIdx >= 0) {
                        int up = c->skeleton.findByName(opts[bestIdx].up);
                        int lo = c->skeleton.findByName(opts[bestIdx].lo);
                        ikChain_.clear();
                        if (up != lo) ikChain_.push_back(up);
                        ikChain_.push_back(lo);
                        poseBeforeDrag_ = pose;
                        ikDragging_ = true;
                        ui.setActive(wid());
                    }
                }
            }
            if (ikDragging_ && ui.leftDown() && c && cl && !ikChain_.empty()) {
                Mat3 placeInv = (Mat3::translation(cl->position.x, cl->position.y)
                              * Mat3::rotation(cl->rotation)
                              * Mat3::scale(cl->scale)).inverse();
                Vec2 local = placeInv.apply(world);
                Pose pose = c->track.sample(frame_);
                IKOptions iopt; // defaults: stable, step-clamped, limits on
                solveChainIK(c->skeleton, pose, ikChain_, local, iopt);
                clampPoseToLimits(c->skeleton, pose);
                c->track.setKey(frame_, pose);
            }
            if (!ui.leftDown() && ikDragging_) {
                ikDragging_ = false;
                if (c) {
                    Pose after = c->track.sample(frame_);
                    Pose before = poseBeforeDrag_;
                    Character* cc = c;
                    int f = frame_;
                    project_->commands.push(std::make_unique<FuncCommand>(
                        "IK pose",
                        [cc, before, f]() { cc->track.setKey(f, before); },
                        [cc, after, f]() { cc->track.setKey(f, after); }));
                }
            }
            break;
        }
        case Tool::Cut: {
            if (ui.leftDown() && ui.active() == 0) {
                cutDragStart_ = world;
                cutDragging_ = true;
                ui.setActive(wid());
            }
            if (cutDragging_ && !ui.leftDown()) {
                cutDragging_ = false;
                Vec2 a = cutDragStart_, b = world;
                CutSpec spec;
                spec.slot = cutSlot_;
                spec.x = (int)std::min(a.x, b.x);
                spec.y = (int)std::min(a.y, b.y);
                spec.w = (int)std::fabs(b.x - a.x);
                spec.h = (int)std::fabs(b.y - a.y);
                if (spec.w > 4 && spec.h > 4) {
                    cuts_.push_back(spec);
                    char msg[96];
                    snprintf(msg, sizeof msg, "cut %s (%d,%d %dx%d) — next slot in panel",
                             spec.slot.c_str(), spec.x, spec.y, spec.w, spec.h);
                    setStatus(msg);
                    cutSlotIdx_ = (cutSlotIdx_ + 1) % cutSlots_.size();
                    cutSlot_ = cutSlots_[(size_t)cutSlotIdx_];
                }
            }
            break;
        }
        default: break;
    }

    // cut rectangles preview
    if (tool_ == Tool::Cut) {
        for (const CutSpec& c : cuts_) {
            Vec2 a = view_.worldToScreenPoint(Vec2((float)c.x, (float)c.y));
            Vec2 b = view_.worldToScreenPoint(Vec2((float)(c.x + c.w), (float)(c.y + c.h)));
            fb_.strokeRect(x + (int)a.x, y + (int)a.y, (int)(b.x - a.x), (int)(b.y - a.y),
                           Theme::get().ok, 2);
        }
        if (cutDragging_) {
            Vec2 a = view_.worldToScreenPoint(cutDragStart_);
            Vec2 b = view_.worldToScreenPoint(ui.mouse());
            fb_.strokeRect(x + (int)std::min(a.x, b.x), y + (int)std::min(a.y, b.y),
                           (int)std::fabs(b.x - a.x), (int)std::fabs(b.y - a.y),
                           Theme::get().accent, 2);
        }
        ui.label(("cut slot: " + cutSlot_).c_str(), x + 8, y + 8, Theme::get().ok);
    }
}

void Editor::commitStroke() {
    RasterLayer* rl = project_->activeRasterLayer();
    if (!rl || rawStroke_.empty()) return;
    Image& img = rl->ensureFrame(frame_);
    BrushSettings b = brush_;
    b.eraser = tool_ == Tool::Eraser;
    if (b.eraser) b.id = eraserId_;
    // paint the stabilized stroke over the live-raw path
    auto smoothed = smoothStroke(rawStroke_, stabilizer_);
    IRect r1 = strokeBounds(rawStroke_, b, img.w, img.h);
    IRect r2 = strokeBounds(smoothed, b, img.w, img.h);
    IRect r{std::min(r1.x0, r2.x0), std::min(r1.y0, r2.y0), std::max(r1.x1, r2.x1), std::max(r1.y1, r2.y1)};
    if (r.x1 < r.x0) return;
    Image after = img;
    engine_.paintStroke(after, smoothed, b);
    // diff region command
    Image beforeCrop = strokeSnapshot_.crop(r.x0, r.y0, r.x1 - r.x0 + 1, r.y1 - r.y0 + 1);
    Image afterCrop = after.crop(r.x0, r.y0, r.x1 - r.x0 + 1, r.y1 - r.y0 + 1);
    Image* framePtr = &rl->ensureFrame(frame_);
    int fx = r.x0, fy = r.y0;
    project_->commands.push(std::make_unique<FuncCommand>(
        b.eraser ? "erase" : "paint",
        [framePtr, beforeCrop, fx, fy]() { framePtr->blit(beforeCrop, fx, fy, 1.0f); },
        [framePtr, afterCrop, fx, fy]() { framePtr->blit(afterCrop, fx, fy, 1.0f); }));
}

Character* Editor::activeCharacter() {
    CharacterLayer* cl = activeCharacterLayer();
    if (!cl) return nullptr;
    return project_->findCharacter(cl->characterId);
}

CharacterLayer* Editor::activeCharacterLayer() {
    Layer* l = project_->findLayer(project_->activeLayerId);
    if (l && l->type == LayerType::Character) return (CharacterLayer*)l;
    return nullptr;
}

void Editor::selectCharacterAt(const Vec2& world) {
    // pick the topmost character layer whose bounds contain the point
    for (auto it = project_->layers.rbegin(); it != project_->layers.rend(); ++it) {
        if ((*it)->type != LayerType::Character) continue;
        CharacterLayer* cl = (CharacterLayer*)it->get();
        Character* c = project_->findCharacter(cl->characterId);
        if (!c) continue;
        Mat3 placeInv = (Mat3::translation(cl->position.x, cl->position.y)
                      * Mat3::rotation(cl->rotation)
                      * Mat3::scale(cl->scale)).inverse();
        Vec2 local = placeInv.apply(world);
        Pose pose = c->track.sample(frame_);
        FKEvaluator fk(c->skeleton);
        auto bw = fk.evaluate(pose);
        for (const BoneWorld& b : bw) {
            // near any bone segment (world units)
            Vec2 ab = b.tip - b.start, am = local - b.start;
            float tt = clampf(am.dot(ab) / (ab.len2() + 1e-6f), 0, 1);
            if (Vec2::dist(b.start + ab * tt, local) < 40) {
                project_->activeLayerId = cl->id;
                selectedCharacter_ = cl->characterId;
                return;
            }
        }
    }
}

} // namespace leptid
