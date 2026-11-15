// SPDX-License-Identifier: GPL-3.0-only
// Whether the game's window is a desktop window, for the features only a desktop has.
#pragma once

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace galaxy {

// Windows, Linux and macOS: another program's window can come in front of the game's (ON LOSING FOCUS). Android and
// iOS show one app in front, and their hosts suspend the game when it leaves.
#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && TARGET_OS_OSX)
inline constexpr bool kDesktopWindow = true;
#else
inline constexpr bool kDesktopWindow = false;
#endif

}  // namespace galaxy
