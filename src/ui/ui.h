// Leptid — dark-mode immediate-mode UI toolkit rendered onto our Image.
// Same software renderer as the canvas (one pipeline for everything).
#pragma once
#include <string>
#include <vector>
#include "core/image.h"

namespace leptid {

// embedded DejaVu fonts (generated into bin/fontdata.cpp by the build)
namespace embed {
extern const unsigned char DejaVuSans_font[];
extern unsigned int DejaVuSans_font_len;
extern const unsigned char DejaVuSansBold_font[];
extern unsigned int DejaVuSansBold_font_len;
}

// Dark theme palette
struct Theme {
    Color windowBg   = Color(24, 25, 30, 255);
    Color panelBg    = Color(33, 35, 42, 255);
    Color panelBg2   = Color(41, 43, 52, 255);
    Color border     = Color(58, 61, 74, 255);
    Color text       = Color(226, 228, 234, 255);
    Color textDim    = Color(150, 155, 170, 255);
    Color accent     = Color(91, 140, 255, 255);
    Color accentDim  = Color(60, 95, 180, 255);
    Color danger     = Color(220, 90, 90, 255);
    Color ok         = Color(80, 200, 120, 255);
    Color canvasBg   = Color(18, 19, 23, 255);
    Color timelineBg = Color(28, 30, 37, 255);
    Color keyframe   = Color(255, 196, 90, 255);
    Color playhead   = Color(91, 140, 255, 255);
    Color onionPrev  = Color(230, 70, 70, 255);
    Color onionNext  = Color(80, 230, 90, 255);
    static const Theme& get();
};

// Key codes (platform backends translate into these)
enum class Key {
    Invalid, Space, Left, Right, Up, Down, Enter, Escape, Delete, Backspace,
    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
    Ctrl, Shift, Tab, F1
};

struct InputEvent {
    enum class Type { None, MouseMove, MouseDown, MouseUp, Wheel, KeyDown, KeyUp, Char, Quit, Resize };
    Type type = Type::None;
    float x = 0, y = 0;          // mouse position (MouseMove/Down/Up/Wheel)
    float wheelDelta = 0;        // Wheel
    int button = 0;              // MouseDown/Up: 1 left, 2 middle, 3 right
    Key key = Key::Invalid;         // KeyDown/Up
    uint32_t codepoint = 0;      // Char
    int width = 0, height = 0;   // Resize
};

// Text rendering (baked glyph atlas via stb_truetype)
class Font {
public:
    bool load(const unsigned char* ttf, unsigned len, float pixelHeight);
    float height() const { return size_; }
    float textWidth(const std::string& text) const;
    void draw(Image& fb, const std::string& text, float x, float y, Color color) const;
    void drawBold(Image& fb, const std::string& text, float x, float y, Color color) const;

private:
    struct Glyph { float x0, y0, x1, y1; float advance; int atlasX, atlasY, w, h; };
    bool bake(const unsigned char* ttf, unsigned len, float pixelHeight, Image& atlas,
              std::vector<Glyph>& glyphs);
    void drawInternal(Image& fb, const std::string& text, float x, float y, Color c,
                      const Image& atlas, const std::vector<Glyph>& glyphs) const;
    float size_ = 0;
    Image atlas_;
    Image atlasBold_;
    std::vector<Glyph> glyphs_;
    std::vector<Glyph> glyphsBold_;
};

Font& uiFont();   // 13px UI font
Font& uiFontBig(); // 16px

// Immediate-mode UI context: one per frame over the whole window framebuffer.
class Ui {
public:
    void begin(Image* fb, float mouseX, float mouseY, bool leftDown, bool rightDown);
    // feed raw events (key/mouse edges); call before begin()
    void onEvent(const InputEvent& e);
    void end() { hot_ = 0; }

    bool hot(unsigned id) const { return hot_ == id; }
    bool active(unsigned id) const { return active_ == id; }
    unsigned active() const { return active_; }
    void setActive(unsigned id) { active_ = id; }
    void setHot(unsigned id) { hot_ = id; }
    bool leftDown() const { return leftDown_; }
    bool rightDown() const { return rightDown_; }
    Vec2 mouse() const { return Vec2(mx_, my_); }
    Vec2 mouseDelta() const { return Vec2(mx_ - prevMx_, my_ - prevMy_); }
    // True only on the frame the button was pressed (edge).
    bool pressed(unsigned id) const { return pressed_ == id; }
    bool released(unsigned id) const { return released_ == id; }
    bool wheel() const { return wheelDelta_ != 0; }
    float wheelDelta() const { return wheelDelta_; }
    bool keyPressed(Key k) const;
    bool keyDown(Key k) const { return keysDown_[(int)k]; }
    std::string charsTyped() const { return chars_; }
    void clearFrameState();

    // ---- widgets (draw + interact; return true on click) ----
    void panel(int x, int y, int w, int h, Color bg);
    void rect(int x, int y, int w, int h, Color c);
    void frame(int x, int y, int w, int h, Color c);
    void label(const std::string& text, int x, int y, Color c, bool bold = false);
    bool button(unsigned id, const std::string& text, int x, int y, int w, int h,
                bool highlighted = false);
    bool checkbox(unsigned id, const std::string& text, int x, int y, bool value);
    float slider(unsigned id, const std::string& label, int x, int y, int w,
                 float value, float lo, float hi);
    int listbox(unsigned id, int x, int y, int w, int h,
                const std::vector<std::string>& items, int selected, int* scroll);
    // returns true on Enter
    bool textbox(unsigned id, std::string& value, int x, int y, int w);

private:
    Image* fb_ = nullptr;
    float mx_ = 0, my_ = 0, prevMx_ = 0, prevMy_ = 0;
    bool leftDown_ = false, rightDown_ = false;
    unsigned hot_ = 0, active_ = 0, pressed_ = 0, released_ = 0;
    float wheelDelta_ = 0;
    bool keysDown_[(int)Key::F1 + 1] = {false};
    std::vector<Key> keysPressed_;
    std::string chars_;
    unsigned captured_ = 0; // textbox capture
};

bool inRect(float px, float py, int x, int y, int w, int h);

} // namespace leptid
