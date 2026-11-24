// Leptid — layer model: raster (DA frames), group/transform, character (OOA in DA),
// and path (motion paths) layers.
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "core/image.h"
#include "core/math.h"

namespace leptid {

enum class LayerType { Raster, Group, Character, Path };

struct Layer {
    LayerType type;
    std::string id;         // stable id ("L1", "L2", ...)
    std::string name;
    bool visible = true;
    float opacity = 1.0f;
    Layer(LayerType t) : type(t) {}
    virtual ~Layer() {}
};

struct PathPoint { Vec2 pos; };
struct PathStroke {
    std::vector<Vec2> pts;
    Color color = Color(120, 200, 255, 255);
    float width = 2.0f;
};

class RasterLayer : public Layer {
public:
    std::map<int, Image> frames; // frame index -> raster (missing = empty frame)
    explicit RasterLayer() : Layer(LayerType::Raster) {}
    Image* frameAt(int f) {
        auto it = frames.find(f);
        return it == frames.end() ? nullptr : &it->second;
    }
    Image& ensureFrame(int f) {
        auto it = frames.find(f);
        if (it == frames.end()) {
            Image& img = frames[f];
            img.create(1, 1); // size fixed on first paint (canvas size)
            return img;
        }
        return it->second;
    }
};

class GroupLayer : public Layer {
public:
    std::vector<std::shared_ptr<Layer>> children;
    Mat3 transform; // move/rotate/scale applied to children in world space
    explicit GroupLayer() : Layer(LayerType::Group) {}
};

class CharacterLayer : public Layer {
public:
    std::string characterId;
    Vec2 position;      // character root world position (layer placement)
    float scale = 1.0f;
    float rotation = 0.0f;
    explicit CharacterLayer() : Layer(LayerType::Character) {}
};

class PathLayer : public Layer {
public:
    std::vector<PathStroke> paths;
    explicit PathLayer() : Layer(LayerType::Path) {}
};

} // namespace leptid
