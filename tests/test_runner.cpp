// Leptid — test binary entry point.
#include "testfw.h"
#include <cstdio>

int main() {
    runCoreFormatTests();
    runRigTests();
    runModelTests();
    runUiTests();
    runAudioTests();
    printf("leptid tests: %d passed, %d failed\n", testfw::gPassed, testfw::gFailed);
    return testfw::gFailed == 0 ? 0 : 1;
}
