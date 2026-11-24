// Leptid — .Lchar implementation.
#include "rig/charser.h"
#include "formats/json.h"
#include "formats/pngio.h"
#include "formats/zipcodec.h"

namespace leptid {

Json characterToJson(const Character& c) {
    Json j = Json::object();
    j.set("format", Json::of(std::string("Lchar")));
    j.set("version", Json::of(1));
    j.set("name", Json::of(c.name));
    Json parts = Json::array();
    for (const CharacterPart& p : c.parts) {
        Json pj = Json::object();
        pj.set("name", Json::of(p.name));
        pj.set("pivotX", Json::of((double)(int)p.pivot.x));
        pj.set("pivotY", Json::of((double)(int)p.pivot.y));
        pj.set("w", Json::of(p.sprite.w));
        pj.set("h", Json::of(p.sprite.h));
        parts.push(pj);
    }
    j.set("parts", parts);
    Json bones = Json::array();
    for (const Bone& b : c.skeleton.bones) {
        Json bj = Json::object();
        bj.set("id", Json::of(b.id));
        bj.set("name", Json::of(b.name));
        bj.set("parent", Json::of(b.parentId));
        bj.set("length", Json::of((double)b.length));
        bj.set("restAngle", Json::of((double)b.restAngle));
        bj.set("offsetX", Json::of((double)b.offset.x));
        bj.set("offsetY", Json::of((double)b.offset.y));
        bj.set("hasLimits", Json::of(b.hasLimits));
        bj.set("minAngle", Json::of((double)b.minAngle));
        bj.set("maxAngle", Json::of((double)b.maxAngle));
        bj.set("spritePart", Json::of(b.spritePart));
        bj.set("anchorX", Json::of((double)b.spriteAnchor.x));
        bj.set("anchorY", Json::of((double)b.spriteAnchor.y));
        bj.set("dirAngle", Json::of((double)b.spriteDirAngle));
        bj.set("spriteLen", Json::of((double)b.spriteLength));
        bones.push(bj);
    }
    Json skel = Json::object();
    skel.set("bones", bones);
    j.set("skeleton", skel);
    Json order = Json::array();
    for (int id : c.drawOrder) order.push(Json::of(id));
    j.set("drawOrder", order);
    Json keys = Json::array();
    for (const PoseKey& k : c.track.keys()) {
        Json kj = Json::object();
        kj.set("frame", Json::of(k.frame));
        kj.set("ease", Json::of((int)k.ease));
        kj.set("rootX", Json::of((double)k.pose.rootPos.x));
        kj.set("rootY", Json::of((double)k.pose.rootPos.y));
        kj.set("rootAngle", Json::of((double)k.pose.rootAngle));
        Json angles = Json::array();
        for (auto& kv : k.pose.angles) {
            Json av = Json::array();
            av.push(Json::of(kv.first));
            av.push(Json::of((double)kv.second));
            angles.push(av);
        }
        kj.set("angles", angles);
        keys.push(kj);
    }
    j.set("keys", keys);
    return j;
}

bool characterFromJson(const Json& j, Character& out, std::string* error) {
    auto fail = [&](const char* msg) { if (error) *error = msg; return false; };
    if (j.str("format") != "Lchar") return fail("not an Lchar file");
    out.name = j.str("name", "Character");
    out.parts.clear();
    out.skeleton.bones.clear();
    out.track = PoseTrack();
    out.drawOrder.clear();
    const Json* parts = j.get("parts");
    if (!parts || !parts->isArray()) return fail("missing parts");
    for (const Json& pj : parts->arrayValue) {
        CharacterPart p;
        p.name = pj.str("name");
        p.pivot = Vec2((float)pj.num("pivotX"), (float)pj.num("pivotY"));
        out.parts.push_back(std::move(p));
    }
    const Json* skel = j.get("skeleton");
    if (!skel) return fail("missing skeleton");
    const Json* bones = skel->get("bones");
    if (!bones || !bones->isArray()) return fail("missing bones");
    for (const Json& bj : bones->arrayValue) {
        Bone b;
        b.id = bj.integer("id", -1);
        b.name = bj.str("name");
        b.parentId = bj.integer("parent", -1);
        b.length = (float)bj.num("length", 60);
        b.restAngle = (float)bj.num("restAngle");
        b.offset = Vec2((float)bj.num("offsetX"), (float)bj.num("offsetY"));
        b.hasLimits = bj.boolean("hasLimits", false);
        b.minAngle = (float)bj.num("minAngle", -3.14159);
        b.maxAngle = (float)bj.num("maxAngle", 3.14159);
        b.spritePart = bj.integer("spritePart", -1);
        b.spriteAnchor = Vec2((float)bj.num("anchorX"), (float)bj.num("anchorY"));
        b.spriteDirAngle = (float)bj.num("dirAngle");
        b.spriteLength = (float)bj.num("spriteLen");
        out.skeleton.bones.push_back(b);
    }
    const Json* order = j.get("drawOrder");
    if (order && order->isArray())
        for (const Json& oj : order->arrayValue) out.drawOrder.push_back((int)oj.numberValue);
    const Json* keys = j.get("keys");
    if (keys && keys->isArray())
        for (const Json& kj : keys->arrayValue) {
            PoseKey k;
            k.frame = kj.integer("frame");
            k.ease = (Ease)kj.integer("ease", (int)Ease::EaseInOut);
            k.pose.rootPos = Vec2((float)kj.num("rootX"), (float)kj.num("rootY"));
            k.pose.rootAngle = (float)kj.num("rootAngle");
            const Json* angles = kj.get("angles");
            if (angles && angles->isArray())
                for (const Json& av : angles->arrayValue)
                    if (av.isArray() && av.arrayValue.size() == 2)
                        k.pose.angles[(int)av.arrayValue[0].numberValue] = (float)av.arrayValue[1].numberValue;
            out.track.setKey(k.frame, k.pose, k.ease);
        }
    if (out.drawOrder.size() != out.skeleton.bones.size()) out.ensureDrawOrder();
    return true;
}

bool saveLchar(const std::string& path, const Character& c, std::string* error) {
    ZipWriter zw;
    zw.add("character.json", characterToJson(c).dump(2));
    for (size_t i = 0; i < c.parts.size(); ++i) {
        if (c.parts[i].sprite.empty()) continue;
        auto png = savePngMemory(c.parts[i].sprite);
        zw.add("parts/part_" + std::to_string(i) + ".png", png);
    }
    auto bytes = zw.build();
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) { if (error) *error = "cannot write " + path; return false; }
    fwrite(bytes.data(), 1, bytes.size(), f);
    fclose(f);
    return true;
}

bool loadLcharMemory(const uint8_t* data, size_t len, Character& out, std::string* error) {
    ZipReader zr;
    if (!zr.open(std::vector<uint8_t>(data, data + len))) {
        if (error) *error = "bad archive: " + zr.error();
        return false;
    }
    std::string json;
    if (!zr.readText("character.json", json)) {
        if (error) *error = "missing character.json";
        return false;
    }
    Json j;
    if (!Json::parse(json, j) || !characterFromJson(j, out, error)) return false;
    for (size_t i = 0; i < out.parts.size(); ++i) {
        std::string name = "parts/part_" + std::to_string(i) + ".png";
        std::vector<uint8_t> png;
        if (zr.read(name, png))
            loadPngMemory(png.data(), png.size(), out.parts[i].sprite);
    }
    return true;
}

bool loadLchar(const std::string& path, Character& out, std::string* error) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { if (error) *error = "cannot open " + path; return false; }
    std::vector<uint8_t> data;
    uint8_t buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) data.insert(data.end(), buf, buf + n);
    fclose(f);
    return loadLcharMemory(data.data(), data.size(), out, error);
}

} // namespace leptid
