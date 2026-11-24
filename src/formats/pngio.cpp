// Leptid — PNG I/O implementation.
#include "formats/pngio.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

namespace leptid {

bool loadPngMemory(const uint8_t* data, size_t len, Image& out) {
    int w = 0, h = 0, n = 0;
    stbi_uc* pixels = stbi_load_from_memory(data, (int)len, &w, &h, &n, 4);
    if (!pixels || w <= 0 || h <= 0) {
        if (pixels) stbi_image_free(pixels);
        return false;
    }
    out.create(w, h);
    size_t count = (size_t)w * h;
    for (size_t i = 0; i < count; ++i) {
        uint32_t r = pixels[i * 4], g = pixels[i * 4 + 1], b = pixels[i * 4 + 2], a = pixels[i * 4 + 3];
        out.px[i] = (a << 24) | (b << 16) | (g << 8) | r;
    }
    stbi_image_free(pixels);
    return true;
}

bool loadPng(const std::string& path, Image& out) {
    int w = 0, h = 0, n = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &n, 4);
    if (!pixels || w <= 0 || h <= 0) {
        if (pixels) stbi_image_free(pixels);
        return false;
    }
    out.create(w, h);
    size_t count = (size_t)w * h;
    for (size_t i = 0; i < count; ++i) {
        uint32_t r = pixels[i * 4], g = pixels[i * 4 + 1], b = pixels[i * 4 + 2], a = pixels[i * 4 + 3];
        out.px[i] = (a << 24) | (b << 16) | (g << 8) | r;
    }
    stbi_image_free(pixels);
    return true;
}

static void stbiWriteFunc(void* ctx, void* data, int size) {
    auto* vec = (std::vector<uint8_t>*)ctx;
    const uint8_t* d = (const uint8_t*)data;
    vec->insert(vec->end(), d, d + size);
}

std::vector<uint8_t> savePngMemory(const Image& img) {
    std::vector<uint8_t> out;
    if (img.empty()) return out;
    std::vector<stbi_uc> rgba((size_t)img.w * img.h * 4);
    size_t count = (size_t)img.w * img.h;
    for (size_t i = 0; i < count; ++i) {
        uint32_t p = img.px[i];
        rgba[i * 4] = (stbi_uc)(p & 0xFF);
        rgba[i * 4 + 1] = (stbi_uc)((p >> 8) & 0xFF);
        rgba[i * 4 + 2] = (stbi_uc)((p >> 16) & 0xFF);
        rgba[i * 4 + 3] = (stbi_uc)((p >> 24) & 0xFF);
    }
    stbi_write_png_to_func(stbiWriteFunc, &out, img.w, img.h, 4, rgba.data(), img.w * 4);
    return out;
}

bool savePng(const std::string& path, const Image& img) {
    if (img.empty()) return false;
    std::vector<stbi_uc> rgba((size_t)img.w * img.h * 4);
    size_t count = (size_t)img.w * img.h;
    for (size_t i = 0; i < count; ++i) {
        uint32_t p = img.px[i];
        rgba[i * 4] = (stbi_uc)(p & 0xFF);
        rgba[i * 4 + 1] = (stbi_uc)((p >> 8) & 0xFF);
        rgba[i * 4 + 2] = (stbi_uc)((p >> 16) & 0xFF);
        rgba[i * 4 + 3] = (stbi_uc)((p >> 24) & 0xFF);
    }
    return stbi_write_png(path.c_str(), img.w, img.h, 4, rgba.data(), img.w * 4) != 0;
}

} // namespace leptid
