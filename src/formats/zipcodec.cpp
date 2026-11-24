// Leptid — DEFLATE + CRC32 + ZIP implementation (own code, no external deps).
#include "formats/zipcodec.h"
#include <cstring>
#include <cstddef>

namespace leptid {

#if defined(_MSC_VER)
// MSVC has no POSIX ssize_t; every use here fits std::ptrdiff_t.
typedef std::ptrdiff_t ssize_t;
#endif

// ---------------- CRC-32 (IEEE 802.3, reflected) ----------------
uint32_t crc32(const uint8_t* data, size_t len, uint32_t seed) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    uint32_t c = seed ^ 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) c = table[(c ^ data[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

// ---------------- bit reader ----------------
namespace {

struct BitReader {
    const uint8_t* data;
    size_t len;
    size_t pos = 0;   // byte pos
    uint32_t bitBuf = 0;
    int bitCount = 0;
    bool failed = false;

    BitReader(const uint8_t* d, size_t l) : data(d), len(l) {}
    bool readBit(int& b) {
        if (bitCount == 0) {
            if (pos >= len) { failed = true; return false; }
            bitBuf = data[pos++];
            bitCount = 8;
        }
        b = bitBuf & 1;
        bitBuf >>= 1;
        --bitCount;
        return true;
    }
    bool readBits(int n, int& out) {
        out = 0;
        for (int i = 0; i < n; ++i) {
            int b;
            if (!readBit(b)) return false;
            out |= b << i;
        }
        return true;
    }
    // Huffman canonical decoding (puff-style: per-length counts + sorted symbol table).
    bool decodeSymbol(const int* lengths, int nCodes, int& sym) {
        int count[16] = {0};
        for (int i = 0; i < nCodes; ++i) if (lengths[i] > 0 && lengths[i] < 16) count[lengths[i]]++;
        int next[16];
        int total = 0;
        for (int l = 1; l < 16; ++l) { next[l] = total; total += count[l]; }
        int symbols[288];
        for (int i = 0; i < nCodes; ++i)
            if (lengths[i] > 0 && lengths[i] < 16) symbols[next[lengths[i]]++] = i;
        int code = 0, first = 0, index = 0;
        for (int l = 1; l <= 15; ++l) {
            int b;
            if (!readBit(b)) { failed = true; return false; }
            code |= b;
            if (code - first < count[l]) {
                sym = symbols[index + (code - first)];
                return true;
            }
            index += count[l];
            first = (first + count[l]) << 1;
            code <<= 1;
        }
        failed = true;
        return false;
    }
};

const int kLenBase[29] = {3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258};
const int kLenExtra[29] = {0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0};
const int kDistBase[30] = {1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577};
const int kDistExtra[30] = {0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13};

void buildFixedLengths(int litLen[288], int distLen[30]) {
    for (int i = 0; i < 144; ++i) litLen[i] = 8;
    for (int i = 144; i < 256; ++i) litLen[i] = 9;
    for (int i = 256; i < 280; ++i) litLen[i] = 7;
    for (int i = 280; i < 288; ++i) litLen[i] = 8;
    for (int i = 0; i < 30; ++i) distLen[i] = 5;
}

bool inflateBlock(BitReader& br, std::vector<uint8_t>& out, const int litLens[288], const int distLens[30]) {
    while (true) {
        int sym;
        if (!br.decodeSymbol(litLens, 288, sym)) return false;
        if (sym < 256) {
            out.push_back((uint8_t)sym);
        } else if (sym == 256) {
            return true; // end of block
        } else {
            int li = sym - 257;
            if (li >= 29) return false;
            int extra;
            if (!br.readBits(kLenExtra[li], extra)) return false;
            int length = kLenBase[li] + extra;
            int dsym;
            if (!br.decodeSymbol(distLens, 30, dsym)) return false;
            if (dsym >= 30) return false;
            if (!br.readBits(kDistExtra[dsym], extra)) return false;
            int dist = kDistBase[dsym] + extra;
            if ((size_t)dist > out.size()) return false; // invalid reference
            size_t start = out.size() - (size_t)dist;
            for (int i = 0; i < length; ++i) out.push_back(out[start + i]);
        }
        if (br.failed) return false;
    }
}

bool inflateRaw(const uint8_t* in, size_t inLen, std::vector<uint8_t>& out) {
    BitReader br(in, inLen);
    while (true) {
        int bfinal, btype;
        if (!br.readBits(1, bfinal)) return false;
        if (!br.readBits(2, btype)) return false;
        if (btype == 0) {
            // stored: skip to byte boundary, read LEN, copy
            br.bitCount = 0;
            if (br.pos + 4 > br.len) return false;
            int lo = in[br.pos], hi = in[br.pos + 1], nlo = in[br.pos + 2], nhi = in[br.pos + 3];
            br.pos += 4;
            int n = lo | (hi << 8);
            int ncheck = (nlo | (nhi << 8));
            if ((n ^ 0xFFFF) != ncheck) return false;
            if (br.pos + n > br.len) return false;
            out.insert(out.end(), in + br.pos, in + br.pos + n);
            br.pos += n;
        } else if (btype == 1) {
            int litLens[288], distLens[30];
            buildFixedLengths(litLens, distLens);
            if (!inflateBlock(br, out, litLens, distLens)) return false;
        } else if (btype == 2) {
            int hlit, hdist, hclen;
            if (!br.readBits(5, hlit) || !br.readBits(5, hdist) || !br.readBits(4, hclen)) return false;
            hlit += 257; hdist += 1; hclen += 4;
            static const int order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
            int codeLens[19] = {0};
            for (int i = 0; i < hclen; ++i) {
                int v;
                if (!br.readBits(3, v)) return false;
                codeLens[order[i]] = v;
            }
            // decode code-length alphabet
            int lenVals[320] = {0}; // hlit + hdist values
            int idx = 0;
            while (idx < hlit + hdist) {
                int sym;
                if (!br.decodeSymbol(codeLens, 19, sym)) return false;
                if (sym < 16) { lenVals[idx++] = sym; }
                else if (sym == 16) {
                    if (idx == 0) return false;
                    int rep;
                    if (!br.readBits(2, rep)) return false;
                    int v = lenVals[idx - 1];
                    for (int k = 0; k < rep + 3 && idx < 320; ++k) lenVals[idx++] = v;
                } else if (sym == 17) {
                    int rep;
                    if (!br.readBits(3, rep)) return false;
                    for (int k = 0; k < rep + 3 && idx < 320; ++k) lenVals[idx++] = 0;
                } else { // 18
                    int rep;
                    if (!br.readBits(7, rep)) return false;
                    for (int k = 0; k < rep + 11 && idx < 320; ++k) lenVals[idx++] = 0;
                }
            }
            if (idx > hlit + hdist) return false;
            int litLens[288] = {0}, distLens[30] = {0};
            for (int i = 0; i < hlit; ++i) litLens[i] = lenVals[i];
            for (int i = 0; i < hdist; ++i) distLens[i] = lenVals[hlit + i];
            if (!inflateBlock(br, out, litLens, distLens)) return false;
        } else {
            return false; // btype 3 invalid
        }
        if (bfinal) break;
        if (br.failed) return false;
    }
    return !br.failed;
}

} // namespace

bool inflateData(const uint8_t* in, size_t inLen, std::vector<uint8_t>& out) {
    out.clear();
    return inflateRaw(in, inLen, out);
}

void deflateData(const uint8_t* in, size_t inLen, std::vector<uint8_t>& out) {
    // Fixed-Huffman DEFLATE with greedy LZ77 matching (hash chains).
    out.clear();
    out.reserve(inLen + inLen / 8 + 64);
    const int kHashBits = 15, kHashSize = 1 << kHashBits;
    const int kMaxChain = 128, kMinMatch = 3, kMaxMatch = 258, kWindow = 32768;
    static std::vector<int> head, prev;
    head.assign(kHashSize, -1);
    prev.assign(inLen + 1, -1);
    auto hashAt = [&](size_t i) -> int {
        if (i + 2 >= inLen) return -1;
        uint32_t h = (uint32_t)in[i] | ((uint32_t)in[i + 1] << 8) | ((uint32_t)in[i + 2] << 16);
        return (int)((h * 2654435761u) >> (32 - kHashBits));
    };
    // We write with a proper bit-writer: fixed Huffman, LSB-first.
    struct BitWriter {
        std::vector<uint8_t>& buf;
        uint32_t acc = 0;
        int n = 0;
        void put(int value, int bits) {
            for (int i = 0; i < bits; ++i) {
                acc |= ((value >> i) & 1) << n;
                if (++n == 8) { buf.push_back((uint8_t)acc); acc = 0; n = 0; }
            }
        }
        // Huffman codes in fixed-Huffman are written MSB-first within the code.
        void putCode(int code, int bits) {
            for (int i = bits - 1; i >= 0; --i) {
                acc |= ((code >> i) & 1) << n;
                if (++n == 8) { buf.push_back((uint8_t)acc); acc = 0; n = 0; }
            }
        }
        void flush() { if (n > 0) buf.push_back((uint8_t)acc); acc = 0; n = 0; }
    } bw{out};
    // Block header: BFINAL=1, BTYPE=01 (fixed Huffman) — written LSB-first via the bit writer.
    bw.put(1, 1);
    bw.put(1, 2);
    auto litCodeBits = [](int sym, int* bits, int* code) {
        if (sym < 144) { *bits = 8; *code = 0x30 + sym; }
        else if (sym < 256) { *bits = 9; *code = 0x190 + (sym - 144); }
        else if (sym < 280) { *bits = 7; *code = sym - 256; }
        else { *bits = 8; *code = 0xC0 + (sym - 280); }
    };
    auto emitLiteralSym = [&](int sym) {
        int bits, code;
        litCodeBits(sym, &bits, &code);
        bw.putCode(code, bits);
    };
    auto emitExtra = [&](int value, int nBits) { bw.put(value, nBits); }; // extra bits are LSB-first
    auto emitDistCode = [&](int dsym) {
        // fixed distance codes: 5 bits, MSB-first, plain value
        bw.putCode(dsym, 5);
    };
    auto lenToSym = [](int len, int& sym, int& extra, int& nBits) {
        sym = 28;
        for (int i = 28; i >= 0; --i) {
            if (len >= kLenBase[i] && len < kLenBase[i] + (1 << kLenExtra[i]) ) {
                if (i == 28 && len != 258) continue;
                sym = 257 + i; extra = len - kLenBase[i]; nBits = kLenExtra[i];
                if (i == 28) { extra = 0; nBits = 0; }
                return;
            }
        }
        // fallback
        sym = 257; extra = 0; nBits = 0;
    };
    auto distToSym = [](int dist, int& sym, int& extra, int& nBits) {
        sym = 29;
        for (int i = 29; i >= 0; --i) {
            if (dist >= kDistBase[i] && dist < kDistBase[i] + (1 << kDistExtra[i])) {
                sym = i; extra = dist - kDistBase[i]; nBits = kDistExtra[i];
                return;
            }
        }
        sym = 0; extra = 0; nBits = 0;
    };
    size_t i = 0;
    while (i < inLen) {
        int bestLen = 0, bestDist = 0;
        int h = hashAt(i);
        if (h >= 0 && i + kMinMatch <= inLen) {
            int cand = head[h];
            int chain = 0;
            size_t maxLen = std::min<size_t>(kMaxMatch, inLen - i);
            while (cand >= 0 && chain++ < kMaxChain) {
                size_t d = i - (size_t)cand;
                if (d > kWindow) break;
                if (d > 0) {
                    size_t l = 0;
                    while (l < maxLen && in[cand + l] == in[i + l]) ++l;
                    if ((int)l > bestLen) { bestLen = (int)l; bestDist = (int)d; }
                    if (bestLen >= (int)maxLen && bestLen >= 32) break;
                }
                cand = prev[cand];
            }
            if (bestLen < kMinMatch) bestLen = 0;
        }
        if (bestLen >= kMinMatch) {
            int sym, extra, nBits;
            lenToSym(bestLen, sym, extra, nBits);
            emitLiteralSym(sym);
            emitExtra(extra, nBits);
            int dsym, dextra, dnBits;
            distToSym(bestDist, dsym, dextra, dnBits);
            emitDistCode(dsym);
            emitExtra(dextra, dnBits);
            // insert hashes for covered positions
            for (size_t k = i; k < i + (size_t)bestLen; ++k) {
                int hk = hashAt(k);
                if (hk >= 0) { prev[k] = head[hk]; head[hk] = (int)k; }
            }
            i += (size_t)bestLen;
        } else {
            emitLiteralSym(in[i]);
            if (h >= 0) { prev[i] = head[h]; head[h] = (int)i; }
            i += 1;
        }
    }
    // end-of-block symbol 256
    emitLiteralSym(256);
    bw.flush();
}

// ---------------- zlib wrapper ----------------
static uint32_t adler32(const uint8_t* d, size_t n) {
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < n; ++i) {
        a = (a + d[i]) % 65521;
        b = (b + a) % 65521;
    }
    return (b << 16) | a;
}

bool zlibInflate(const uint8_t* in, size_t inLen, std::vector<uint8_t>& out) {
    if (inLen < 6) return false;
    out.clear();
    uint8_t cmf = in[0], flg = in[1];
    if ((cmf & 0x0F) != 8) return false;
    if (((cmf << 8) + flg) % 31 != 0) return false;
    if (!inflateRaw(in + 2, inLen - 6, out)) return false;
    // Adler-32 trailer, MSB first per RFC 1950.
    uint32_t want = ((uint32_t)in[inLen - 4] << 24) | ((uint32_t)in[inLen - 3] << 16) |
                    ((uint32_t)in[inLen - 2] << 8) | ((uint32_t)in[inLen - 1]);
    return adler32(out.data(), out.size()) == want;
}

void zlibDeflate(const uint8_t* in, size_t inLen, std::vector<uint8_t>& out) {
    out.clear();
    out.push_back(0x78); out.push_back(0x01);
    std::vector<uint8_t> body;
    deflateData(in, inLen, body);
    out.insert(out.end(), body.begin(), body.end());
    uint32_t a = adler32(in, inLen);
    out.push_back((uint8_t)((a >> 24) & 0xFF)); out.push_back((uint8_t)((a >> 16) & 0xFF));
    out.push_back((uint8_t)((a >> 8) & 0xFF)); out.push_back((uint8_t)(a & 0xFF));
}

// ---------------- ZIP ----------------
static void put16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back((uint8_t)(x & 0xFF)); v.push_back((uint8_t)(x >> 8));
}
static void put32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back((uint8_t)(x & 0xFF)); v.push_back((uint8_t)((x >> 8) & 0xFF));
    v.push_back((uint8_t)((x >> 16) & 0xFF)); v.push_back((uint8_t)((x >> 24) & 0xFF));
}
static uint16_t get16(const std::vector<uint8_t>& v, size_t i) {
    return (uint16_t)(v[i] | (v[i + 1] << 8));
}
static uint32_t get32(const std::vector<uint8_t>& v, size_t i) {
    return (uint32_t)v[i] | ((uint32_t)v[i + 1] << 8) | ((uint32_t)v[i + 2] << 16) | ((uint32_t)v[i + 3] << 24);
}

