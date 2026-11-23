// Which releases a Windows install updates to, as the game's updater does (runtime/src/update/update.cpp takes): a
// release install takes releases; a beta install takes betas and the releases after them (its final release among
// them). Each release has its own Setup: a beta's has its own name.
#pragma once

#include <array>
#include <string>

// MAJOR.MINOR.PATCH[-label.N], the order of runtime/src/update/update.cpp compare_versions (and of the Android launcher's
// Updates.compareVersions): the three numbers, then a release after its pre-releases, then the pre-release's number. 2.0.0
// is above 2.0.0-beta.2, which is above 2.0.0-beta.1: a compare of the three numbers alone would call them equal.
inline int CompareReleaseVersions(const std::wstring& a, const std::wstring& b) {
    const auto number = [](const std::wstring& text, long long& value) {
        value = 0;
        if (text.empty()) return false;
        for (const wchar_t c : text) {
            if (c < L'0' || c > L'9' || value > 1000000000) return false;
            value = value * 10 + (c - L'0');
        }
        return true;
    };
    const auto key = [&number](const std::wstring& version) {
        std::array<long long, 5> k{};
        if (version.empty()) return k;
        const size_t dash = version.find(L'-');
        const std::wstring numbers = version.substr(0, dash);
        size_t at = 0;
        for (int i = 0; i < 3 && at <= numbers.size(); i++) {
            const size_t dot = numbers.find(L'.', at);
            long long value = 0;
            k[static_cast<size_t>(i)] = number(numbers.substr(at, dot == std::wstring::npos ? std::wstring::npos : dot - at), value) ? value : 0;
            if (dot == std::wstring::npos) break;
            at = dot + 1;
        }
        k[3] = dash == std::wstring::npos ? 1 : 0;
        if (dash != std::wstring::npos) {
            const std::wstring label = version.substr(dash + 1);
            const size_t dot = label.rfind(L'.');
            long long value = 0;
            if (dot != std::wstring::npos && number(label.substr(dot + 1), value)) k[4] = value;
        }
        return k;
    };
    const auto x = key(a), y = key(b);
    for (size_t i = 0; i < x.size(); i++) {
        if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    }
    return 0;
}

constexpr bool TakesRelease(bool installedBeta, bool releaseBeta) { return installedBeta || !releaseBeta; }

constexpr const wchar_t* SetupAssetName(bool releaseBeta) { return releaseBeta ? L"Nebula-Beta-Setup.exe" : L"Nebula-Setup.exe"; }
