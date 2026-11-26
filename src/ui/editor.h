// Leptid — editor session: dark-mode application shell, canvas, tools, panels.
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "model/compositor.h"
#include "model/brush.h"
#include "model/lepser.h"
#include "rig/cutter.h"
#include "ui/ui.h"

namespace leptid {

class Editor {
public:
    Editor();
    ~Editor();

    bool openProject(const std::string& path);
    bool saveProject(const std::string& path);
    // One animation frame: consume input, update, draw everything.
    void update(Ui& ui, int width, int height, double dt);
    Image& framebuffer() { return fb_; }
    bool wantsQuit() const { return quit_; }
    void setQuit() { quit_ = true; }

    // headless UI capture (used by --ui-shot)
    void renderOnce(Ui& ui, int width, int height);

private:
    enum class Tool { Brush, Eraser, Camera, Bone, IK, Cut };
    enum class Dialog { None, Open, SaveLep, SaveLchar, ImportChar, ImportAudio, Export, About, Help, AI };

    // --- state ---
    Image fb_;
    std::shared_ptr<Project> project_;
    std::string projectPath_;
    bool quit_ = false;
    int frame_ = 0;
    bool playing_ = false;
    bool playForward_ = true;      // ping-pong direction
    double playAccum_ = 0;
    int statusFlash_ = 0;
    std::string statusText_ = "Ready";

    // view
    ViewTransform view_;
    Vec2 panStartWorld_;
    bool panning_ = false;

    // tools
    Tool tool_ = Tool::Brush;
    BrushSettings brush_;
    BrushId eraserId_ = BrushId::EraserHard;   // eraser variant picked in the rail
    int stabilizer_ = 3;
    int brushColorIdx_ = 0;
    std::vector<Color> palette_;

    // layout metrics (single spacing system for the whole shell)
    static const int kRailW = 152;   // tool + brush rail
    static const int kRightW = 300;  // right column
    static const int kTimelineH = 184;
    static const int kStatusH = 24;
    static const int kTopH = 40;
    static const int kGap = 8;       // standard gap
    static const int kPad = 10;      // standard panel padding

    // camera animation (Synfig-style animate mode)
    bool animAutoKey_ = false;     // ON: camera drags auto-key at the playhead
    int selCamKey_ = -1;           // frame of the selected camera key (-1 = none)

    // onion + overlays
    bool onionOn_ = true;
    int onionBefore_ = 1, onionAfter_ = 1;
    float onionOpacity_ = 0.3f;
    bool showBones_ = true;
    bool showCameraRect_ = true;
    bool advancedTimeline_ = false;

    BrushEngine engine_;  // brush presets + stamping

    // stroke
    std::vector<Vec2> rawStroke_;
    bool stroking_ = false;
    Image strokeSnapshot_;
    // live-preview bookkeeping: region of the layer the in-progress stroke
    // currently occupies (restored from the snapshot each frame so the
    // smoothed path is repainted over a clean base — no flow buildup)
    IRect liveBounds_{0, 0, -1, -1};
    bool liveValid_ = false;

    // rig editing
    std::string selectedCharacter_; // character id
    int selectedBone_ = -1;
    bool boneDragging_ = false;
    std::vector<int> ikChain_;
    bool ikDragging_ = false;
    Pose poseBeforeDrag_;

    // character cutting
    std::vector<CutSpec> cuts_;
    std::string cutSlot_;
    Vec2 cutDragStart_;
    bool cutDragging_ = false;
    int cutSlotIdx_ = 0;
    std::vector<std::string> cutSlots_;

    // panels
    int layersScroll_ = 0;
    int timelineScroll_ = 0;
    int rigScroll_ = 0;
    unsigned widgetCounter_ = 1;
    unsigned wid() { return ++widgetCounter_; }

    // dialogs
    Dialog dialog_ = Dialog::None;
    std::vector<std::string> dialogEntries_;
    std::string dialogDir_ = ".";
    std::string dialogFile_;
    int dialogScroll_ = 0;
    std::string dialogError_;
    // export dialog fields
    bool expPng_ = true, expGif_ = true, expMp4_ = false;
    std::string expDir_ = "render", expGifName_ = "out.gif", expMp4Name_ = "out.mp4";
    std::string expStatus_;
    // optional AI assistant
    std::string aiHost_ = "http://localhost";
    std::string aiModel_ = "llava";
    std::string aiQuestion_ = "How do I animate a walk cycle?";
    std::string aiReply_;
    std::string aiStatus_;

    // audio (todo: waveform strip)
    bool audioStripVisible_ = false;

    // helpers
    void drawTopBar(Ui& ui, int w);
    void drawToolbar(Ui& ui, int x, int y, int h);
    void drawCanvas(Ui& ui, int x, int y, int w, int h);
    void drawLayersPanel(Ui& ui, int x, int y, int w, int h);
    void drawRigPanel(Ui& ui, int x, int y, int w, int h);
    void drawTimeline(Ui& ui, int x, int y, int w, int h);
    void drawDialog(Ui& ui, int w, int h);
    void drawStatus(Ui& ui, int w, int h);
    void drawLogo(Ui& ui, int x, int y, int px);
    void drawEye(Ui& ui, int x, int y, bool visible, unsigned id, bool* toggled);
    void brushBar(Ui& ui, int x, int y, int w);  // brush preset grid inside the rail
    // centered text inside a rect (kills misaligned labels)
    void centerLabel(const std::string& text, int x, int y, int w, int h, Color c, bool bold = false);

    void handleShortcuts(Ui& ui);
    void handleCanvasInput(Ui& ui, int x, int y, int w, int h);
    void openDialog(Dialog d);
    void refreshDialogEntries();
    std::string pickFileFromDialog();
    void doExport();
    void commitStroke();
    void buildCutCharacter();
    Character* activeCharacter();
    CharacterLayer* activeCharacterLayer();
    void selectCharacterAt(const Vec2& world);
    void setStatus(const std::string& s) { statusText_ = s; statusFlash_ = 180; }
};

} // namespace leptid