std::vector<uint8_t> ZipWriter::build() const {
    std::vector<uint8_t> out;
    struct CD { std::string name; uint32_t crc, compSize, uncompSize, offset; uint16_t method; };
    std::vector<CD> cds;
    for (const Entry& e : entries) {
        uint32_t crc = crc32(e.data.data(), e.data.size());
        std::vector<uint8_t> stored;
        uint16_t method = 0;
        std::vector<uint8_t> payload;
        if (e.store) {
            payload = e.data;
            method = 0;
        } else {
            std::vector<uint8_t> z;
            zlibDeflate(e.data.data(), e.data.size(), z);
            z.erase(z.begin(), z.begin() + 2);   // strip zlib header
            z.resize(z.size() - 4);              // strip adler tail
            payload = std::move(z);
            method = 8;
        }
        uint32_t offset = (uint32_t)out.size();
        // local file header
        put32(out, 0x04034b50);
        put16(out, 20);           // version needed
        put16(out, 0);            // flags
        put16(out, method);
        put16(out, 0); put16(out, 0);            // time, date
        put32(out, crc);
        put32(out, (uint32_t)payload.size());
        put32(out, (uint32_t)e.data.size());
        put16(out, (uint16_t)e.name.size());
        put16(out, 0);            // extra len
        out.insert(out.end(), e.name.begin(), e.name.end());
        out.insert(out.end(), payload.begin(), payload.end());
        cds.push_back(CD{e.name, crc, (uint32_t)payload.size(), (uint32_t)e.data.size(), offset, method});
    }
    uint32_t cdStart = (uint32_t)out.size();
    for (const CD& c : cds) {
        put32(out, 0x02014b50);
        put16(out, 20); put16(out, 20);
        put16(out, 0); put16(out, c.method);
        put16(out, 0); put16(out, 0);
        put32(out, c.crc);
        put32(out, c.compSize);
        put32(out, c.uncompSize);
        put16(out, (uint16_t)c.name.size());
        put16(out, 0); put16(out, 0); put16(out, 0); // extra len, comment len, disk start
        put16(out, 0);                                // internal attrs
        put32(out, 0);                                // external attrs
        put32(out, c.offset);
        out.insert(out.end(), c.name.begin(), c.name.end());
    }
    uint32_t cdSize = (uint32_t)out.size() - cdStart;
    put32(out, 0x06054b50);
    put16(out, 0); put16(out, 0);
    put16(out, (uint16_t)cds.size()); put16(out, (uint16_t)cds.size());
    put32(out, cdSize);
    put32(out, cdStart);
    put16(out, 0);
    return out;
}

