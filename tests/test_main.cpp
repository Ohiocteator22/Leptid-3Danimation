// Leptid — engine test suite (assert-based, no external framework).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "testfw.h"
#include "core/image.h"
#include "formats/json.h"
#include "formats/zipcodec.h"
#include "formats/pngio.h"
#include <filesystem>
#if defined(_MSC_VER) // MSVC names the pipe-to-process functions _popen/_pclose
#define popen _popen
#define pclose _pclose
#endif

using namespace leptid;

namespace testfw {
int gPassed = 0, gFailed = 0;
}

static void testMath() {
    Vec2 a(3, 4);
    CHECK(std::fabs(a.len() - 5) < 1e-5);
    Mat3 rot = Mat3::rotation(degToRad(90));
    Vec2 r = rot.apply(1, 0);
    CHECK(std::fabs(r.x) < 1e-5 && std::fabs(r.y - 1) < 1e-5);
    Mat3 inv = rot.inverse();
    Vec2 back = inv.apply(r);
    CHECK(std::fabs(back.x - 1) < 1e-4 && std::fabs(back.y) < 1e-4);
    // clampAngleRange: elbow limit
    CHECK(clampAngleRange(degToRad(200), degToRad(0), degToRad(150)) == degToRad(150));
    CHECK(clampAngleRange(degToRad(-170), degToRad(150), degToRad(-150)) == degToRad(-170)); // inside wrap arc
    CHECK(std::fabs(lerpAngle(degToRad(170), degToRad(-170), 0.5f) - degToRad(180)) < 1e-4 ||
          std::fabs(normalizeAngle(lerpAngle(degToRad(170), degToRad(-170), 0.5f))) > degToRad(179));
}

static void testImage() {
    Image img(64, 64);
    img.fillCircle(32, 32, 10, Color(255, 0, 0, 255));
    CHECK(img.opaquePixelCount() > 250 && img.opaquePixelCount() < 500);
    Image sub = img.crop(24, 24, 16, 16);
    CHECK(sub.at(8, 8) == Color(255, 0, 0, 255).pack());
    // blit with opacity
    Image dst(32, 32);
    dst.clear(Color(0, 0, 0, 255));
    Image src(8, 8);
    src.clear(Color(255, 255, 255, 255));
    dst.blit(src, 4, 4, 0.5f);
    Color c = Color::unpack(dst.at(5, 5));
    CHECK(c.r == 127 || c.r == 128); // 0.5 blend
    CHECK(c.a == 255);
    // transformed blit: scale x2
    Image dst2(64, 64);
    Mat3 m = Mat3::translation(8, 8) * Mat3::scale(2.0f);
    dst2.blitTransformed(src, m);
    CHECK(dst2.opaquePixelCount() == 256);
    // resized
    Image small = dst2.resized(16, 16);
    CHECK(small.w == 16 && small.opaquePixelCount() > 0);
}

static void testJson() {
    Json j = Json::object();
    j.set("name", Json::of(std::string("leptid")));
    j.set("fps", Json::of(24));
    j.set("scale", Json::of(1.5));
    Json arr = Json::array();
    arr.push(Json::of(1));
    arr.push(Json::of(2.5));
    j.set("arr", arr);
    Json nested = Json::object();
    nested.set("onion", Json::of(true));
    j.set("opts", nested);
    std::string text = j.dump();
    Json back;
    CHECK(Json::parse(text, back));
    CHECK(back.str("name") == "leptid");
    CHECK(back.integer("fps", 0) == 24);
    CHECK(std::fabs(back.num("scale", 0) - 1.5) < 1e-9);
    CHECK(back.get("arr") && back.get("arr")->isArray() && back.get("arr")->arrayValue.size() == 2);
    CHECK(back.boolean("opts/missing", true) == true);
    const Json* opts = back.get("opts");
    CHECK(opts && opts->boolean("onion", false) == true);
    // unicode + escapes
    Json u = Json::object();
    u.set("t", Json::of(std::string("quote\" back\\slash \xE2\x9C\x93 newline\n")));
    std::string ut = u.dump();
    Json ub;
    CHECK(Json::parse(ut, ub));
    CHECK(ub.str("t").find("\xE2\x9C\x93") != std::string::npos);
    CHECK(ub.str("t").find('\n') != std::string::npos);
    // malformed
    Json bad;
    CHECK(!Json::parse("{\"a\":}", bad));
    CHECK(!Json::parse("{\"a\":1", bad));
}

static void testCrcDeflate() {
    const char* s = "Hello, Leptid compression! Hello, Leptid compression! Hello!";
    std::vector<uint8_t> data(s, s + strlen(s));
    // roundtrip my own deflate
    std::vector<uint8_t> comp, back;
    deflateData(data.data(), data.size(), comp);
    CHECK(inflateData(comp.data(), comp.size(), back));
    CHECK(back.size() == data.size() && memcmp(back.data(), data.data(), data.size()) == 0);
    // larger, more repetitive data
    std::vector<uint8_t> big;
    for (int i = 0; i < 10000; ++i) {
        const char* w = "the quick brown fox jumps over the lazy dog ";
        big.insert(big.end(), w, w + strlen(w));
    }
    deflateData(big.data(), big.size(), comp);
    CHECK(inflateData(comp.data(), comp.size(), back));
    CHECK(back == big);
    CHECK(comp.size() < big.size() / 2); // must actually compress repeats
    // zlib wrapper
    std::vector<uint8_t> z;
    zlibDeflate(big.data(), big.size(), z);
    CHECK(zlibInflate(z.data(), z.size(), back));
    CHECK(back == big);
}

