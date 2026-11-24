// Leptid — .lep v3 implementation.
#include "model/lepser.h"
#include "formats/json.h"
#include "formats/pngio.h"
#include "formats/zipcodec.h"
#include "rig/charser.h"

namespace leptid {

static Json poseToJson(const Pose& pose) {
    Json j = Json::object();
    j.set("rootX", Json::of((double)pose.rootPos.x));
    j.set("rootY", Json::of((double)pose.rootPos.y));
    j.set("rootAngle", Json::of((double)pose.rootAngle));
    Json angles = Json::array();
    for (auto& kv : pose.angles) {
        Json av = Json::array();
        av.push(Json::of(kv.first));
        av.push(Json::of((double)kv.second));
        angles.push(av);
    }
    j.set("angles", angles);
    return j;
}


static Json layerToJson(const Layer* l) {
    Json j = Json::object();
    j.set("id", Json::of(l->id));
    j.set("type", Json::of(std::string(l->type == LayerType::Raster ? "raster"
                            : l->type == LayerType::Group ? "group"
                            : l->type == LayerType::Character ? "character" : "path")));
    j.set("name", Json::of(l->name));
    j.set("visible", Json::of(l->visible));
    j.set("opacity", Json::of((double)l->opacity));
    if (l->type == LayerType::Raster) {
        const RasterLayer* rl = (const RasterLayer*)l;
        Json frames = Json::array();
        for (auto& kv : rl->frames)
            if (!kv.second.empty()) frames.push(Json::of(kv.first));
        j.set("frames", frames);
    } else if (l->type == LayerType::Group) {
        const GroupLayer* gl = (const GroupLayer*)l;
        Json t = Json::array();
        t.push(Json::of((double)gl->transform.a));
        t.push(Json::of((double)gl->transform.b));
        t.push(Json::of((double)gl->transform.c));
        t.push(Json::of((double)gl->transform.d));
        t.push(Json::of((double)gl->transform.e));
        t.push(Json::of((double)gl->transform.f));
        j.set("transform", t);
        Json children = Json::array();
        for (auto& c : gl->children) children.push(layerToJson(c.get()));
        j.set("children", children);
    } else if (l->type == LayerType::Character) {
        const CharacterLayer* cl = (const CharacterLayer*)l;
        j.set("characterId", Json::of(cl->characterId));
        j.set("x", Json::of((double)cl->position.x));
        j.set("y", Json::of((double)cl->position.y));
        j.set("scale", Json::of((double)cl->scale));
        j.set("rotation", Json::of((double)cl->rotation));
    } else if (l->type == LayerType::Path) {
        const PathLayer* pl = (const PathLayer*)l;
        Json paths = Json::array();
        for (const PathStroke& s : pl->paths) {
            Json sj = Json::object();
            Json pts = Json::array();
            for (const Vec2& pt : s.pts) {
                Json pv = Json::array();
                pv.push(Json::of((double)pt.x));
                pv.push(Json::of((double)pt.y));
                pts.push(pv);
            }
            sj.set("points", pts);
            sj.set("width", Json::of((double)s.width));
            paths.push(sj);
        }
        j.set("paths", paths);
    }
    return j;
}

static std::shared_ptr<Layer> layerFromJson(const Json& j) {
    std::string type = j.str("type", "raster");
    std::shared_ptr<Layer> l;
    if (type == "raster") {
        auto rl = std::make_shared<RasterLayer>();
        const Json* frames = j.get("frames");
        if (frames && frames->isArray())
            for (const Json& f : frames->arrayValue) rl->frames[(int)f.numberValue].create(1, 1);
        l = rl;
    } else if (type == "group") {
        auto gl = std::make_shared<GroupLayer>();
        const Json* t = j.get("transform");
        if (t && t->isArray() && t->arrayValue.size() == 6) {
            gl->transform.a = (float)t->arrayValue[0].numberValue;
            gl->transform.b = (float)t->arrayValue[1].numberValue;
            gl->transform.c = (float)t->arrayValue[2].numberValue;
            gl->transform.d = (float)t->arrayValue[3].numberValue;
            gl->transform.e = (float)t->arrayValue[4].numberValue;
            gl->transform.f = (float)t->arrayValue[5].numberValue;
        }
        const Json* children = j.get("children");
        if (children && children->isArray())
            for (const Json& c : children->arrayValue) gl->children.push_back(layerFromJson(c));
        l = gl;
    } else if (type == "character") {
        auto cl = std::make_shared<CharacterLayer>();
        cl->characterId = j.str("characterId");
        cl->position = Vec2((float)j.num("x"), (float)j.num("y"));
        cl->scale = (float)j.num("scale", 1.0);
        cl->rotation = (float)j.num("rotation");
        l = cl;
    } else {
        auto pl = std::make_shared<PathLayer>();
        const Json* paths = j.get("paths");
        if (paths && paths->isArray())
            for (const Json& sj : paths->arrayValue) {
                PathStroke s;
                s.width = (float)sj.num("width", 2.0);
                const Json* pts = sj.get("points");
                if (pts && pts->isArray())
                    for (const Json& pv : pts->arrayValue)
                        if (pv.isArray() && pv.arrayValue.size() == 2)
                            s.pts.push_back(Vec2((float)pv.arrayValue[0].numberValue,
                                                 (float)pv.arrayValue[1].numberValue));
                pl->paths.push_back(s);
            }
        l = pl;
    }
    l->id = j.str("id");
    l->name = j.str("name");
    l->visible = j.boolean("visible", true);
    l->opacity = (float)j.num("opacity", 1.0);
    return l;
}

bool saveLep(const std::string& path, const Project& p, std::string* error) {
    ZipWriter zw;
    Json j = Json::object();
    j.set("format", Json::of(std::string("leptid-project")));
    j.set("version", Json::of(4));
    j.set("name", Json::of(p.name));
    j.set("fps", Json::of(p.fps));
    j.set("frameCount", Json::of(p.frameCount));
    j.set("worldW", Json::of(p.worldW));
    j.set("worldH", Json::of(p.worldH));
    Json cam = Json::object();
    cam.set("x", Json::of((double)p.camera.x));
    cam.set("y", Json::of((double)p.camera.y));
    cam.set("scale", Json::of((double)p.camera.scale));
    j.set("camera", cam);
    // animated camera track + playback state (v4)
    Json camTrack = Json::array();
    for (const CameraKey& k : p.cameraTrack.keys()) {
        Json kj = Json::object();
        kj.set("frame", Json::of(k.frame));
        kj.set("x", Json::of((double)k.cam.x));
        kj.set("y", Json::of((double)k.cam.y));
        kj.set("scale", Json::of((double)k.cam.scale));
        kj.set("ease", Json::of((int)k.ease));
        camTrack.push(kj);
    }
    j.set("cameraTrack", camTrack);
    j.set("playMode", Json::of((int)p.playMode));
    if (p.playStart >= 0) j.set("playStart", Json::of(p.playStart));
    if (p.playEnd >= 0) j.set("playEnd", Json::of(p.playEnd));
    Json layers = Json::array();
    for (auto& l : p.layers) layers.push(layerToJson(l.get()));
    j.set("layers", layers);
    Json chars = Json::array();
    for (auto& kv : p.characters) chars.push(Json::of(kv.first));
    j.set("characters", chars);
    j.set("activeLayer", Json::of(p.activeLayerId));
    zw.add("project.json", j.dump(2));

    // raster frames
    for (auto& l : p.layers) {
        if (l->type != LayerType::Raster) continue;
        const RasterLayer* rl = (const RasterLayer*)l.get();
        for (auto& kv : rl->frames) {
            if (kv.second.empty()) continue;
            auto png = savePngMemory(kv.second);
            zw.add("layers/" + l->id + "/frame_" + std::to_string(kv.first) + ".png", png);
        }
        if (l->type == LayerType::Raster && ((RasterLayer*)l.get())->frames.empty()) {
            // ensure group dirs exist implicitly — ZIP needs no directory entries
        }
    }
    // characters: full embedded copy (self-contained project)
    for (auto& kv : p.characters) {
        const Character& c = *kv.second;
        std::string base = "characters/" + kv.first;
        zw.add(base + ".json", characterToJson(c).dump(2));
        for (size_t i = 0; i < c.parts.size(); ++i) {
            if (c.parts[i].sprite.empty()) continue;
            auto png = savePngMemory(c.parts[i].sprite);
            zw.add(base + "/part_" + std::to_string(i) + ".png", png);
        }
    }
    auto bytes = zw.build();
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) { if (error) *error = "cannot write " + path; return false; }
    fwrite(bytes.data(), 1, bytes.size(), f);
    fclose(f);
    return true;
}

