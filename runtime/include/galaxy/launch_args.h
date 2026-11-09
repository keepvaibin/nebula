// SPDX-License-Identifier: GPL-3.0-only
// What the command line names (docs/COMMAND_LINE.md), in every build. The host's main fills it once, before any other
// thread starts; everything after reads it.
#pragma once

#include <filesystem>

namespace galaxy {

struct LaunchArgs {
    std::filesystem::path data;         // --data: Nebula's data folder; empty: the platform's (platform_paths.h)
    std::filesystem::path settings;     // --settings: the folder of config.ini and runtime_settings.ini; empty: the data folder
    std::filesystem::path crash_dumps;  // --crash-dumps: crash dumps and reports; empty: <data>/crashes
    unsigned gpu_validation = 0u;       // --gpu-validation: 1 the graphics API's validation layer, 2 also Direct3D 12's GPU-based
    bool console_log = false;           // --console-log 1: the log stays on the console, not logs/latest.log
    unsigned gpu_start_retry = 0u;      // --gpu-start-retry: starts so far that found no graphics memory free
};

inline LaunchArgs g_launch_args;

[[nodiscard]] inline const LaunchArgs& launch_args() noexcept {
    return g_launch_args;
}

}  // namespace galaxy
