// Leptid — audio import implementation.
#include "model/audio.h"
#include <cstdio>
#include <cstring>

namespace leptid {

namespace {

uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

bool parseWavBytes(const std::vector<uint8_t>& bytes, std::shared_ptr<AudioClip>& out, std::string* error) {
    auto fail = [&](const char* m) { if (error) *error = m; return false; };
    if (bytes.size() < 44 || memcmp(bytes.data(), "RIFF", 4) != 0 || memcmp(bytes.data() + 8, "WAVE", 4) != 0)
        return fail("not a WAV file");
    size_t pos = 12;
    uint16_t fmt = 0, channels = 0, bits = 0;
    uint32_t rate = 0;
    const uint8_t* dataPtr = nullptr;
    size_t dataLen = 0;
    while (pos + 8 <= bytes.size()) {
        const uint8_t* id = bytes.data() + pos;
        uint32_t sz = rd32(bytes.data() + pos + 4);
        if (memcmp(id, "fmt ", 4) == 0 && pos + 8 + 16 <= bytes.size()) {
            fmt = rd16(bytes.data() + pos + 8);
            channels = rd16(bytes.data() + pos + 10);
            rate = rd32(bytes.data() + pos + 12);
            bits = rd16(bytes.data() + pos + 22);
        } else if (memcmp(id, "data", 4) == 0) {
            dataPtr = bytes.data() + pos + 8;
            dataLen = std::min((size_t)sz, bytes.size() - pos - 8);
        }
        pos += 8 + sz + (sz & 1);
        if (sz == 0) break; // malformed; avoid infinite loop
    }
    if (!dataPtr || channels == 0 || rate == 0) return fail("missing fmt/data chunk");
    if (fmt != 1) return fail("only PCM WAV supported");
    if (bits != 16 && bits != 8) return fail("only 8/16-bit PCM supported");
    auto clip = std::make_shared<AudioClip>();
    clip->path = "";
    clip->sampleRate = (int)rate;
    clip->channels = channels;
    size_t frames = dataLen / (channels * (bits / 8));
    clip->samples.resize(frames * channels);
    for (size_t i = 0; i < frames * channels; ++i) {
        if (bits == 16) {
            int16_t v = (int16_t)rd16(dataPtr + i * 2);
            clip->samples[i] = (float)v / 32768.0f;
        } else {
            clip->samples[i] = ((float)dataPtr[i] - 128.0f) / 128.0f;
        }
    }
    out = clip;
    return true;
}

std::vector<uint8_t> readFile(const std::string& path) {
    std::vector<uint8_t> data;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return data;
    uint8_t buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) data.insert(data.end(), buf, buf + n);
    fclose(f);
    return data;
}

} // namespace

bool loadWavFile(const std::string& path, std::shared_ptr<AudioClip>& out, std::string* error) {
    auto bytes = readFile(path);
    if (!parseWavBytes(bytes, out, error)) return false;
    out->path = path;
    return true;
}

bool loadAudioViaFfmpeg(const std::string& path, std::shared_ptr<AudioClip>& out, std::string* error) {
    // probe ffmpeg quickly
    FILE* probe = popen("ffmpeg -version 2>/dev/null", "r");
    if (!probe) { if (error) *error = "ffmpeg not available"; return false; }
    char buf[64] = {0};
    bool have = fgets(buf, sizeof buf, probe) != nullptr;
    int rc = pclose(probe);
    if (!have || rc != 0) { if (error) *error = "ffmpeg not available"; return false; }
    std::string cmd = "ffmpeg -y -loglevel error -i \"" + path + "\" -f wav -acodec pcm_s16le -";
    FILE* p = popen(cmd.c_str(), "r");
    if (!p) { if (error) *error = "cannot spawn ffmpeg"; return false; }
    std::vector<uint8_t> bytes;
    uint8_t buf2[65536];
    size_t n;
    while ((n = fread(buf2, 1, sizeof buf2, p)) > 0) bytes.insert(bytes.end(), buf2, buf2 + n);
    int prc = pclose(p);
    if (prc != 0 || bytes.size() < 44) { if (error) *error = "ffmpeg decode failed"; return false; }
    if (!parseWavBytes(bytes, out, error)) return false;
    out->path = path;
    return true;
}

bool importAudio(const std::string& path, std::shared_ptr<AudioClip>& out, std::string* error) {
    size_t dot = path.rfind('.');
    std::string ext = dot == std::string::npos ? "" : path.substr(dot + 1);
    for (auto& c : ext) c = (char)((c >= 'A' && c <= 'Z') ? c + 32 : c);
    if (ext == "wav" || ext == "wave") return loadWavFile(path, out, error);
    return loadAudioViaFfmpeg(path, out, error);
}

} // namespace leptid
