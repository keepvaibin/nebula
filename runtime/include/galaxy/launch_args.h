// SPDX-License-Identifier: GPL-3.0-only
// What the command line names (docs/using-nebula/command-line-arguments.md), in every build. The host's main fills it once, before any other
// thread starts; everything after reads it.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy {

// --diagnostics <what>[,<what>] (docs/using-nebula/diagnostics.md): what a run records, into the log and the diagnostics folder
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
    kDiagnoseProfile = 1u << 9u,         // profile: the game and render threads sampled each millisecond (<thread>-profile-<process>.txt)
    kDiagnoseSceneLoads = 1u << 10u,     // scene-loads: each scene change's phases and placed objects
    kDiagnoseFileReads = 1u << 11u,      // file-reads: each read and map of the game's files, with its frame
    kDiagnosePipelines = 1u << 12u,      // pipelines: the pipelines the run created (pipelines-<process>.txt)
    kDiagnoseDeviceFaults = 1u << 13u,   // device-faults: a lost device's breadcrumbs and the driver's fault data
    kDiagnoseTelemetry = 1u << 14u,      // telemetry: frame counts in shared memory each second (tools/telemetry)
    kDiagnoseProgramsDrawn = 1u << 15u,  // programs-drawn: each program pair a draw asks for, once (a stage compiles only those)
    kDiagnoseRingStress = 1u << 16u,     // ring-stress: the upload rings made again every kRingStressFrames frames (a test
                                         // of their retirement: the old rings must outlive every frame that reads them)
    kDiagnoseResolutionSteps = 1u << 17u,  // resolution-steps: dynamic resolution on, walking every step its floor allows,
                                           // each held kResolutionStepsPictures scene pictures (a test of every pass at
                                           // every step, on a GPU that never needs one)
    kDiagnoseHdrHighlights = 1u << 18u,  // hdr-highlights: with HDR output, captures show the light past the paper white
                                         // (HdrPattern::Highlights) in place of the SDR picture
    kDiagnoseFramePlans = 1u << 19u,     // frame-plans: every 60th frame list's shape and the walk's plans of it
                                         // (frame-plans-<pid>.txt; walk_plan_tests' fixtures, ledger P-044)
    kDiagnoseConstantsWatch = 1u << 20u,  // constants-watch: Direct3D 11 checks a compute read of its constant ring once
                                          // a frame and prints "[constants] watch" on a wrong read (ledger P-332)
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
    {"device-faults", kDiagnoseDeviceFaults},
    {"telemetry", kDiagnoseTelemetry},
    {"programs-drawn", kDiagnoseProgramsDrawn},
    {"ring-stress", kDiagnoseRingStress},
    {"resolution-steps", kDiagnoseResolutionSteps},
    {"hdr-highlights", kDiagnoseHdrHighlights},
    {"frame-plans", kDiagnoseFramePlans},
    {"constants-watch", kDiagnoseConstantsWatch},
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
    // Started with its window hidden (Windows: STARTUPINFO's SW_HIDE, as a test starts it; elsewhere --hidden 1): no
    // window, dialog or Discord status, and no keyboard, mouse, controller motor or Wii Remote of the player's.
    bool started_hidden = false;
    // --no-focus 1 (a test's visible run, Windows; every platform accepts it and ignores it elsewhere): the game window never asks for
    // the keyboard focus (shown without activating, no foreground, active or focus call), and the game runs as it does with the focus:
    // the window never counts as "not in front" (no pause or mute on focus loss, no background rendering, the fine timer held).
    bool no_focus = false;
    bool timed = false;                 // --seconds over 0: a run that ends by itself, a test's
    // --start-check N: the program's start without the game's files (host_main.cpp run_start_check): the window and the
    // renderer, which may take a software adapter when no graphics card runs it, then N frames drawn and shown.
    unsigned start_check = 0u;
    // --software-device 1: the renderer takes only a software device (Direct3D 12 and 11: Microsoft's software adapter; Vulkan: a device of
    // the processor type); where there is none, the game ends at its start (renderer_choice.h adapter_allowed). A test's run on a computer
    // with no graphics card slot to spare. Without it a software adapter is taken only by the start check.
    bool software_device = false;
    unsigned gpu_start_retry = 0u;      // --gpu-start-retry: starts so far that found no graphics memory free
    unsigned started_again = 0u;        // --started-again: starts this run made again before this one
    // --start-again-of <id>-<time> (Windows): the process this start takes over from, by its id and creation time.
    std::string start_again_of;
    // --renderer d3d12|d3d11|vulkan (0: not given, the settings' renderer, render_config.h next_start_renderer): the
    // Direct3D 12 host runs NebulaD11 for d3d11 and NebulaVk for vulkan without starting Direct3D 12 (host_main.cpp
    // hand_off_to). A renderer given here never falls back to another when no card runs it.
    unsigned renderer = 0u;  // 1 d3d12, 2 d3d11, 3 vulkan (render::RendererKind)
    std::vector<std::filesystem::path> extensions;  // --extension, each time given: host extensions (host_extension.h)
    // --import, each time given: mod files or folders imported as a drop on the window imports them, the result page
    // after. "Open with" and the .luma association start the game with it; a game already running for the
    // same folder takes them (show_existing_instance).
    std::vector<std::filesystem::path> imports;
    bool safe_start = false;  // --safe-start 1: every mod off for this start, the player's choices kept
    // --shipped-mods: the folder of the .luma files that come with the game (mod_import.h install_shipped_mods); empty:
    // luma/mods in the package's folder (platform_paths.h package_directory). Android's app gives the folder it unpacks
    // them to.
    std::filesystem::path shipped_mods;
    // --mod-dev, each time given: a mod in dev mode (reload.md 4; `lumalink run` passes it): its reload requests
    // (<mods>/.reload/<id>.request) are taken while the game runs.
    std::vector<std::string> mod_dev;
    // Android (arm64): a Vulkan driver the player imported in place of the system's (the launcher's Graphics driver).
    // --gpu-driver: its package's folder; --gpu-driver-library: the driver's library in it (the package's meta.json
    // libraryName); --gpu-driver-hooks: the app's native library folder, which holds libadrenotools' hooks. The
    // launcher puts a file named "starting" in the folder before the start; the first frame shown removes it.
    std::filesystem::path gpu_driver;
    std::string gpu_driver_library;
    std::filesystem::path gpu_driver_hooks;
};

inline LaunchArgs g_launch_args;

[[nodiscard]] inline const LaunchArgs& launch_args() noexcept {
    return g_launch_args;
}

// ring-stress: how often the upload rings are made again (frames): often enough that a run of a test's length makes
// them again hundreds of times, with frames in flight on both sides of each.
inline constexpr unsigned kRingStressFrames = 5u;

// resolution-steps: the scene pictures each step is held for (a snapshot every this many pictures lands in each).
inline constexpr unsigned kResolutionStepsPictures = 120u;

// A player's own start: shown, not timed, and with no host extension (a test driver's). Only it may choose settings for
// the player (the first start's measured height).
[[nodiscard]] inline bool ordinary_start() noexcept {
    return !g_launch_args.started_hidden && !g_launch_args.timed && g_launch_args.start_check == 0u &&
           g_launch_args.extensions.empty();
}

// Whether this run records any of `what` (Diagnostic bits).
[[nodiscard]] inline bool diagnosing(unsigned what) noexcept {
    return (g_launch_args.diagnostics & what) != 0u;
}

}  // namespace galaxy
