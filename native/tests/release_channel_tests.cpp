// Windows Setup's update channels (installer/native/release_channel.h): a beta install is offered its final release
// (P-092: it was offered betas only).
#include "../../installer/native/release_channel.h"

#include <cstdio>
#include <cwchar>

int main() {
    bool passed = true;
    const auto check = [&passed](bool condition, const char* what) {
        if (!condition) {
            std::printf("FAILED: %s\n", what);
            passed = false;
        }
    };
    check(TakesRelease(true, false), "a beta install takes a release");
    check(TakesRelease(true, true), "a beta install takes a beta");
    check(TakesRelease(false, false), "a release install takes a release");
    check(!TakesRelease(false, true), "a release install takes no beta");
    check(std::wcscmp(SetupAssetName(false), L"Nebula-Setup.exe") == 0 &&
              std::wcscmp(SetupAssetName(true), L"Nebula-Beta-Setup.exe") == 0,
          "each release's own Setup");
    if (!passed) return 1;
    std::printf("release channel tests passed\n");
    return 0;
}
