// Which releases a Windows install updates to, as the game's updater does (runtime/src/update/update.cpp takes): a
// release install takes releases; a beta install takes betas and the releases after them (its final release among
// them). Each release has its own Setup: a beta's has its own name.
#pragma once

constexpr bool TakesRelease(bool installedBeta, bool releaseBeta) { return installedBeta || !releaseBeta; }

constexpr const wchar_t* SetupAssetName(bool releaseBeta) { return releaseBeta ? L"Nebula-Beta-Setup.exe" : L"Nebula-Setup.exe"; }
