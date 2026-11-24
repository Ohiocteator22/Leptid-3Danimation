// Leptid — audio import: WAV (PCM) directly; anything else via ffmpeg transcode.
// Audio is used for timeline sync (waveform + duration); it is not played back
// in v0.1 (no platform audio backend yet) — see README honest-limitations.
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "model/project.h"

namespace leptid {

// Load PCM WAV (16-bit or 8-bit, mono/stereo) into an AudioClip.
bool loadWavFile(const std::string& path, std::shared_ptr<AudioClip>& out, std::string* error = nullptr);

// MP3/OGG/etc via `ffmpeg -i input -f wav -` (graceful failure without ffmpeg).
bool loadAudioViaFfmpeg(const std::string& path, std::shared_ptr<AudioClip>& out, std::string* error = nullptr);

// Dispatcher by extension.
bool importAudio(const std::string& path, std::shared_ptr<AudioClip>& out, std::string* error = nullptr);

} // namespace leptid