bool loadLep(const std::string& path, Project& out, std::string* error) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { if (error) *error = "cannot open " + path; return false; }
    std::vector<uint8_t> data;
    uint8_t buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) data.insert(data.end(), buf, buf + n);
    fclose(f);
    ZipReader zr;
    if (!zr.open(data)) { if (error) *error = "bad archive: " + zr.error(); return false; }
    std::string json;
    if (!zr.readText("project.json", json)) { if (error) *error = "missing project.json"; return false; }
    Json j;
    if (!Json::parse(json, j)) { if (error) *error = "corrupt project.json"; return false; }
    if (j.integer("version", 0) < 3) { if (error) *error = "unsupported .lep version"; return false; }
    out.name = j.str("name", "Untitled");
    out.fps = j.integer("fps", 24);
    out.frameCount = j.integer("frameCount", 48);
    out.worldW = j.integer("worldW", 4000);
    out.worldH = j.integer("worldH", 4000);
    const Json* cam = j.get("camera");
    if (cam && cam->isObject()) {
        out.camera.x = (float)cam->num("x", 2000);
        out.camera.y = (float)cam->num("y", 2000);
        out.camera.scale = (float)cam->num("scale", 1.0);
    }
    // animated camera track + playback state (v4; absent in v3 files)
    out.cameraTrack = CameraTrack();
    const Json* ct = j.get("cameraTrack");
    if (ct && ct->isArray())
        for (const Json& kj : ct->arrayValue) {
            Camera c;
            c.x = (float)kj.num("x", 2000);
            c.y = (float)kj.num("y", 2000);
            c.scale = (float)kj.num("scale", 1.0);
            Ease e = (Ease)kj.integer("ease", (int)Ease::EaseInOut);
            out.cameraTrack.setKey(kj.integer("frame", 0), c, e);
        }
    out.playMode = (PlayMode)j.integer("playMode", (int)PlayMode::Loop);
    out.playStart = j.integer("playStart", -1);
    out.playEnd = j.integer("playEnd", -1);
    out.activeLayerId = j.str("activeLayer");
    out.layers.clear();
    out.characters.clear();
    const Json* layers = j.get("layers");
    if (layers && layers->isArray())
        for (const Json& lj : layers->arrayValue) {
            auto l = layerFromJson(lj);
            // raster frames: load PNGs
            if (l->type == LayerType::Raster) {
                RasterLayer* rl = (RasterLayer*)l.get();
                for (auto& kv : rl->frames) {
                    std::string name = "layers/" + rl->id + "/frame_" + std::to_string(kv.first) + ".png";
                    std::vector<uint8_t> png;
                    if (zr.read(name, png))
                        loadPngMemory(png.data(), png.size(), rl->frames[kv.first]);
                }
            }
            out.layers.push_back(l);
        }
    // characters embedded in the project
    const Json* chars = j.get("characters");
    if (chars && chars->isArray())
        for (const Json& cj : chars->arrayValue) {
            std::string cid = cj.stringValue;
            std::string base = "characters/" + cid;
            std::string cjson;
            if (!zr.readText(base + ".json", cjson)) continue;
            Json cjj;
            if (!Json::parse(cjson, cjj)) continue;
            auto c = std::make_shared<Character>();
            if (!characterFromJson(cjj, *c, error)) continue;
            for (size_t i = 0; i < c->parts.size(); ++i) {
                std::string name = base + "/part_" + std::to_string(i) + ".png";
                std::vector<uint8_t> png;
                if (zr.read(name, png))
                    loadPngMemory(png.data(), png.size(), c->parts[i].sprite);
            }
            c->id = cid;
            out.characters[cid] = c;
        }
    if (out.activeLayerId.empty() && !out.layers.empty())
        out.activeLayerId = out.layers.back()->id;
    return true;
}

} // namespace leptid
