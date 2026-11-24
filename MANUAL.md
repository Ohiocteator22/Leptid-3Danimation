# Leptid — User Manual

Leptid is a free/open-source 2D animation application written in C++17 with zero
external dependencies. It combines **frame-by-frame drawing** (DA — Drawing
Animation) with **articulated, object-oriented animation** (OOA — Object-Oriented
Animation) in one dark-mode editor.

---

## 1. Starting Leptid

| Command | What it does |
| --- | --- |
| `leptid` | opens the editor (dark mode) |
| `leptid project.lep` | opens a saved project in the editor |
| `leptid --demo out.lep --gif out.gif --out render` | writes a demo project and renders it |
| `leptid --headless --export project.lep --out frames --gif out.gif --mp4 out.mp4` | renders without a window (scripts/CI) |
| `leptid --ui-shot shot.png project.lep` | saves one screenshot of the editor UI |

On Windows, build first (see the README's *Building on Windows* section), then
run `bin\leptid.exe` the same way.

## 2. The editor at a glance

```
+--------------------------------------------------------------+
| Leptid   New  Open  Save   Import .Lchar   Export  Onion ...  |
+---+--------------------------------------------------+-------+
| B |                                                  | Layers|
| E |                    CANVAS                        | Rig / |
| C |        (draw, pan, zoom, rig, cut)               | Pose  |
| K |                                                  |       |
| I |                                                  |       |
| P |                                                  |       |
+---+--------------------------------------------------+-------+
| Play  < >   frame 1/48  fps 24   onion   stabilizer   Audio  |
| timeline tracks ...                                          |
+--------------------------------------------------------------+
```

- **Left rail (B E C K I P)** selects the active tool; the size `- / +` buttons
  and the color swatches below set brush size and color.
- **Canvas** shows the composited scene through the editor view. Wheel = zoom,
  middle-drag = pan the view.
- **Layers panel** (top right): add raster/group/path layers, toggle visibility,
  adjust opacity, reorder, duplicate, delete.
- **Rig / Pose panel** (bottom right): bone list, FK/IK mode, pose keys, and the
  character-cutting controls. Shown when a character layer is selected.
- **Timeline** (bottom): playback, frame ruler (click/drag to scrub), layer
  tracks, pose-key diamonds, audio waveform strip.

### Keyboard map

| Key | Action |
| --- | --- |
| `Space` | play / pause |
| `←` / `→` | previous / next frame |
| `B` `E` | brush / eraser |
| `C` | camera tool (drag = move project camera, wheel = zoom camera) |
| `K` | bone tool (FK: click a bone, drag to rotate) |
| `I` | IK tool (drag a hand/foot; pose key written on release) |
| `P` | cut tool (drag rectangles over T-pose artwork) |
| `Ctrl+Z` / `Ctrl+Y` | undo / redo |
| `Ctrl+S` / `Ctrl+O` | save / open project |
| `Ctrl+1` | import a `.Lchar` character as a layer |

## 3. DA — drawing animation workflow

1. Select a raster layer, pick a color and brush size, draw.
2. Move one frame forward (`→`) and draw the next pose. Enable **Onion** to see
   the previous frame tinted red (~30 % opacity) and the next frame tinted green.
3. The **stabilizer** slider smooths finished strokes: raw input is passed
   through a moving average, then resampled along a Catmull-Rom spline. 0 = off,
   10 = very smooth.
4. Frames hold per layer; an unpainted frame shows nothing for that layer.
5. Groups: `+Group` creates a transform group — children move/rotate/scale with
   the group. Path layers draw motion-path overlays.

The **camera tool (C)** moves the project camera — an 800×600 world-space
viewport used for **export**. The editor view (wheel/pan) is just for looking
around; the camera rectangle is drawn while the tool is active so you can frame
the shot you will export.

## 4. OOA — characters and rigs

### 4.1 Building a character by cutting (tool P)

1. Import or draw a **T-pose** figure on a raster layer (flat background color
   works best; white is treated as the removable key color).
2. Select the cut tool `P`. Drag a rectangle over the **torso**. The cut slot
   advances automatically: torso → head → armL_upper → armL_lower →
   armR_upper → armR_lower → legL_upper → legL_lower → legR_upper → legR_lower.
   You can cycle slots in the Rig panel (`<` / `>`); single-segment slots
   (`armL`, `armR`, `legL`, `legR`) are supported if you did not cut upper/lower.
3. Click **Build Character**. Leptid extracts each part (preserving source pixel
   coordinates and transparency), removes the background color, sets pivots
   (limbs top-center, torso/head bottom-center), builds a humanoid skeleton with
   anatomical offsets (arms at the shoulders, legs at the hips) and bend limits
   (elbows and knees only fold one way), and adds the character layer.

### 4.2 Animating with FK and IK

- **FK (bone tool K):** click a bone, drag to rotate it. A pose key is written
  at the current frame.
- **IK (IK tool I):** drag a hand or foot. The stable solver moves the limb
  toward your cursor — the arm glides, never flings:
  - two-bone chains (arm/leg) use a closed-form analytic solver;
  - longer chains use damped FABRIK with a capped iteration count;
  - every solve is clamped to ≤ 24° per joint and every joint respects its
    bend limits, so targets can never whip a limb through the body;
  - the elbow keeps its bend side; if the hinted side is anatomically
    impossible the solver flips to the legal arc.
- **AI Assist** (optional): if a local Ollama server is running you can ask for
  pose/animation guidance. The assistant knows this manual. It is a pure
  convenience feature — everything works without it.

### 4.3 Pose keys and tweening

Poses are keyed as whole-pose keyframes (frame + pose + easing). Between two
keys the rig tweens with the chosen easing (default ease-in-out); angles take
the shortest path. A bone missing from a key is treated as at rest there, so
partial keys still tween. `Key Pose` writes the current pose at the current
frame; `Del Key` removes it. The timeline shows diamonds for every key; the
project's end frame is the **end frame** of the last animation (not the key
count), and `Sync` in the layers panel extends the timeline to fit.

