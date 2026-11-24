// Leptid — compositor implementation.
#include "model/compositor.h"
#include <cmath>

namespace leptid {

Image tintImage(const Image& src, Color tint, float opacity) {
    Image out = src;
    for (auto& p : out.px) {
        uint8_t a = (uint8_t)(p >> 24);
        if (a == 0) { p = 0; continue; }
        float af = (a / 255.0f) * opacity;
        // blend the original color toward the tint (keeps some luminance)
        uint8_t r = (uint8_t)(p & 0xFF), g = (uint8_t)((p >> 8) & 0xFF), b = (uint8_t)((p >> 16) & 0xFF);
        uint8_t nr = (uint8_t)(r * (1 - opacity) + tint.r * opacity);
        uint8_t ng = (uint8_t)(g * (1 - opacity) + tint.g * opacity);
        uint8_t nb = (uint8_t)(b * (1 - opacity) + tint.b * opacity);
        uint8_t na = (uint8_t)clampf(af * 255.0f, 0, 255);
        p = ((uint32_t)na << 24) | ((uint32_t)nb << 16) | ((uint32_t)ng << 8) | nr;
    }
    return out;
}

static void drawPathStroke(Image& dst, const PathStroke& s, const Mat3& m) {
    if (s.pts.size() < 2) return;
    for (size_t i = 1; i < s.pts.size(); ++i) {
        Vec2 a = m.apply(s.pts[i - 1]), b = m.apply(s.pts[i]);
        dst.drawLine(a.x, a.y, b.x, b.y, s.color, s.width);
    }
}

static void drawBoneOverlay(Image& dst, const Skeleton& sk, const Pose& pose,
                            const Mat3& m, const CharacterLayer& cl) {
    FKEvaluator fk(sk);
    Pose p = pose;
    p.rootPos = cl.position + Mat3::rotation(cl.rotation).apply(p.rootPos * cl.scale);
    auto world = fk.evaluate(p);
    Color line(80, 220, 140, 230), joint(240, 240, 120, 255);
    for (const BoneWorld& bw : world) {
        Vec2 a = m.apply(bw.start), b = m.apply(bw.tip);
        dst.drawLine(a.x, a.y, b.x, b.y, line, 2.0f);
        dst.fillDisc(a.x, a.y, 3, joint);
        dst.fillDisc(b.x, b.y, 2.5f, joint);
    }
}

static void renderLayerRecursive(const Project& proj, const Layer* layer, const Mat3& toOut,
                                 Image& out, const RenderOptions& opt, int frame);

static void renderLayerContent(const Project& proj, const Layer* layer, const Mat3& toOut,
                               Image& out, const RenderOptions& opt, int frame) {
    if (!layer->visible || layer->opacity <= 0) return;
    float op = layer->opacity * (layer->type == LayerType::Group ? 1.0f : 1.0f);
    switch (layer->type) {
        case LayerType::Raster: {
            const RasterLayer* rl = (const RasterLayer*)layer;
            // Onion skin: active raster layer's neighbours, tinted, under the current frame.
            if (opt.onion && layer->id == proj.activeLayerId) {
                auto drawNeighbor = [&](int f, Color tint) {
                    auto it = rl->frames.find(f);
                    if (it == rl->frames.end() || it->second.empty()) return;
                    Image tinted = tintImage(it->second, tint, opt.onionOpacity);
                    out.blitTransformed(tinted, toOut, opt.onionOpacity);
                };
                for (int k = opt.onionBefore; k >= 1; --k)
                    drawNeighbor(frame - k, Color(230, 70, 70, 255));  // previous = red
                for (int k = 1; k <= opt.onionAfter; ++k)
                    drawNeighbor(frame + k, Color(80, 230, 90, 255));  // next = green
            }
            auto it = rl->frames.find(frame);
            if (it != rl->frames.end() && !it->second.empty())
                out.blitTransformed(it->second, toOut, op);
            break;
        }
        case LayerType::Group: {
            const GroupLayer* gl = (const GroupLayer*)layer;
            Mat3 childMat = toOut * gl->transform;
            for (auto& c : gl->children)
                renderLayerRecursive(proj, c.get(), childMat, out, opt, frame);
            break;
        }
        case LayerType::Character: {
            const CharacterLayer* cl = (const CharacterLayer*)layer;
            const Character* c = proj.findCharacter(cl->characterId);
            if (!c) break;
            Pose pose = c->poseAtFrame(frame);
            // Character root placement: layer position/scale/rotation around the root.
            Mat3 place = toOut
                       * Mat3::translation(cl->position.x, cl->position.y)
                       * Mat3::rotation(cl->rotation)
                       * Mat3::scale(cl->scale);
            // The pose's rootPos is baked by shifting: render with modified pose.
            Pose p = pose;
            p.rootPos = p.rootPos * 0.0f; // root at origin; placement handles position
            Image charCanvas(out.w, out.h); // render character separately for opacity control
            charCanvas.clear(Color(0, 0, 0, 0));
            FKEvaluator fk(c->skeleton);
            auto world = fk.evaluate(p);
            if (c->drawOrder.size() != c->skeleton.bones.size()) {
                const_cast<Character*>(c)->ensureDrawOrder();
            }
            for (int boneId : c->drawOrder) {
                const Bone* b = c->skeleton.bone(boneId);
                if (!b || b->spritePart < 0 || b->spritePart >= (int)c->parts.size()) continue;
                const CharacterPart& part = c->parts[(size_t)b->spritePart];
                if (part.sprite.empty()) continue;
                const BoneWorld& bw = world[(size_t)b->id];
                float sc = (b->spriteLength > 1.0f) ? b->length / b->spriteLength : 1.0f;
                Mat3 sm = place
                        * Mat3::translation(bw.start.x, bw.start.y)
                        * Mat3::rotation(bw.angle - b->spriteDirAngle)
                        * Mat3::scale(sc)
                        * Mat3::translation(-part.pivot.x, -part.pivot.y);
                charCanvas.blitTransformed(part.sprite, sm, 1.0f);
            }
            out.blit(charCanvas, 0, 0, op);
            if (opt.showBones) {
                drawBoneOverlay(out, c->skeleton, pose,
                                place, *cl);
            }
            break;
        }
        case LayerType::Path: {
            const PathLayer* pl = (const PathLayer*)layer;
            for (const PathStroke& s : pl->paths) drawPathStroke(out, s, toOut);
            break;
        }
    }
}

static void renderLayerRecursive(const Project& proj, const Layer* layer, const Mat3& toOut,
                                 Image& out, const RenderOptions& opt, int frame) {
    renderLayerContent(proj, layer, toOut, out, opt, frame);
}

Image renderFrame(const Project& p, const RenderOptions& opt) {
    Image out;
    out.create(opt.outW, opt.outH);
    if (!out.empty()) out.clear(opt.background);
    ViewTransform local;
    const ViewTransform* vt = opt.view;
    if (!vt) {
        // export/headless path: the animated camera track (if any) drives output
        local = ViewTransform::fromCamera(p.cameraAtFrame(opt.frame), opt.outW, opt.outH);
        vt = &local;
    }
    Mat3 toOut = vt->worldToScreen();
    // bottom → top
    for (auto& l : p.layers)
        renderLayerRecursive(p, l.get(), toOut, out, opt, opt.frame);
    // Camera rectangle overlay for the editor (project camera shown while editing)
    if (opt.showCameraRect) {
        Camera cam = p.camera;
        Mat3 t = vt->worldToScreen();
        // draw the export viewport rectangle (project camera at 800x600 output)
        float w = 800.0f, h = 600.0f;
        Vec2 tl = t.apply(cam.x - w / (2 * cam.scale), cam.y - h / (2 * cam.scale));
        Vec2 br = t.apply(cam.x + w / (2 * cam.scale), cam.y + h / (2 * cam.scale));
        out.strokeRect((int)tl.x, (int)tl.y, (int)(br.x - tl.x), (int)(br.y - tl.y),
                       Color(90, 160, 255, 160), 2);
    }
    return out;
}

} // namespace leptid
