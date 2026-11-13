// SPDX-License-Identifier: GPL-3.0-only
// What the command line names (docs/COMMAND_LINE.md), in every build. The host's main fills it once, before any other
// thread starts; everything after reads it.
#pragma once

#include <filesystem>
#include <string_view>
#include <vector>

namespace galaxy {

// --diagnostics <what>[,<what>] (docs/DIAGNOSTICS.md): what a run records, into the log and the diagnostics folder
// (platform_paths.h). Nothing is recorded without it.
enum Diagnostic : unsigned {
    kDiagnoseTimeline = 1u << 0u,        // timeline: the frame pipeline's events, written at exit (frame_timeline.h)
    kDiagnoseFrameTails = 1u << 1u,      // frame-tails: the frame intervals' histogram every 2 s
    kDiagnosePointerLatency = 1u << 2u,  // pointer-latency: the pointer's selection to the picture showing it
    kDiagnosePresentStats = 1u << 3u,    // present-stats: presented and new frames, GPU and CPU times, each second
    kDiagnoseFrameTimes = 1u << 4u,      // frame-times: each frame's time and where a scripted press begins
    kDiagnoseGpuPasses = 1u << 5u,       // gpu-passes: GPU time by pass and the shader invocation counts
    kDiagnoseMemory = 1u << 6u,          // memory: what the renderer holds, every 600 frames
    kDiagnoseHeaps = 1u << 7u,           // heaps: memory's lines and the process's heaps by block size
    kDiagnoseThreadCpu = 1u << 8u,       // thread-cpu: each thread's CPU time each second
    kDiagnoseProfile = 1u << 9u,         // profile: the game thread sampled each millisecond (game-profile-<process>.txt)
    kDiagnoseSceneLoads = 1u << 10u,     // scene-loads: each scene change's phases and placed objects
    kDiagnoseFileReads = 1u << 11u,      // file-reads: each read and map of the game's files, with its frame
    kDiagnosePipelines = 1u << 12u,      // pipelines: the pipelines the run created (pipelines-<process>.txt)
    kDiagnoseRtInputs = 1u << 13u,       // rt-inputs: what ray tracing reads of each frame list, checked and counted
    kDiagnoseRtView = 1u << 14u,         // rt-view: the tracer's scene drawn over the game's, each instance its own colour
    kDiagnoseRtCheck = 1u << 15u,        // rt-check: the tracer's scene against the depth the game drew, coloured and counted
    kDiagnoseDeviceFaults = 1u << 16u,   // device-faults: a lost device's breadcrumbs and the driver's fault data
    kDiagnoseTelemetry = 1u << 17u,      // telemetry: frame counts in shared memory each second (tools/telemetry)
    kDiagnoseRingStress = 1u << 18u,     // ring-stress: the upload rings made again every kRingStressFrames frames (a test
                                         // of their retirement: the old rings must outlive every frame that reads them)
};

// The names --diagnostics takes, each one item or a set. "1" is the launcher's recording.
struct DiagnosticName {
    std::string_view name;
    unsigned what;
};
inline constexpr unsigned kDiagnoseFrames =
    kDiagnoseTimeline | kDiagnoseFrameTails | kDiagnosePointerLatency | kDiagnosePresentStats | kDiagnoseFrameTimes;
inline constexpr DiagnosticName kDiagnosticNames[] = {
    {"timeline", kDiagnoseTimeline},
    {"frame-tails", kDiagnoseFrameTails},
    {"pointer-latency", kDiagnosePointerLatency},
    {"present-stats", kDiagnosePresentStats},
    {"frame-times", kDiagnoseFrameTimes},
    {"gpu-passes", kDiagnoseGpuPasses},
    {"memory", kDiagnoseMemory},
    {"heaps", kDiagnoseMemory | kDiagnoseHeaps},
    {"thread-cpu", kDiagnoseThreadCpu},
    {"profile", kDiagnoseProfile},
    {"scene-loads", kDiagnoseSceneLoads},
    {"file-reads", kDiagnoseFileReads},
    {"pipelines", kDiagnosePipelines},
    {"rt-inputs", kDiagnoseRtInputs},
    {"rt-view", kDiagnoseRtView},
    {"rt-check", kDiagnoseRtCheck},
    {"device-faults", kDiagnoseDeviceFaults},
    {"telemetry", kDiagnoseTelemetry},
    {"ring-stress", kDiagnoseRingStress},
    {"frames", kDiagnoseFrames},
    {"gpu", kDiagnoseGpuPasses},
    {"threads", kDiagnoseThreadCpu | kDiagnoseProfile},
    {"loads", kDiagnoseSceneLoads | kDiagnoseFileReads},
    {"1", kDiagnoseTimeline | kDiagnoseFrameTails | kDiagnosePointerLatency | kDiagnosePresentStats | kDiagnoseGpuPasses},
    {"0", 0u},
};

// The items a comma-separated list names; false (and `unknown` the first name it does not know) when one is not a name.
[[nodiscard]] inline bool diagnostics_from_text(std::string_view list, unsigned& what, std::string_view& unknown) noexcept {
    what = 0u;
    if (list.empty()) {
        return true;
    }
    for (;;) {
        const std::size_t comma = list.find(',');
        const std::string_view name = list.substr(0u, comma);
        bool known = false;
        for (const DiagnosticName& entry : kDiagnosticNames) {
            if (entry.name == name) {
                what |= entry.what;
                known = true;
                break;
            }
        }
        if (!known) {
            unknown = name;
            return false;
        }
        if (comma == std::string_view::npos) {
            return true;
        }
        list = list.substr(comma + 1u);
    }
}

struct LaunchArgs {
    std::filesystem::path data;         // --data: Nebula's data folder; empty: the platform's (platform_paths.h)
    std::filesystem::path settings;     // --settings: the folder of config.ini and runtime_settings.ini; empty: the data folder
    std::filesystem::path crash_dumps;  // --crash-dumps: crash dumps and reports; empty: <data>/crashes
    std::filesystem::path timeline;     // --timeline: the recorded timeline's file; empty: the diagnostics folder's
    unsigned diagnostics = 0u;          // --diagnostics: Diagnostic bits
    unsigned gpu_validation = 0u;       // --gpu-validation: 1 the graphics API's validation layer, 2 also Direct3D 12's GPU-based
    bool console_log = false;           // --console-log 1: the log stays on the console, not logs/latest.log
    // Started with its window hidden (Windows: STARTUPINFO's SW_HIDE, as a test starts it): no window, dialog or Discord
    // status, and no keyboard, mouse, controller motor or Wii Remote of the player's.
    bool started_hidden = false;
    unsigned gpu_start_retry = 0u;      // --gpu-start-retry: starts so far that found no graphics memory free
    std::vector<std::filesystem::path> extensions;  // --extension, each time given: host extensions (host_extension.h)
    // --import, each time given: mod files or folders imported as a drop on the window imports them, the result page
    // after (task 6.1). "Open with" and the .luma association start the game with it; a game already running for the
    // same folder takes them (show_existing_instance).
    std::vector<std::filesystem::path> imports;
    bool safe_start = false;  // --safe-start 1: every mod off for this start, the player's choices kept (task 6.6)
};

inline LaunchArgs g_launch_args;

[[nodiscard]] inline const LaunchArgs& launch_args() noexcept {
    return g_launch_args;
}

// ring-stress: how often the upload rings are made again (frames): often enough that a run of a test's length makes
// them again hundreds of times, with frames in flight on both sides of each.
inline constexpr unsigned kRingStressFrames = 5u;

// Whether this run records any of `what` (Diagnostic bits).
[[nodiscard]] inline bool diagnosing(unsigned what) noexcept {
    return (g_launch_args.diagnostics & what) != 0u;
}

}  // namespace galaxy
