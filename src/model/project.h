// Leptid — project model: fps, timeline, layers, characters, camera, audio.
// Centralizes coordinate-space conversion (blueprint rule: never scatter it).
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "core/math.h"
#include "model/layer.h"
#include "model/undo.h"
#include "rig/character.h"

namespace leptid {

// Project-level camera (blueprint §7-D): a world-space viewport, not a layer.
// World is 4000x4000; default output/viewport 800x600; camera stores x, y, scale.
struct Camera {
    float x = 2000, y = 2000; // world-space center of the viewport
    float scale = 1.0f;       // output px per world px
};

struct ViewTransform {
    float cx = 2000, cy = 2000; // world point at the viewport center
    float scale = 1.0f;         // screen px per world px
    int viewW = 800, viewH = 600;

    static ViewTransform fromCamera(const Camera& cam, int w, int h) {
        ViewTransform v;
        v.cx = cam.x; v.cy = cam.y; v.scale = cam.scale; v.viewW = w; v.viewH = h;
        return v;
    }
    Mat3 worldToScreen() const {
        return Mat3::translation(viewW * 0.5f, viewH * 0.5f)
             * Mat3::scale(scale)
             * Mat3::translation(-cx, -cy);
    }
    Mat3 screenToWorld() const { return worldToScreen().inverse(); }
    Vec2 worldToScreenPoint(const Vec2& w) const { return worldToScreen().apply(w); }
    Vec2 screenToWorldPoint(const Vec2& s) const { return screenToWorld().apply(s); }
};

// Camera animation (inspired by Synfig's "anything can animate"): the project
// camera can be keyed like a pose track. Each key stores x/y/scale plus the
// Synfig-style ease that shapes the segment leaving that key.
struct CameraKey {
    int frame = 0;
    Camera cam;
    Ease ease = Ease::EaseInOut;
};

class CameraTrack {
public:
    void setKey(int frame, const Camera& cam, Ease ease = Ease::EaseInOut);
    void removeKey(int frame);                       // no-op if absent
    bool hasKey(int frame) const;
    bool getKey(int frame, CameraKey& out) const;
    void moveKey(int fromFrame, int toFrame);
    int keyCount() const { return (int)keys_.size(); }
    const std::vector<CameraKey>& keys() const { return keys_; }

    // Before the first / after the last key → nearest key held. Between keys →
    // eased interpolation of x, y and scale.
    Camera sample(int frame) const;

private:
    std::vector<CameraKey> keys_;                    // sorted by frame
};

// Playback behavior (OpenToonz flipbook): loop, ping-pong, or play once.
enum class PlayMode { Loop, PingPong, Once };

// Pure playhead step inside [0, count): unit-testable without the editor loop.
// Ping-pong reverses via `forward` (in/out). "Once" clamps to the last frame.
int advancePlayhead(int frame, int count, PlayMode mode, bool& forward);

// Audio clip attached to the DA timeline (import WAV directly; MP3 via ffmpeg).
struct AudioClip {
    std::string path;
    int sampleRate = 0;
    int channels = 0;
    std::vector<float> samples; // interleaved; loaded for waveform + timing
    double durationSec() const {
        if (sampleRate <= 0 || channels <= 0) return 0;
        return (double)samples.size() / (sampleRate * channels);
    }
    int durationFrames(int fps) const { return (int)std::ceil(durationSec() * fps); }
};

class Project {
public:
    std::string name = "Untitled";
    int fps = 24;
    int frameCount = 48;
    int worldW = 4000, worldH = 4000;
    Camera camera;                                   // static camera (also the edit target)
    CameraTrack cameraTrack;                         // animated camera keys (may be empty)
    PlayMode playMode = PlayMode::Loop;              // session playback behavior
    int playStart = -1, playEnd = -1;                // play range frames; -1 = unset (OpenToonz markers)
    std::vector<std::shared_ptr<Layer>> layers; // index 0 = bottom, last = top
    std::map<std::string, std::shared_ptr<Character>> characters;
    std::shared_ptr<AudioClip> audio;
    CommandStack commands;
    std::string activeLayerId;   // editing focus (drawing / onion skin / rig panel)

    static std::shared_ptr<Project> createDefault();
    static std::string newLayerId();

    // Layer helpers
    Layer* findLayer(const std::string& id) const;
    RasterLayer* activeRasterLayer() const;
    void addLayer(std::shared_ptr<Layer> layer, int index = -1); // -1 = top
    std::shared_ptr<Layer> removeLayer(const std::string& id);   // returns removed
    void moveLayer(const std::string& id, int newIndex);
    std::shared_ptr<Layer> duplicateLayer(const std::string& id); // deep copy for raster
    int layerIndex(const std::string& id) const;

    // Characters
    Character* findCharacter(const std::string& id) const;
    std::string addCharacter(std::shared_ptr<Character> c);

    // Timeline sync (blueprint §7-C): effective animation end frame from any
    // character layer — the END FRAME, not the key count.
    int effectiveTimelineEnd() const;
    void syncTimelineToCharacters(); // extends frameCount if a character needs more

    // Camera effective for a frame: the animated track when it has keys,
    // otherwise the static camera. One call site for preview, playback, export.
    Camera cameraAtFrame(int frame) const;
};

} // namespace leptid
