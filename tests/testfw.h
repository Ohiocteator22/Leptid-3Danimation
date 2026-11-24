// Leptid — shared test framework (assert macros + entry point declarations).
#pragma once
#include <cmath>
#include <cstdio>

namespace testfw {
extern int gPassed, gFailed;
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
