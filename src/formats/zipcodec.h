// Leptid — DEFLATE (RFC 1950/1951) codec + CRC-32, no external dependencies.
// inflate: full decoder (stored, fixed and dynamic Huffman blocks).
// deflate: fixed-Huffman LZ77 compressor with greedy hash-chain matching.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace leptid {

uint32_t crc32(const uint8_t* data, size_t len, uint32_t seed = 0);

// raw DEFLATE bitstream <-> bytes. Returns false on corrupt input.
bool inflateData(const uint8_t* in, size_t inLen, std::vector<uint8_t>& out);
void deflateData(const uint8_t* in, size_t inLen, std::vector<uint8_t>& out);

// zlib-wrapped (with 0x78 header + Adler-32) — used for embedded PNG decoding helpers if needed.
bool zlibInflate(const uint8_t* in, size_t inLen, std::vector<uint8_t>& out);
void zlibDeflate(const uint8_t* in, size_t inLen, std::vector<uint8_t>& out);

// ---------------- ZIP container (STORE + DEFLATE) ----------------
class ZipWriter {
public:
    struct Entry { std::string name; std::vector<uint8_t> data; uint32_t crc = 0; uint32_t offset = 0; bool store = false; };
    void add(const std::string& name, const std::string& text) {
        Entry e; e.name = name; e.data.assign(text.begin(), text.end()); entries.push_back(std::move(e));
    }
    void add(const std::string& name, const std::vector<uint8_t>& bytes, bool store = false) {
        Entry e; e.name = name; e.data = bytes; e.store = store; entries.push_back(std::move(e));
    }
    std::vector<uint8_t> build() const;

private:
    std::vector<Entry> entries;
};

class ZipReader {
public:
    struct Entry { std::string name; uint32_t offset = 0; uint32_t compSize = 0; uint32_t uncompSize = 0; uint16_t method = 0; };
    bool open(const std::vector<uint8_t>& bytes);
    const std::vector<Entry>& entries() const { return entries_; }
    bool exists(const std::string& name) const;
    bool read(const std::string& name, std::vector<uint8_t>& out) const;
    bool readText(const std::string& name, std::string& out) const;
    std::string error() const { return error_; }

private:
    std::vector<uint8_t> bytes_;
    std::vector<Entry> entries_;
    std::string error_;
};

} // namespace leptid
