// Leptid — audio import tests.
#include "testfw.h"
#include "model/audio.h"
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <vector>

using namespace leptid;
using namespace testfw;

void runAudioTests() {
    // synthesize a 16-bit stereo WAV (0.5 s @ 22050 Hz, sine)
    const int rate = 22050, frames = rate / 2, ch = 2;
    std::vector<uint8_t> wav;
    auto put32 = [&](uint32_t v) { wav.push_back(v & 0xFF); wav.push_back((v >> 8) & 0xFF); wav.push_back((v >> 16) & 0xFF); wav.push_back((v >> 24) & 0xFF); };
    auto put16 = [&](uint16_t v) { wav.push_back(v & 0xFF); wav.push_back((v >> 8) & 0xFF); };
    uint32_t dataSize = frames * ch * 2;
    for (char c : {'R','I','F','F'}) wav.push_back((uint8_t)c);
    put32(36 + dataSize);
    for (char c : {'W','A','V','E'}) wav.push_back((uint8_t)c);
    for (char c : {'f','m','t',' '}) wav.push_back((uint8_t)c);
    put32(16);
    put16(1); put16(ch); put32(rate); put32(rate * ch * 2); put16(ch * 2); put16(16);
    for (char c : {'d','a','t','a'}) wav.push_back((uint8_t)c);
    put32(dataSize);
    for (int i = 0; i < frames; ++i)
        for (int c = 0; c < ch; ++c) {
            float v = std::sin((float)i * 0.05f);
            put16((uint16_t)(int16_t)(v * 20000));
        }
    FILE* f = fopen("/tmp/leptid_test.wav", "wb");
    fwrite(wav.data(), 1, wav.size(), f);
    fclose(f);
    std::shared_ptr<AudioClip> clip;
    std::string err;
    CHECK(loadWavFile("/tmp/leptid_test.wav", clip, &err));
    CHECK(clip->sampleRate == rate && clip->channels == ch);
    CHECK(clip->samples.size() == (size_t)frames * ch);
    CHECK_NEAR(clip->durationSec(), 0.5, 0.01);
    CHECK(clip->durationFrames(24) == 12);
    // dispatcher on extension
    CHECK(importAudio("/tmp/leptid_test.wav", clip, &err));
    // ffmpeg transcode path (mp3 from our own sine wav)
    if (system("ffmpeg -y -loglevel error -i /tmp/leptid_test.wav /tmp/leptid_test.mp3") == 0) {
        CHECK(importAudio("/tmp/leptid_test.mp3", clip, &err));
        CHECK_NEAR(clip->durationSec(), 0.5, 0.05);
    }
}
