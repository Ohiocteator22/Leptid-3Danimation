// Leptid — GIF89a encoder implementation.
#include "export/gif.h"
#include <algorithm>
#include <cstdio>
#include <map>
#include <cmath>

namespace leptid {

namespace {

// --- median-cut palette from sampled pixels ---
struct Rgb { int r, g, b; };

void medianCut(std::vector<Rgb>& voxels, int depth, std::vector<Rgb>& out) {
    if (voxels.empty()) return;
    if (depth == 0 || voxels.size() < 2) {
        long r = 0, g = 0, b = 0;
        for (auto& v : voxels) { r += v.r; g += v.g; b += v.b; }
        out.push_back({(int)(r / voxels.size()), (int)(g / voxels.size()), (int)(b / voxels.size())});
        return;
    }
    // widest channel
    int minR = 255, maxR = 0, minG = 255, maxG = 0, minB = 255, maxB = 0;
    for (auto& v : voxels) {
        minR = std::min(minR, v.r); maxR = std::max(maxR, v.r);
        minG = std::min(minG, v.g); maxG = std::max(maxG, v.g);
        minB = std::min(minB, v.b); maxB = std::max(maxB, v.b);
    }
    int rangeR = maxR - minR, rangeG = maxG - minG, rangeB = maxB - minB;
    auto cmp = [&](const Rgb& a, const Rgb& b2) {
        if (rangeR >= rangeG && rangeR >= rangeB) return a.r < b2.r;
        if (rangeG >= rangeB) return a.g < b2.g;
        return a.b < b2.b;
    };
    std::sort(voxels.begin(), voxels.end(), cmp);
    size_t mid = voxels.size() / 2;
    std::vector<Rgb> left(voxels.begin(), voxels.begin() + mid);
    std::vector<Rgb> right(voxels.begin() + mid, voxels.end());
    medianCut(left, depth - 1, out);
    medianCut(right, depth - 1, out);
}

// --- GIF LZW ---
struct LzwWriter {
    std::vector<uint8_t>& out;
    uint32_t acc = 0;
    int nbits = 0;
    std::vector<uint8_t> block;
    explicit LzwWriter(std::vector<uint8_t>& o) : out(o) {}
    void put(int code, int width) {
        acc |= (uint32_t)code << nbits;
        nbits += width;
        while (nbits >= 8) {
            block.push_back((uint8_t)(acc & 0xFF));
            acc >>= 8;
            nbits -= 8;
            if (block.size() == 255) { flushBlock(); }
        }
    }
    void flushBlock() {
        if (block.empty()) return;
        out.push_back((uint8_t)block.size());
        out.insert(out.end(), block.begin(), block.end());
        block.clear();
    }
    void finish() {
        if (nbits > 0) { block.push_back((uint8_t)(acc & 0xFF)); acc = 0; nbits = 0; }
        flushBlock();
        out.push_back(0); // block terminator
    }
};

void lzwEncode(const std::vector<uint8_t>& indices, int minCodeSize, std::vector<uint8_t>& out) {
    int clear = 1 << minCodeSize;
    int eoi = clear + 1;
    LzwWriter w(out);
    int codeSize = minCodeSize + 1;
    int nextCode = eoi + 1;
    w.put(clear, codeSize);
    std::map<std::pair<int, int>, int> table;
    int prefix = indices.empty() ? -1 : indices[0];
    for (size_t i = 1; i < indices.size(); ++i) {
        int k = indices[i];
        auto key = std::make_pair(prefix, k);
        auto it = table.find(key);
        if (it != table.end()) {
            prefix = it->second;
        } else {
            w.put(prefix, codeSize);
            if (nextCode < 4096) {
                table[key] = nextCode++;
                if (nextCode - 1 == (1 << codeSize) && codeSize < 12) ++codeSize;
            } else {
                w.put(clear, codeSize);
                table.clear();
                codeSize = minCodeSize + 1;
                nextCode = eoi + 1;
            }
            prefix = k;
        }
    }
    if (prefix >= 0) w.put(prefix, codeSize);
    w.put(eoi, codeSize);
    w.finish();
}

} // namespace

void GifEncoder::addFrame(const Image& img) {
    frames_.push_back(img);
    cache_.clear();
}

void GifEncoder::buildPalette() {
    std::vector<Rgb> samples;
    for (const Image& f : frames_) {
        int step = std::max(1, (int)std::sqrt((double)f.w * f.h / 4000.0));
        for (int y = 0; y < f.h; y += step)
            for (int x = 0; x < f.w; x += step) {
                uint32_t p = f.at(x, y);
                samples.push_back({(int)(p & 0xFF), (int)((p >> 8) & 0xFF), (int)((p >> 16) & 0xFF)});
            }
        if (samples.size() > 200000) break;
    }
    if (samples.empty()) samples.push_back({0, 0, 0});
    std::vector<Rgb> pal;
    medianCut(samples, 7, pal); // up to 256 entries
    pal.resize(std::min((size_t)256, pal.size()));
    if (pal.empty()) pal.push_back({0, 0, 0});
    palette_.clear();
    for (auto& c : pal) palette_.push_back(((uint32_t)c.r) | ((uint32_t)c.g << 8) | ((uint32_t)c.b << 16));
}

uint8_t GifEncoder::nearestIndex(uint32_t px) const {
    int r = (px >> 0) & 0xFF, g = (px >> 8) & 0xFF, b = (px >> 16) & 0xFF;
    size_t cacheKey = (size_t)((r >> 3) << 10 | (g >> 3) << 5 | (b >> 3));
    if (cache_.empty()) cache_.assign(32768, -1);
    if (cache_[cacheKey] >= 0) return (uint8_t)cache_[cacheKey];
    int best = 0;
    long bestD = 1 << 30;
    for (size_t i = 0; i < palette_.size(); ++i) {
        uint32_t p = palette_[i];
        int dr = r - (int)(p & 0xFF), dg = g - (int)((p >> 8) & 0xFF), db = b - (int)((p >> 16) & 0xFF);
        long d = dr * dr * 30 + dg * dg * 59 + db * db * 11;
        if (d < bestD) { bestD = d; best = (int)i; }
    }
    cache_[cacheKey] = (int16_t)best;
    return (uint8_t)best;
}

bool GifEncoder::write(const std::string& path, int delayCs, std::string* error) const {
    auto fail = [&](const std::string& m) { if (error) *error = m; return false; };
    if (frames_.empty()) return fail("no frames");
    if (frames_[0].empty()) return fail("empty frame");
    int W = frames_[0].w, H = frames_[0].h;
    for (const Image& f : frames_)
        if (f.w != W || f.h != H) return fail("frames differ in size");

    const_cast<GifEncoder*>(this)->buildPalette();
    cache_.clear();
    int palSize = (int)palette_.size();
    int bits = 1;
    while ((1 << bits) < palSize) ++bits;
    int tableEntries = 1 << bits;
    int minCodeSize = std::max(2, bits);

    std::vector<uint8_t> out;
    // header
    const char* hdr = "GIF89a";
    out.insert(out.end(), hdr, hdr + 6);
    out.push_back((uint8_t)(W & 0xFF)); out.push_back((uint8_t)((W >> 8) & 0xFF));
    out.push_back((uint8_t)(H & 0xFF)); out.push_back((uint8_t)((H >> 8) & 0xFF));
    out.push_back((uint8_t)(0x80 | ((bits - 1) & 7))); // global color table, size
    out.push_back(0); // bg color index
    out.push_back(0); // aspect
    for (int i = 0; i < tableEntries; ++i) {
        uint32_t p = i < palSize ? palette_[i] : 0;
        out.push_back((uint8_t)(p & 0xFF));
        out.push_back((uint8_t)((p >> 8) & 0xFF));
        out.push_back((uint8_t)((p >> 16) & 0xFF));
    }
    for (const Image& f : frames_) {
        // graphic control extension: delay, no transparency
        out.push_back(0x21); out.push_back(0xF9); out.push_back(4);
        out.push_back(0x04); // disposal = none? bit2..4: disposal method 1 (do not dispose) + no transparency
        int d = std::max(2, delayCs);
        out.push_back((uint8_t)(d & 0xFF)); out.push_back((uint8_t)((d >> 8) & 0xFF));
        out.push_back(0); out.push_back(0);
        // image descriptor
        out.push_back(0x2C);
        out.push_back(0); out.push_back(0); out.push_back(0); out.push_back(0);
        out.push_back((uint8_t)(W & 0xFF)); out.push_back((uint8_t)((W >> 8) & 0xFF));
        out.push_back((uint8_t)(H & 0xFF)); out.push_back((uint8_t)((H >> 8) & 0xFF));
        out.push_back(0); // no local table
        out.push_back((uint8_t)minCodeSize);
        std::vector<uint8_t> indices;
        indices.reserve((size_t)W * H);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                indices.push_back(nearestIndex(f.at(x, y)));
        lzwEncode(indices, minCodeSize, out);
    }
    out.push_back(0x3B); // trailer

    FILE* fp = fopen(path.c_str(), "wb");
    if (!fp) return fail("cannot write " + path);
    fwrite(out.data(), 1, out.size(), fp);
    fclose(fp);
    return true;
}

} // namespace leptid
