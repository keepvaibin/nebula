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
    // The 2.0.0 beta: its player is offered the true 2.0.0, and a 1.0.2 player is not offered the beta.
    check(CompareReleaseVersions(L"2.0.0", L"2.0.0-beta.1") > 0 && CompareReleaseVersions(L"2.0.0-beta.1", L"2.0.0") < 0,
          "2.0.0 is above 2.0.0-beta.1 (three numbers alone would call them equal)");
    check(CompareReleaseVersions(L"2.0.0-beta.1", L"2.0.0-beta.1") == 0 && CompareReleaseVersions(L"2.0.0-beta.2", L"2.0.0-beta.1") > 0 &&
              CompareReleaseVersions(L"2.0.0-beta.10", L"2.0.0-beta.9") > 0,
          "a beta's number counts");
    check(CompareReleaseVersions(L"2.0.0-beta.1", L"1.0.2") > 0 && CompareReleaseVersions(L"2.0.1", L"2.0.0") > 0 &&
              CompareReleaseVersions(L"2.0.10", L"2.0.9") > 0 && CompareReleaseVersions(L"", L"0.0.0") < 0,
          "the beta is above 1.0.2, numbers compare as numbers");
    check(TakesRelease(true, false) && !TakesRelease(false, true),
          "a beta install takes 2.0.0; a 1.0.2 install takes no beta");
    if (!passed) return 1;
    std::printf("release channel tests passed\n");
    return 0;
}
