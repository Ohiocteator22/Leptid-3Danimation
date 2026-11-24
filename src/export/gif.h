// Leptid — GIF89a encoder (own LZW + median-cut palette, no dependencies).
#pragma once
#include <cstdint>
#include <vector>
#include "core/image.h"

namespace leptid {

// Encode an animation as GIF89a. `delayCs` = per-frame delay in centiseconds.
class GifEncoder {
public:
    void addFrame(const Image& img);
    bool write(const std::string& path, int delayCs, std::string* error = nullptr) const;

private:
    std::vector<Image> frames_;
    void buildPalette();                       // median cut over all frames
    uint8_t nearestIndex(uint32_t px) const;   // with coarse cache
    std::vector<uint32_t> palette_;            // up to 256 entries
    mutable std::vector<int16_t> cache_;       // 32^3 RGB lookup
};

} // namespace leptid
