// Leptid — shared test framework (assert macros + entry point declarations).
#pragma once
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace testfw {
extern int gPassed, gFailed;

// Scratch file path that works on every CI platform. A hardcoded "/tmp/..."
// breaks on Windows, where fopen("/tmp/x") resolves to "<drive>:\tmp\x" and
// that directory does not exist (saves fail, loads "cannot open"). %TEMP% is
// always writable on Windows runners; /tmp is standard on Linux/macOS.
inline std::string tmpFile(const char* name) {
#ifdef _WIN32
    const char* t = std::getenv("TEMP");
    if (!t || !*t) t = std::getenv("TMP");
    std::string dir = (t && *t) ? t : ".";
    if (dir.back() != '/' && dir.back() != '\\') dir += '\\';
    return dir + name;
#else
    return std::string("/tmp/") + name;
#endif
}
}

#define CHECK(cond) do { \
    if (cond) { ++testfw::gPassed; } \
    else { ++testfw::gFailed; fprintf(stderr, "FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

#define CHECK_NEAR(a, b, eps) do { \
    if (std::fabs((a) - (b)) <= (eps)) { ++testfw::gPassed; } \
    else { ++testfw::gFailed; fprintf(stderr, "FAIL %s:%d  %s (got %.6f want %.6f)\n", \
        __FILE__, __LINE__, #a, (double)(a), (double)(b)); } \
} while (0)

// test suites
void runCoreFormatTests();   // tests/test_main.cpp
void runRigTests();          // tests/test_rig.cpp
void runModelTests();        // tests/test_model.cpp
void runUiTests();           // tests/test_ui.cpp
void runAudioTests();        // tests/test_audio.cpp