## 5. Audio

`Import Audio` in the timeline loads a WAV directly; MP3/OGG are transcoded with
ffmpeg. The clip's waveform appears under the tracks and the timeline can sync
its length to the audio. v0.1 uses audio for **timing only** — sound is not
played back yet (no platform audio backend; see README limitations).

## 6. File formats

- **`.lep` version 3** — the project file: a ZIP archive containing
  `project.json` (fps, frames, camera, layers, character list), raster frames as
  `layers/<id>/frame_<n>.png`, and each character embedded fully
  (`characters/<id>.json` + `characters/<id>/part_<i>.png`), so projects are
  self-contained and survive moving the character files.
- **`.Lchar`** — a rigged character asset (same ZIP structure for one character)
  that can be imported into any project (`Import .Lchar` or `Ctrl+1`).
- Everything is written with Leptid's own ZIP/DEFLATE/JSON code (verified
  byte-for-byte against standard zip/python tooling in the test suite).

## 7. Exporting

`Export` opens the dialog with three outputs:

- **PNG sequence** — `frame_0000.png …` rendered through the project camera
  (transparency preserved),
- **animated GIF** — Leptid's own GIF89a encoder with a median-cut palette,
- **MP4** — piped through ffmpeg (H.264); skipped with a clear message when
  ffmpeg is not installed.

The demo project (`leptid --demo`) produces `demo/hello_leptid.lep`,
`demo/hello.gif`, `demo/hello.mp4` and `demo/render/frame_*.png`.

## 8. Tips

- Undo covers strokes, layer ops and pose keys (200 steps).
- Playback speed = project fps; change it in the timeline readout by saving and
  editing `project.json` in the `.lep` archive (any zip tool can open it).
- Characters are re-usable: save one with `Save .Lchar` and import it elsewhere.
- If the IK feels too damped when dragging fast, drag again — the per-solve
  clamp is what keeps limbs from flying; repeated solves converge to the cursor.
