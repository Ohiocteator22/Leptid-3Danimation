// Leptid — project implementation.
#include "model/project.h"
#include <cstdlib>

namespace leptid {

static int gLayerCounter = 0;

std::string Project::newLayerId() {
    return "L" + std::to_string(++gLayerCounter);
}

std::shared_ptr<Project> Project::createDefault() {
    auto p = std::make_shared<Project>();
    auto raster = std::make_shared<RasterLayer>();
    raster->id = Project::newLayerId();
    raster->name = "Layer 1";
    p->layers.push_back(raster);
    p->activeLayerId = raster->id;
    p->camera = Camera();
    return p;
}

Layer* Project::findLayer(const std::string& id) const {
    for (auto& l : layers) if (l->id == id) return l.get();
    return nullptr;
}

RasterLayer* Project::activeRasterLayer() const {
    Layer* l = findLayer(activeLayerId);
    if (l && l->type == LayerType::Raster) return (RasterLayer*)l;
    // fall back to the topmost raster layer
    for (auto it = layers.rbegin(); it != layers.rend(); ++it)
        if ((*it)->type == LayerType::Raster) return (RasterLayer*)it->get();
    return nullptr;
}

void Project::addLayer(std::shared_ptr<Layer> layer, int index) {
    if (!layer) return;
    if (index < 0 || index > (int)layers.size()) index = (int)layers.size();
    layers.insert(layers.begin() + index, layer);
    activeLayerId = layer->id;
}

std::shared_ptr<Layer> Project::removeLayer(const std::string& id) {
    for (size_t i = 0; i < layers.size(); ++i)
        if (layers[i]->id == id) {
            auto removed = layers[i];
            layers.erase(layers.begin() + i);
            if (activeLayerId == id && !layers.empty())
                activeLayerId = layers.back()->id;
            return removed;
        }
    return nullptr;
}

void Project::moveLayer(const std::string& id, int newIndex) {
    int idx = layerIndex(id);
    if (idx < 0) return;
    if (newIndex < 0) newIndex = 0;
    if (newIndex >= (int)layers.size()) newIndex = (int)layers.size() - 1;
    auto l = layers[(size_t)idx];
    layers.erase(layers.begin() + idx);
    layers.insert(layers.begin() + newIndex, l);
}

int Project::layerIndex(const std::string& id) const {
    for (size_t i = 0; i < layers.size(); ++i)
        if (layers[i]->id == id) return (int)i;
    return -1;
}

static std::shared_ptr<Layer> cloneLayerShallow(const Layer* src);

static std::shared_ptr<Layer> cloneLayer(const Layer* src) {
    if (src->type == LayerType::Raster) {
        auto dst = std::make_shared<RasterLayer>();
        dst->frames = ((const RasterLayer*)src)->frames; // full frame copy
        return dst;
    }
    if (src->type == LayerType::Group) {
        auto dst = std::make_shared<GroupLayer>();
        const auto* g = (const GroupLayer*)src;
        dst->transform = g->transform;
        for (auto& c : g->children) dst->children.push_back(cloneLayerShallow(c.get()));
        return dst;
    }
    if (src->type == LayerType::Character) {
        auto dst = std::make_shared<CharacterLayer>();
        const auto* c = (const CharacterLayer*)src;
        dst->characterId = c->characterId;
        dst->position = c->position;
        dst->scale = c->scale;
        dst->rotation = c->rotation;
        return dst;
    }
    auto dst = std::make_shared<PathLayer>();
    dst->paths = ((const PathLayer*)src)->paths;
    return dst;
}

static std::shared_ptr<Layer> cloneLayerShallow(const Layer* src) {
    auto dst = cloneLayer(src);
    dst->name = src->name;
    dst->visible = src->visible;
    dst->opacity = src->opacity;
    return dst;
}

std::shared_ptr<Layer> Project::duplicateLayer(const std::string& id) {
    Layer* src = findLayer(id);
    if (!src) return nullptr;
    auto copy = cloneLayerShallow(src);
    copy->id = newLayerId();
    copy->name = src->name + " copy";
    int idx = layerIndex(id);
    addLayer(copy, idx + 1);
    return copy;
}

Character* Project::findCharacter(const std::string& id) const {
    auto it = characters.find(id);
    return it == characters.end() ? nullptr : it->second.get();
}

std::string Project::addCharacter(std::shared_ptr<Character> c) {
    if (!c) return "";
    if (c->id.empty()) {
        int n = (int)characters.size() + 1;
        c->id = "char" + std::to_string(n);
        while (characters.count(c->id)) {
            ++n;
            c->id = "char" + std::to_string(n);
        }
    }
    std::string id = c->id;
    characters[id] = c;
    return id;
}

int Project::effectiveTimelineEnd() const {
    int end = 0;
    for (auto& l : layers) {
        if (l->type == LayerType::Character) {
            const CharacterLayer* cl = (const CharacterLayer*)l.get();
            const Character* c = findCharacter(cl->characterId);
            if (c) end = std::max(end, c->animationEndFrame() + 1); // end FRAME (a key on frame N occupies frame N)
        } else if (l->type == LayerType::Raster) {
            const RasterLayer* rl = (const RasterLayer*)l.get();
            for (auto& kv : rl->frames) end = std::max(end, kv.first + 1);
        }
    }
    if (audio) end = std::max(end, audio->durationFrames(fps));
    return end;
}

void Project::syncTimelineToCharacters() {
    int end = effectiveTimelineEnd();
    if (end > frameCount) frameCount = end;
}

Camera Project::cameraAtFrame(int frame) const {
    if (cameraTrack.keyCount() > 0) return cameraTrack.sample(frame);
    return camera;
}

// ---------------- camera track ----------------
void CameraTrack::setKey(int frame, const Camera& cam, Ease ease) {
    for (CameraKey& k : keys_)
        if (k.frame == frame) { k.cam = cam; k.ease = ease; return; }
    CameraKey k;
    k.frame = frame;
    k.cam = cam;
    k.ease = ease;
    keys_.push_back(k);
    std::sort(keys_.begin(), keys_.end(),
              [](const CameraKey& a, const CameraKey& b) { return a.frame < b.frame; });
}

void CameraTrack::removeKey(int frame) {
    keys_.erase(std::remove_if(keys_.begin(), keys_.end(),
                               [frame](const CameraKey& k) { return k.frame == frame; }),
                keys_.end());
}

bool CameraTrack::hasKey(int frame) const {
    for (const CameraKey& k : keys_) if (k.frame == frame) return true;
    return false;
}

bool CameraTrack::getKey(int frame, CameraKey& out) const {
    for (const CameraKey& k : keys_)
        if (k.frame == frame) { out = k; return true; }
    return false;
}

void CameraTrack::moveKey(int fromFrame, int toFrame) {
    for (CameraKey& k : keys_)
        if (k.frame == fromFrame) k.frame = toFrame;
    std::sort(keys_.begin(), keys_.end(),
              [](const CameraKey& a, const CameraKey& b) { return a.frame < b.frame; });
}

Camera CameraTrack::sample(int frame) const {
    if (keys_.empty()) return Camera();
    if (frame <= keys_.front().frame) return keys_.front().cam;
    if (frame >= keys_.back().frame) return keys_.back().cam;
    for (size_t i = 0; i + 1 < keys_.size(); ++i) {
        const CameraKey& a = keys_[i];
        const CameraKey& b = keys_[i + 1];
        if (frame >= a.frame && frame <= b.frame) {
            float span = (float)(b.frame - a.frame);
            float t = span > 0.0f ? (float)(frame - a.frame) / span : 1.0f;
            t = applyEase(a.ease, t); // the leaving key shapes the segment
            Camera c;
            c.x = a.cam.x + (b.cam.x - a.cam.x) * t;
            c.y = a.cam.y + (b.cam.y - a.cam.y) * t;
            c.scale = a.cam.scale * std::pow(b.cam.scale / a.cam.scale, t); // zoom is multiplicative
            return c;
        }
    }
    return keys_.back().cam;
}

// ---------------- playback ----------------
int advancePlayhead(int frame, int count, PlayMode mode, bool& forward) {
    if (count <= 0) return 0;
    switch (mode) {
        case PlayMode::Loop:
            return (frame + 1) % count;
        case PlayMode::PingPong: {
            if (count == 1) return 0;
            int next = frame + (forward ? 1 : -1);
            if (next >= count) { next = count - 2; forward = false; }
            else if (next < 0) { next = 1; forward = true; }
            return next < 0 ? 0 : next;
        }
        case PlayMode::Once:
            return std::min(frame + 1, count - 1);
    }
    return frame;
}

} // namespace leptid