bool ZipReader::open(const std::vector<uint8_t>& b) {
    bytes_ = b;
    entries_.clear();
    error_.clear();
    if (b.size() < 22) { error_ = "archive too small"; return false; }
    // find EOCD from the back
    ssize_t eocd = -1;
    for (ssize_t i = (ssize_t)b.size() - 22; i >= 0 && i >= (ssize_t)b.size() - 65557; --i) {
        if (get32(b, (size_t)i) == 0x06054b50) { eocd = i; break; }
    }
    if (eocd < 0) { error_ = "no EOCD"; return false; }
    uint16_t n = get16(b, (size_t)eocd + 10);
    uint32_t cdStart = get32(b, (size_t)eocd + 16);
    size_t p = cdStart;
    for (int i = 0; i < n; ++i) {
        if (p + 46 > b.size() || get32(b, p) != 0x02014b50) { error_ = "bad central directory"; return false; }
        Entry e;
        e.method = get16(b, p + 10);
        e.compSize = get32(b, p + 20);
        e.uncompSize = get32(b, p + 24);
        uint16_t nameLen = get16(b, p + 28);
        uint16_t extraLen = get16(b, p + 30);
        uint16_t commentLen = get16(b, p + 32);
        e.offset = get32(b, p + 42);
        if (p + 46 + nameLen > b.size()) { error_ = "bad name"; return false; }
        e.name.assign((const char*)b.data() + p + 46, nameLen);
        p += 46 + nameLen + extraLen + commentLen;
        entries_.push_back(std::move(e));
    }
    return true;
}

bool ZipReader::exists(const std::string& name) const {
    for (const Entry& e : entries_) if (e.name == name) return true;
    return false;
}

bool ZipReader::read(const std::string& name, std::vector<uint8_t>& out) const {
    const Entry* found = nullptr;
    for (const Entry& e : entries_) if (e.name == name) { found = &e; break; }
    if (!found) { return false; }
    if (found->offset + 30 > bytes_.size()) return false;
    if (get32(bytes_, found->offset) != 0x04034b50) return false;
    uint16_t nameLen = get16(bytes_, found->offset + 26);
    uint16_t extraLen = get16(bytes_, found->offset + 28);
    size_t dataStart = found->offset + 30 + nameLen + extraLen;
    if (dataStart + found->compSize > bytes_.size()) return false;
    const uint8_t* d = bytes_.data() + dataStart;
    if (found->method == 0) {
        out.assign(d, d + found->compSize);
        return true;
    }
    if (found->method == 8) return inflateRaw(d, found->compSize, out);
    return false;
}

bool ZipReader::readText(const std::string& name, std::string& out) const {
    std::vector<uint8_t> bytes;
    if (!read(name, bytes)) return false;
    out.assign(bytes.begin(), bytes.end());
    return true;
}

} // namespace leptid
