// Leptid — PNG load/save on top of stb_image / stb_image_write (vendored, public domain).
#pragma once
#include "core/image.h"

namespace leptid {

bool loadPng(const std::string& path, Image& out);
bool loadPngMemory(const uint8_t* data, size_t len, Image& out);
bool savePng(const std::string& path, const Image& img);
std::vector<uint8_t> savePngMemory(const Image& img);

} // namespace leptid
