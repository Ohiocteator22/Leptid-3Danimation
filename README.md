# Leptid

**Leptid** is a free/open-source 2D animation application — frame-by-frame
drawing (DA) plus articulated, object-oriented animation (OOA) — written in
**C++17 with zero external dependencies** (a compiler is the only build
requirement; ffmpeg is optional for MP4 export, Ollama optional for the AI
assistant).

Everything here is implemented from scratch in this repository: the software
renderer, the dark-mode UI toolkit, the ZIP/DEFLATE/JSON file stack, the GIF
encoder, the rig/IK engine and the platform backends. The only vendored files
are the public-domain `stb_image`, `stb_image_write` and `stb_truetype` headers
and the freely-licensed DejaVu fonts (see *Licenses*).

> This build supersedes the old Python/PySide6 blueprint: all features from that
> roadmap — including the previously-dropped inverse kinematics — are
> implemented here in C++.

---

## Feature status (honest, verified)

| Area | Status | Verification |
| --- | --- | --- |
| Raster engine (RGBA8, alpha-over, bilinear transformed blits, soft brush, eraser) | ✅ done | unit tests |
| **Brush engine (Krita-inspired)**: 10 presets — Pencil (chisel nib), Ink, Marker, Airbrush, Chalk, Spray, Soft, Hard, Hard/Soft eraser — stamp tips as assets, spacing + flow + opacity per preset, stroke-sample picker icons | ✅ done | unit tests (hard/soft alpha, chalk grain, marker band vs spray gaps) |
| **Keyframe easing** (inspired by Synfig's waypoint interpolation): per-key Constant / Linear / Ease In / Ease Out / Ease In-Out, editable from the timeline for pose and camera keys | ✅ done | unit tests (known eased values) |
| **Animated camera track** (inspired by Synfig "anything can animate"): camera keys with easing, auto-key "Anim" mode, timeline row, drives preview + export | ✅ done | unit tests (eased sampling, multiplicative zoom, .lep round-trip) |
| **Playback modes + range** (inspired by OpenToonz flipbook): Loop / Ping-pong / Once, Playback Start/End markers on the ruler | ✅ done | unit tests (advancePlayhead) |
| Stroke stabilizer (moving average + Catmull-Rom, 0–10 slider) | ✅ done | unit tests |
| Layers: raster / group / character / path, opacity, visibility, reorder, duplicate | ✅ done | unit tests |
| Timeline: fps, scrubbing, playback, frame tracks, pose-key diamonds, onion skin (prev red / next green ~30 %) | ✅ done | unit + UI tests |
| Undo/redo (strokes, layer ops, pose keys; 200 steps) | ✅ done | unit tests |
| Rig: humanoid template, FK, per-joint limits | ✅ done | unit tests |
| **Stable IK**: analytic two-bone + damped FABRIK, ≤ 24°/joint/solve, bend-side preservation, limits always enforced | ✅ done | dedicated stability tests (300-step wild-target sweep: no joint ever exceeds the step cap, no limb ever passes its reach, elbow never flips) |
| Pose keyframes + tweening (Linear/EaseIn/EaseOut/EaseInOut/Step, shortest-path angles) | ✅ done | unit tests |
| Character cutting (T-pose → parts → rigged character, key-color removal, auto pivots) | ✅ done | unit tests |
| `.lep` v3 self-contained ZIP project (JSON + embedded PNGs) | ✅ done | roundtrip tests + **cross-verified against python `zipfile`/`zlib` in both directions** |
| `.Lchar` character assets | ✅ done | roundtrip tests |
| Export: PNG sequence, animated GIF (own LZW encoder, median-cut palette), MP4 via ffmpeg | ✅ done | outputs decoded with ffmpeg; demo GIF/MP4/PNGs ship in `demo/` |
| Audio: WAV import, MP3/OGG via ffmpeg, waveform strip + timeline sync | ✅ done | unit tests (wav + mp3) |
| Dark-mode editor (tools, layers/rig/timeline panels, dialogs, file browser) | ✅ done | headless UI tests + live Xvfb screenshots |
| Platforms: **X11** verified live (window, input, presentation, `_NET_WM_ICON`), **Win32** backend + icon resource shipped, headless mode | ✅ | Xvfb + `x11grab` capture |
| App icon: Windows `.ico` (7 sizes), macOS `.icns` (10 variants), embedded window icon + UI logo | ✅ done | generated from the supplied PNG, checked in `assets/icons/` |
| **Single-file distribution**: fully static Linux binary; one-`exe` Windows recipe (MinGW `-static` + windres icon, or MSVC `/MT`) | ✅ done | `make static` → 3.4 MB `leptid-static`, "not a dynamic executable" |
| Optional AI assistant (local Ollama; pose guidance) | ✅ done (off unless a server answers) | manual |
| Camera: project camera frames exports; editor view pan/zoom | ✅ done | compositor tests |

### Known limitations (v0.1)

- **Audio is timing-only** — the waveform syncs the timeline, but sound is not
  played back (no platform audio backend yet).
- The Win32 backend ships but was **not compiled or run on a Windows machine**
  in this environment (only Linux/X11 was available); treat it as first-run
  experimental and report issues.
- The humanoid cut template assumes a front-facing T-pose; unusual cut layouts
  may need pivot adjustments (edit the generated `characters/<id>.json` inside
  the `.lep` archive with any zip tool).
- Single-threaded renderer — big canvases render fast enough for editing, but
  4K exports take seconds per frame.
- AI assistant needs a local Ollama server (`http://localhost:11434`); nothing
  is sent anywhere without one.

---

## Building

### Linux / macOS (g++ or clang)

```sh
make            # builds bin/leptid (+ fonts embedded via bin/bin2cpp)
make tests      # builds bin/leptid_tests
./bin/leptid_tests
make demo       # writes demo/hello_leptid.lep + renders
./bin/leptid    # run the editor
```

X11 is used automatically when available (`pkg-config --exists x11`); without a
display use the headless modes.

### Windows (the primary target)

Two options:

**Option A — MinGW-w64 / MSYS2 (simplest)**

```sh
# in an MSYS2/mingw64 shell
pacman -S mingw-w64-x86_64-gcc make
make                # same Makefile, produces bin\leptid.exe
```

**Option B — CMake + Visual Studio (MSVC)**

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release    # produces build\leptid.exe
```

(See `CMakeLists.txt` — MSVC needs C++17; no other libraries.)

Windows uses the Win32 backend automatically (`platform_win32.cpp`, GDI
`StretchDIBITS` presentation, standard `WM_*` input). If something does not
work on your machine, please open an issue — that backend is the least-tested
part of this build (see limitations).

### Single-file distribution

The whole app is already self-contained — fonts, brush tips and the icon are
compiled into the binary, and there are no dynamic dependencies beyond the
platform itself:

```sh
make static            # → bin/leptid-static (fully static, headless-capable)
ldd bin/leptid-static  # "not a dynamic executable"
```

The static Linux build has no window backend; it runs `--demo`, `--headless`
and `--ui-shot` (CI, servers, AppImage-style wrapping). For interactive use
build the normal target.

**Windows single .exe** — two supported ways, both with the app icon embedded
via `packaging/leptid.rc`:

1. *MinGW-w64 (MSYS2)* — run `packaging/build_windows.bat`, or by hand:
   ```sh
   windres packaging/leptid.rc -O coff -o leptid.res.obj
   g++ -std=c++17 -O2 -Isrc -Ithird_party/stb -o bin/leptid.exe        src/app/*.cpp src/core/*.cpp src/formats/*.cpp src/model/*.cpp        src/rig/*.cpp src/export/*.cpp src/ui/*.cpp        src/platform/platform_win32.cpp src/platform/platform_stub.cpp        src/ai/*.cpp bin/fontdata.cpp bin/assets.cpp leptid.res.obj        -static -static-libgcc -static-libstdc++ -lgdi32 -luser32
   ```
   Result: one portable `leptid.exe` (no DLLs, icon in Explorer/taskbar).
2. *MSVC* — `cmake -B build && cmake --build build --config Release` links
   statically with `/MT`; the `.rc` file is picked up automatically by CMake.

**macOS** — build with the normal Makefile; `assets/icons/leptid.icns` drops
into an `.app` bundle (`Contents/Resources/leptid.icns` + a matching
`CFBundleIconFile` in `Info.plist`).

## Running the tests

```sh
make tests && ./bin/leptid_tests
# → leptid tests: 2437 passed, 0 failed
```

The suite covers the raster engine, the DEFLATE/ZIP/JSON stack (including
cross-checks against python's `zipfile`/`zlib`), FK/IK stability contracts,
tweening, file-format roundtrips, the compositor, brush/stabilizer, the cutter,
audio import and UI interaction (simulated input strokes).

## The demo

```sh
./bin/leptid --demo demo/hello_leptid.lep --gif demo/hello.gif --mp4 demo/hello.mp4 --out demo/render
./bin/leptid demo/hello_leptid.lep    # open it in the editor
```

The demo exercises the whole pipeline: painted background, frame-by-frame
bouncing ball, a cut-and-rigged character waving via IK/keyframes, a motion-path
layer and the project camera. `demo/hello.gif` / `demo/hello.mp4` are rendered
outputs.

## Repository layout

```
.github/
  workflows/
    release.yml       tag-triggered 3-OS release build (see "Releases" below)
assets/
  brushes/            brush tip PNGs (embedded at build time)
  icons/              app icons: .ico (Windows rc), .icns, PNG set
build/                local build output (git-ignored)
cmake/
  embed_assets.cmake  brush/icon embedding step used by CMakeLists.txt
demo/                 hello_leptid.lep demo project + rendered GIF/MP4
packaging/
  leptid.rc           Windows version info + icon (compiled into the exe)
  build_windows.bat   MinGW single-file Windows build helper
src/
  core/      math, RGBA image + compositing primitives, version string
  formats/   JSON, CRC32, DEFLATE/ZIP (own code), PNG (stb)
  model/     layers, project, camera + camera track, compositor,
             brush+stabilizer, undo, .lep v4 serialization, audio import
  rig/       bones, skeleton, FK, stable IK, pose tracks/tweening/easing,
             character, .Lchar serialization, character cutter
  export/    GIF89a encoder, exporter (PNG seq / GIF / MP4)
  ui/        dark-mode widget toolkit (fonts, widgets) + editor (canvas,
             tools, panels, timeline, dialogs)
  platform/  window abstraction: X11 (verified), Win32 (shipped), headless
  ai/        optional Ollama assistant (POSIX sockets; Winsock2 shim on Windows)
  app/       main + demo generator + headless runner
tests/       2480-check test suite (no framework, pure asserts)
tools/
  bin2cpp.cpp         embeds fonts/assets into generated sources
  patch_version.py    stamps a release version into version.cpp + leptid.rc
third_party/ stb headers (public domain) + DejaVu fonts (free license)
CMakeLists.txt       cross-platform build (recommended for MSVC/CI)
Makefile             g++ build for Linux/macOS + `make static` single file
MANUAL.md    user manual (also the AI assistant's knowledge base)
```

## Releases (GitHub Actions)

Releases are built automatically for **Linux, macOS and Windows** by
`.github/workflows/release.yml`. Push a version tag and the workflow builds,
tests and publishes:

```bash
git tag v0.1.3
git push origin v0.1.3
```

Per platform it stamps the tag into `src/core/version.cpp` and
`packaging/leptid.rc` (`tools/patch_version.py`), builds with CMake, runs the
full test suite (2480 checks) plus a headless demo smoke test, then uploads:

| OS | Runner | Backend | Artifact |
| --- | --- | --- | --- |
| Linux x64 | ubuntu-latest | X11 | `leptid-vX.Y.Z-linux-x64.tar.gz` (GUI binary + zero-dependency static single-file build) |
| macOS universal | macos-latest | headless stub | `leptid-vX.Y.Z-macos-universal.tar.gz` (arm64 + x86_64; CLI/headless: render, export, demo) |
| Windows x64 | windows-latest | Win32 | `leptid-vX.Y.Z-windows-x64.zip` (portable exe, static CRT, icon from the .rc) |

The GitHub Release is created with auto-generated notes once all three
platforms pass. The workflow also supports `workflow_dispatch` to test the
pipeline without publishing.

## Design notes (why it is built this way)

- **One shared pipeline**: the same compositor draws the canvas preview,
  playback and exports — what you see is what renders.
- **Coordinate conversion is centralized** (`ViewTransform`) — no scattered
  screen/world math.
- **Stability-first IK**: the analytic two-bone solver cannot overshoot by
  construction; FABRIK runs with capped iterations; every solve is clamped by
  joint limits and a per-solve angular step, which is what makes dragging
  smooth instead of flailing (the explicitly requested behavior).
- **Self-contained formats**: `.lep` v3 embeds every asset so projects keep
  working when files move; written/read with Leptid's own verified ZIP code.
- **Zero dependencies**: distribution is a folder — no installers, no runtime.

## Licenses

- Leptid code: MIT (see `LICENSE`).
- `third_party/stb/*`: public domain / MIT (stb single-file libraries).
- `third_party/fonts/*`: DejaVu Sans — free license (Bitstream Vera + public
  domain additions), redistributed with this project per its terms.

Copyright © 2026 — Leptid is free software; use it, fork it, ship it.