static void testZipRoundtrip() {
    ZipWriter zw;
    zw.add("project.json", "{\"version\":3,\"fps\":24}");
    std::vector<uint8_t> bin(1000);
    for (size_t i = 0; i < bin.size(); ++i) bin[i] = (uint8_t)(i * 7);
    zw.add("layers/limb_0.png", bin, /*store*/ true);
    zw.add("characters/hero.json", "{\"bones\":[\"head\",\"torso\"]}");
    auto bytes = zw.build();
    ZipReader zr;
    CHECK(zr.open(bytes));
    CHECK(zr.entries().size() == 3);
    CHECK(zr.exists("project.json"));
    std::string text;
    CHECK(zr.readText("project.json", text));
    CHECK(text == "{\"version\":3,\"fps\":24}");
    std::vector<uint8_t> out;
    CHECK(zr.read("layers/limb_0.png", out));
    CHECK(out == bin);
    CHECK(zr.readText("characters/hero.json", text));
    CHECK(text.find("torso") != std::string::npos);
    CHECK(!zr.read("missing.json", out));
}

static void testZipInterop() {
    // Windows runners ship `python`, not `python3`; the temp zip must live in
    // the real temp directory, not /tmp (which does not exist on Windows).
#ifdef _WIN32
    const char* py = "python";
#else
    const char* py = "python3";
#endif
    std::string tmpZip =
        (std::filesystem::temp_directory_path() / "leptid_ours.zip").generic_string();

    // 1) Read a real python-zipfile-produced deflate zip with our reader.
    std::string cmd = std::string(py) + " -c \"\n"
                    "import zipfile,io,sys\n"
                    "b=io.BytesIO()\n"
                    "z=zipfile.ZipFile(b,'w',zipfile.ZIP_DEFLATED)\n"
                    "data=('abcdefgh'*2000).encode()\n"
                    "z.writestr('a.txt',data)\n"
                    "z.writestr('dir/b.bin',bytes(range(256))*40)\n"
                    "z.close()\n"
                    "sys.stdout.buffer.write(b.getvalue())\n\"";
    FILE* p = popen(cmd.c_str(), "r");
    std::vector<uint8_t> pyZip;
    if (p) {
        char buf[4096];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, p)) > 0) pyZip.insert(pyZip.end(), buf, buf + n);
        pclose(p);
    }
    CHECK(!pyZip.empty());
    ZipReader zr;
    CHECK(zr.open(pyZip));
    CHECK(zr.entries().size() == 2);
    std::string t;
    CHECK(zr.readText("a.txt", t) && t.size() == 16000);
    bool patternOk = t.size() == 16000;
    for (size_t i = 0; patternOk && i < t.size(); ++i)
        patternOk = t[i] == (char)('a' + (i % 8));
    CHECK(patternOk);
    std::vector<uint8_t> bb;
    CHECK(zr.read("dir/b.bin", bb) && bb.size() == 10240);
    bool ok = true;
    for (size_t i = 0; i < bb.size(); ++i) if (bb[i] != (uint8_t)(i % 256)) { ok = false; break; }
    CHECK(ok);

    // 2) Our zip read by python zipfile (roundtrip + deflate check).
    ZipWriter zw;
    zw.add("x.json", "{\"ok\":true,\"n\":3}");
    std::vector<uint8_t> payload(5000, 42);
    zw.add("d/payload", payload);
    auto ours = zw.build();
    FILE* pf = fopen(tmpZip.c_str(), "wb");
    CHECK(pf != nullptr);
    if (pf) {
        fwrite(ours.data(), 1, ours.size(), pf);
        fclose(pf);
    }
    std::string cmd2 = std::string(py) + " -c \"\n"
                     "import zipfile\n"
                     "z=zipfile.ZipFile('" + tmpZip + "')\n"
                     "assert z.testzip() is None, 'crc fail'\n"
                     "assert z.read('x.json').decode()=='{\\\"ok\\\":true,\\\"n\\\":3}'\n"
                     "assert z.read('d/payload')==b'\\x2a'*5000\n"
                     "assert z.getinfo('d/payload').compress_type==8\n"
                     "print('PY_OK')\"";
    FILE* pr = popen(cmd2.c_str(), "r");
    CHECK(pr != nullptr);
    if (pr) {
        char buf[64] = {0};
        fread(buf, 1, 31, pr);
        CHECK(strstr(buf, "PY_OK") != nullptr);
        int rc = pclose(pr);
        CHECK(rc == 0);
    }
}

static void testPngRoundtrip() {
    Image img(50, 40);
    img.fillDisc(25, 20, 15, Color(30, 144, 255, 200));
    auto bytes = savePngMemory(img);
    CHECK(bytes.size() > 100);
    Image back;
    CHECK(loadPngMemory(bytes.data(), bytes.size(), back));
    CHECK(back.w == 50 && back.h == 40);
    // soft-edge disc alpha changes pixels, so compare loosely: center must match
    CHECK(back.at(25, 20) == img.at(25, 20));
    CHECK(back.px == img.px); // PNG is lossless
    // file path roundtrip
    CHECK(savePng("/tmp/leptid_test.png", img));
    Image fileImg;
    CHECK(loadPng("/tmp/leptid_test.png", fileImg));
    CHECK(fileImg.px == img.px);
}

void runCoreFormatTests() {
    testMath();
    testImage();
    testJson();
    testCrcDeflate();
    testZipRoundtrip();
    testZipInterop();
    testPngRoundtrip();
}
