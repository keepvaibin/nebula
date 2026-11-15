// SPDX-License-Identifier: GPL-3.0-only
// The benchmark (benchmark.h): its stages, the run on the game thread, the per-frame measurements, and the result file
// with its frame lists (benchmark_results.h). The arithmetic is benchmark_stats.h's.
//
// A run measures each stage twice, at the player's settings and then at the standard settings (render_config.h
// apply_benchmark_standard), then the probe (each optional pass alone) and the first stage again for the heat check.
// A step on the stage the step before measured stays there: only its settings change. Settings change only under full
// black (the run's cover, LiveState::cover, faded in before and out after), never on a picture the player sees. A
// travel's stage is shown from its scene's start, its opening and Mario's arrival, but measured only from play
// (NEBULA_CONTEXT_SCENE_PLAY), so every window of a stage measures the same scene. Before each window the game runs
// at its own pace, as in play, until the scene has made what it shows (its textures and pipelines): a stage's first
// seconds load, and a window that counted them would measure the load, not the frames a player sees for the rest of
// it. Each window then lasts a fixed time. The game keeps its own pace throughout, as in play; the pictures are its
// frames and Nebula's interpolated frames between them, without VSync and with no cap (render_config.h
// apply_benchmark_presenting), so the frame rate is what the device can show while the motion stays the game's own.
// No input reaches the game. Each shown picture is sampled on the render thread with the
// CPU time of the game and render threads and the work counters; each frame's GPU pass times come from the renderer
// as they are read back.
#include "galaxy/benchmark.h"

#include "galaxy/atomic_file.h"
#include "galaxy/benchmark_recommend.h"
#include "galaxy/benchmark_results.h"
#include "galaxy/benchmark_stats.h"
#include "galaxy/cpu_count.h"
#include "galaxy/memory_saves.h"
#include "galaxy/platform_paths.h"
#include "galaxy/platform_time.h"
#include "galaxy/render/galaxy_select.h"
#include "galaxy/render/host_window.h"
#include "galaxy/render/render_config.h"
#include "galaxy/render/resolution_fit.h"
#include "galaxy/render/rt_caps.h"
#include "galaxy/render_api.h"
#include "galaxy/runtime_settings.h"
#include "nebula_game_api.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include "galaxy/windows_api.h"
#include <intrin.h>
#include <psapi.h>
#else
#include <pthread.h>
#include <sys/resource.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#endif
#if defined(__ANDROID__)
#include <sys/system_properties.h>
#endif
#if defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#endif
#endif

namespace galaxy::bench {
namespace {

// ---- the stages ----------------------------------------------------------------------------------------------------

// Stage, scenario, kind (stage: measured at the player's settings and at the standard settings; grand6: the same with
// the Observatory's six Grand Stars, so its domes are built; probe: the stage the probe measures each configuration
// on) and measured seconds per window. Why each: one per cost class (open planets, bloom, water, particles, the hub).
// 15 seconds at each setting, 30 a stage: about three minutes of measured frames in all.
constexpr const char* kBuiltInStages =
    "CosmosGardenGalaxy\t1\tstage\t15\n"
    "BattleShipGalaxy\t1\tstage\t15\n"
    "HeavenlyBeachGalaxy\t1\tstage\t15\n"
    "HellProminenceGalaxy\t1\tstage\t15\n"
    "AstroGalaxy\t1\tgrand6\t15\n"
    "BattleShipGalaxy\t1\tprobe\t3\n";

// The warm-up before each window, paced as in play: at least kWarmFrames, then until kQuietFrames in a row made no
// texture and built no pipeline with none left to build, at most kWarmMostFrames (a scene that keeps making them).
constexpr std::uint32_t kWarmFrames = 60u;
constexpr std::uint32_t kQuietFrames = 60u;
constexpr std::uint32_t kWarmMostFrames = 1200u;
constexpr std::uint32_t kDrainFrames = 30u;      // not measured: the frames queued before the warm-up ended drain
// The pictures a window keeps room for before it begins: its seconds at the highest picture rate and a tenth again, at
// most this many (more grow the list).
constexpr std::size_t kMostReservedPictures = std::size_t{1} << 18;
constexpr std::uint32_t kTitleFrames = 120u;     // title frames before the first travel (after the reset, Options::here)
constexpr std::uint32_t kArriveFrames = 900u;    // scene frames a travel may go unanswered before it is asked again
constexpr std::uint32_t kPlayFrames = 3600u;     // scene frames an arrived stage may take to reach play (its opening)
constexpr std::int64_t kCoverNs = 300'000'000;   // the cover's fade to black or back
constexpr std::uint32_t kAsks = 3u;              // asks before the run fails
constexpr std::uint32_t kPipelineFrames = 1200u; // the longest wait for a settings change's pipelines
constexpr std::uint32_t kSettleFrames = 10u;     // frames after a settings change before its pipelines are counted
// A travel unanswered this long fails the run, scene frames or not: a load that never ends, or a screen that is not a
// GameScene, gives scene_frame no frame to count kArriveFrames with.
constexpr std::int64_t kArriveTimeoutNs = 120'000'000'000;

constexpr double kTargetFps = 60.0;
constexpr double kFitsLowFps = 58.0;          // a 1% low this high holds 60 FPS (a 3% allowance for timer noise)
constexpr double kDisplayTolerance = 0.03;    // a frame rate this close to the refresh rate is the display's
constexpr double kHeatJudgedGpuMs = 1.0;      // below this, a GPU's clocks rise and fall with load: no heat verdict
constexpr std::size_t kSlowestFrames = 10u;   // slow frames kept per stage, with their causes
constexpr std::uint32_t kGpuReadbackFrames = 4u;  // a frame's GPU time arrives this many pictures late at most
constexpr std::size_t kPassSlots = 24u;

struct Stage {
    std::string id;
    std::uint32_t scenario = 1u;
    bool grand = false;
    std::uint32_t seconds = 15u;  // each window's measured time
};

bool same_scene(const Stage& a, const Stage& b) {
    return a.id == b.id && a.scenario == b.scenario && a.grand == b.grand;
}

bool known_stage(const std::string& id, std::uint32_t scenario) {
    for (const render::GalaxyStage& stage : render::kGalaxyStages) {
        if (id == stage.id) {
            return scenario >= 1u && scenario <= stage.stars;
        }
    }
    return false;
}

// The rows; the probe's stage separately. Rows naming no stage the galaxy select knows are left out.
bool parse_stages(const std::string& text, std::vector<Stage>& stages, Stage& probe) {
    std::istringstream in(text);
    std::string line;
    bool have_probe = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::istringstream row(line);
        Stage s;
        std::string kind;
        if (!(row >> s.id >> s.scenario >> kind >> s.seconds) || !known_stage(s.id, s.scenario) || s.seconds < 1u ||
            s.seconds > 120u) {
            continue;
        }
        if (kind == "probe") {
            probe = s;
            have_probe = true;
        } else if (kind == "stage" || kind == "grand6") {
            s.grand = kind == "grand6";
            stages.push_back(s);
        }
    }
    return !stages.empty() && have_probe;
}

// ---- thread clocks -------------------------------------------------------------------------------------------------

struct ThreadClock {
#if defined(_WIN32)
    HANDLE handle = nullptr;
#elif defined(__APPLE__)
    mach_port_t port = 0;
#else
    clockid_t id{};
#endif
};

ThreadClock this_thread_clock() {
    ThreadClock clock;
#if defined(_WIN32)
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &clock.handle,
                    THREAD_QUERY_LIMITED_INFORMATION, FALSE, 0);
#elif defined(__APPLE__)
    clock.port = pthread_mach_thread_np(pthread_self());
#else
    if (pthread_getcpuclockid(pthread_self(), &clock.id) != 0) clock.id = CLOCK_THREAD_CPUTIME_ID;
#endif
    return clock;
}

#if defined(_WIN32)
std::int64_t filetime_ns(const FILETIME& t) {
    return static_cast<std::int64_t>((static_cast<std::uint64_t>(t.dwHighDateTime) << 32u) | t.dwLowDateTime) * 100;
}
#endif

// CPU nanoseconds the thread has run; -1 when unknown. On Windows these advance in scheduler ticks (15.6 ms), too
// coarse for one frame: fine_cpu is read per frame and this only scales it.
std::int64_t cpu_ns(const ThreadClock& clock) {
#if defined(_WIN32)
    FILETIME created{}, exited{}, kernel{}, user{};
    return clock.handle != nullptr && GetThreadTimes(clock.handle, &created, &exited, &kernel, &user)
               ? filetime_ns(kernel) + filetime_ns(user)
               : -1;
#elif defined(__APPLE__)
    thread_basic_info_data_t info{};
    mach_msg_type_number_t count = THREAD_BASIC_INFO_COUNT;
    if (thread_info(clock.port, THREAD_BASIC_INFO, reinterpret_cast<thread_info_t>(&info), &count) != KERN_SUCCESS) {
        return -1;
    }
    return (std::int64_t{info.user_time.seconds} + info.system_time.seconds) * 1000000000 +
           (std::int64_t{info.user_time.microseconds} + info.system_time.microseconds) * 1000;
#else
    timespec now{};
    return clock_gettime(clock.id, &now) == 0 ? std::int64_t{now.tv_sec} * 1000000000 + now.tv_nsec : -1;
#endif
}

// The thread's CPU time in fine units, -1 when unknown: cycles on Windows (QueryThreadCycleTime; fine_scale turns
// them into time), nanoseconds elsewhere.
std::int64_t fine_cpu(const ThreadClock& clock) {
#if defined(_WIN32)
    ULONG64 cycles = 0u;
    return clock.handle != nullptr && QueryThreadCycleTime(clock.handle, &cycles) ? static_cast<std::int64_t>(cycles)
                                                                                    : -1;
#else
    return cpu_ns(clock);
#endif
}

// The clock's handle closed (this_thread_clock's duplicate on Windows; nothing to free elsewhere).
void close_clock(ThreadClock& clock) {
#if defined(_WIN32)
    if (clock.handle != nullptr) CloseHandle(clock.handle);
#endif
    clock = ThreadClock{};
}

std::uint64_t process_peak_bytes() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    return GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) ? counters.PeakWorkingSetSize : 0u;
#else
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) != 0) return 0u;
#if defined(__APPLE__)
    return static_cast<std::uint64_t>(usage.ru_maxrss);  // bytes on Darwin
#else
    return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024u;  // kilobytes on Linux
#endif
#endif
}

std::int64_t steady_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ---- the device ----------------------------------------------------------------------------------------------------

// Texts go into the result as printable ASCII with no quote or backslash, at most `max` bytes (the schema's limit:
// diag_report.cpp), trimmed.
constexpr std::size_t kNameBytes = 96u;   // a device's, a stage's or a pass's name
constexpr std::size_t kNoteBytes = 256u;  // a line of the result's limits
std::string clean(std::string_view text, std::size_t max = kNameBytes) {
    std::string out;
    for (const char c : text) {
        if (out.size() >= max) break;
        if (c >= 0x20 && c < 0x7f && c != '"' && c != '\\') out.push_back(c);
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    const std::size_t first = out.find_first_not_of(' ');
    return first == std::string::npos ? std::string() : out.substr(first);
}

std::string cpu_brand() {
#if (defined(_WIN32) && (defined(_M_X64) || defined(_M_IX86))) || defined(__x86_64__) || defined(__i386__)
    unsigned words[12] = {};
    for (unsigned i = 0; i < 3u; ++i) {
#if defined(_WIN32)
        int regs[4];
        __cpuid(regs, static_cast<int>(0x80000002u + i));
        std::memcpy(words + i * 4u, regs, sizeof(regs));
#else
        __get_cpuid(0x80000002u + i, &words[i * 4u], &words[i * 4u + 1u], &words[i * 4u + 2u], &words[i * 4u + 3u]);
#endif
    }
    return clean(std::string_view(reinterpret_cast<const char*>(words), sizeof(words)));
#elif defined(__APPLE__)
    char brand[128] = {};
    std::size_t size = sizeof(brand) - 1u;
    return sysctlbyname("machdep.cpu.brand_string", brand, &size, nullptr, 0) == 0 ? clean(brand) : std::string();
#elif defined(__ANDROID__)
    char soc[PROP_VALUE_MAX] = {};
    if (__system_property_get("ro.soc.model", soc) > 0) return clean(soc);
    return {};
#elif defined(__linux__)
    std::ifstream in("/proc/cpuinfo");
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("model name", 0) == 0 || line.rfind("Model", 0) == 0) {
            const std::size_t colon = line.find(':');
            if (colon != std::string::npos) return clean(std::string_view(line).substr(colon + 1u));
        }
    }
    return {};
#else
    return {};
#endif
}

std::uint64_t ram_bytes() {
#if defined(_WIN32)
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    return GlobalMemoryStatusEx(&status) ? status.ullTotalPhys : 0u;
#elif defined(__APPLE__)
    std::uint64_t bytes = 0u;
    std::size_t size = sizeof(bytes);
    return sysctlbyname("hw.memsize", &bytes, &size, nullptr, 0) == 0 ? bytes : 0u;
#else
    const long pages = sysconf(_SC_PHYS_PAGES), page = sysconf(_SC_PAGESIZE);
    return pages > 0 && page > 0 ? static_cast<std::uint64_t>(pages) * static_cast<std::uint64_t>(page) : 0u;
#endif
}

std::string os_version() {
#if defined(_WIN32)
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    const auto get = reinterpret_cast<RtlGetVersionFn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion")));
    OSVERSIONINFOW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    if (get == nullptr || get(&info) != 0) return "Windows";
    return "Windows " + std::to_string(info.dwMajorVersion) + "." + std::to_string(info.dwMinorVersion) + "." +
           std::to_string(info.dwBuildNumber);
#elif defined(__ANDROID__)
    char release[PROP_VALUE_MAX] = {};
    __system_property_get("ro.build.version.release", release);
    return clean(std::string("Android ") + release);
#elif defined(__APPLE__)
    char release[64] = {};
    std::size_t size = sizeof(release) - 1u;
    return sysctlbyname("kern.osproductversion", release, &size, nullptr, 0) == 0 ? clean(std::string("macOS ") + release)
                                                                                  : std::string("macOS");
#else
    utsname name{};
    return uname(&name) == 0 ? clean(std::string(name.sysname) + " " + name.release) : std::string("Linux");
#endif
}

constexpr const char* backend_name() {
#if defined(NEBULA_GPU_VULKAN)
    return "vulkan";
#elif defined(NEBULA_GPU_METAL)
    return "metal";
#elif defined(NEBULA_GPU_D3D11)
    return "d3d11";
#else
    return "d3d12";
#endif
}

// ---- what the render thread samples --------------------------------------------------------------------------------

// One shown picture: when, the two threads' CPU clocks (fine units, -1 unknown), and the work counters at that moment.
struct Picture {
    std::uint64_t ns = 0u;
    std::int64_t render_cpu = -1, game_cpu = -1;
    std::uint64_t read_bytes = 0u, textures = 0u, texture_bytes = 0u, draw_items = 0u;
    std::uint32_t pipelines = 0u;
    std::array<std::uint64_t, kRenderParts> parts{};  // g_render_part_ns
};

// One frame's GPU time as read back: the number of pictures shown before it arrived, the total and each pass's
// milliseconds by slot (pass_names).
struct GpuFrame {
    std::uint32_t picture = 0u;
    double total_ms = 0.0;
    std::array<float, kPassSlots> pass_ms{};
};

// The passes' names by slot, as the renderer gives them (static text): the same names keep their slot for the run.
std::array<const char*, kPassSlots> g_pass_names{};
std::size_t g_pass_count = 0u;

// The slot of a pass name; kPassSlots when the slots are full. With g_picture_mutex held.
std::size_t pass_slot(const char* name) {
    for (std::size_t i = 0; i < g_pass_count; ++i) {
        if (g_pass_names[i] == name || std::strcmp(g_pass_names[i], name) == 0) return i;
    }
    if (g_pass_count == kPassSlots) return kPassSlots;
    g_pass_names[g_pass_count] = name;
    return g_pass_count++;
}

// ---- the run -------------------------------------------------------------------------------------------------------

enum class Kind { Scene, Standard, Probe, Thermal };

// A probe configuration: the optional passes off at a height, or one pass on alone.
enum class ProbeCase : unsigned { Small, Large, Ao, Msaa, Smaa, Bloom, Enhanced, Count };
constexpr const char* kProbeNames[] = {"480 base", "720 base", "ambient occlusion", "msaa 4", "smaa high", "bloom",
                                       "enhanced lighting"};
constexpr Feature kProbeFeature[] = {Feature::Count, Feature::Count, Feature::Ao,      Feature::Msaa,
                                     Feature::Smaa,  Feature::Bloom, Feature::Enhanced};

struct Item {
    Kind kind = Kind::Scene;
    Stage stage;
    ProbeCase probe = ProbeCase::Small;
};

// One item's measured window, as sampled; analysed when the result is written.
struct Window {
    // The stage's load: from the travel's ask to its arrival (none for a step that stayed on the stage).
    bool loaded = false;
    double load_ms = 0.0;
    std::uint64_t load_read_bytes = 0u, load_textures = 0u;
    std::uint32_t load_pipelines = 0u;
    std::vector<Picture> pictures;
    std::vector<GpuFrame> gpu;
    std::int64_t audio_start = -1, audio_end = -1;  // fine units
    std::int64_t start_ns = 0;                      // steady_ns when the window began
    std::uint32_t game_frames = 0u;                 // the game frames it measured
    std::uint64_t gpu_memory_peak = 0u, process_peak = 0u;
    double pixels = 0.0;                    // the scene's pixels at the item's settings and the output below
    unsigned output_w = 0u, output_h = 0u;  // the output (back buffer) when the window began
    bool output_moved = false;              // the output's size changed inside the window
    bool held = true;                       // the settings asked for are the ones that ran (a pass may be clamped)
};

// Reset: a run in the running game waits for the game's reset to its title (reset_done).
// Leave: the cover fades to black over the window just measured; the next step's settings change once it is black.
enum class Phase { Reset, Title, Leave, Settle, Pipelines, Arrive, Warm, Drain, Measure, Done, Failed };

// Each phase's share of a step's progress (live_state), in ten-thousandths of the step, before the measured frames
// take the rest: the loads differ too much from stage to stage to count them by time.
constexpr std::uint32_t kStepBeforeMeasure = 3000u;

struct ThreadSlot {
    std::atomic<bool> known{false};
    ThreadClock clock;
};

// The counters at a travel's ask: the load's work is what they gained by its arrival.
struct Counters {
    std::uint64_t read_bytes = 0u, textures = 0u;
    std::uint32_t pipelines = 0u;
};

Counters counters_now() {
    return {g_read_bytes.load(std::memory_order_relaxed), g_texture_uploads.load(std::memory_order_relaxed),
            render::pipeline_progress().done};
}

struct Run {
    Options options;
    std::filesystem::path file;  // where the result goes (start())
    std::vector<Stage> stages;
    Stage probe_stage;
    std::vector<Item> items;
    std::vector<Window> windows;
    std::size_t index = 0u;
    Phase phase = Phase::Title;
    std::uint32_t phase_frames = 0u;
    std::uint32_t asks = 0u;
    std::string stage_at_ask;
    bool saw_start = false;  // a scene started since the ask (scene_started): a travel to the stage it is on arrived
    bool arrived = false;    // the asked stage's scene started: shown, its opening playing, not yet measured
    // The cover (LiveState::cover): a fade from cover_from to cover_to that began at cover_ns.
    std::uint8_t cover_from = 0u, cover_to = 0u;
    std::int64_t cover_ns = 0;
    std::int64_t asked_ns = 0;
    Counters at_ask;
    bool here_known = false;  // the game is on `here`, the stage the last window measured
    Stage here;
    // The warm-up: the frame its work counters last moved, and their values then.
    std::uint32_t quiet_since = 0u;
    Counters warm_seen;
    bool restarted = false;           // the scene started again inside a window (scene_started)
    std::string failure;

    bool configured = false;
    bool settings_held = false;     // the run's settings in use (hold_settings), the player's kept in `chosen`
    render::RenderConfig chosen;    // the player's, put back at the end
    render::RenderConfig yours;     // the player's settings as measured (no frame pacing, no frame generation)
    render::RenderConfig standard;  // the standard settings (apply_benchmark_standard)
    RuntimeSettings runtime;        // the player's runtime settings, put back at the end
    bool gpu_timing_was_on = false; // GPU timing before the run, put back at the end

    // The fine clocks' scale (Windows cycles to nanoseconds): both clocks of each thread at the run's start.
    std::int64_t game_fine_start = -1, game_coarse_start = -1;
    bool finished = false;
};

Run g_run;
// Held by the game thread's step and by the exit: the result is written once, never while the run changes it.
std::mutex g_run_mutex;

ThreadSlot g_game;
ThreadSlot g_render;
ThreadSlot g_audio;
// The render thread's clocks at its first picture, for the fine clocks' scale.
std::int64_t g_render_fine_start = -1, g_render_coarse_start = -1;

// The measured window's samples (render thread), moved into its Window when it ends.
std::mutex g_picture_mutex;
std::vector<Picture> g_pictures;
std::vector<GpuFrame> g_gpu_frames;
std::atomic<bool> g_measuring{false};
// The GPU memory high-water of the window and the last budget read, sampled on the render thread every 30 pictures.
std::atomic<std::uint64_t> g_gpu_peak{0u};
std::atomic<std::uint64_t> g_gpu_budget{0u};
// The video memory read twice a second on the clock while measuring (render thread; at once in a new run).
galaxy::ClockInterval g_memory_read{500, true};
// When the travel asked last fails the run (steady_ns; 0: no travel waits), checked by the render thread at each
// picture, which the game shows while it loads; past it the game is asked to quit and at_exit writes the run as failed.
std::atomic<std::int64_t> g_arrive_deadline_ns{0};
std::atomic<bool> g_stalled{false};

// The last pictures' times (render thread, on_picture) for the run panel's frame rate: a ring, read under its lock.
constexpr std::size_t kRecentPictures = 512u;
constexpr std::int64_t kRateWindowNs = 500'000'000;
std::mutex g_recent_mutex;
std::array<std::uint64_t, kRecentPictures> g_recent{};
std::size_t g_recent_count = 0u;  // pictures noted, all time (the ring holds the last kRecentPictures)
std::uint32_t g_progress_shown = 0u;  // live_state's progress so far (it never goes back)

// The game's saves while it runs.
MemorySaves g_saves;

// A run in the running game (Options::here): the game's resets it asks for and has not had (take_reset), and whether
// the next reset_done is its end's.
std::atomic<bool> g_here{false};
std::atomic<bool> g_play{false};  // the scene shows play (note_play); false from each scene's start
std::atomic<std::uint32_t> g_resets_asked{0u};
std::atomic<bool> g_ending{false};

std::int64_t slot_fine(ThreadSlot& slot) {
    return slot.known.load(std::memory_order_acquire) ? fine_cpu(slot.clock) : -1;
}

void on_picture(std::uint64_t ns) {
    if (!g_render.known.load(std::memory_order_acquire)) {
        g_render.clock = this_thread_clock();
        g_render_fine_start = fine_cpu(g_render.clock);
        g_render_coarse_start = cpu_ns(g_render.clock);
        g_render.known.store(true, std::memory_order_release);
    }
    {
        const std::lock_guard<std::mutex> lock(g_recent_mutex);
        g_recent[g_recent_count++ % kRecentPictures] = ns;
    }
    // --benchmark 1: the player's stop ends the game here, whatever the game is doing (a stage loading counts no scene
    // frames), as does a travel nothing answered. A run in the running game ends at its next scene frame (scene_frame).
    const bool here = g_here.load(std::memory_order_relaxed);
    if (stopping() && !here) render::host_window::quit_request.store(true, std::memory_order_release);
    const std::int64_t deadline = g_arrive_deadline_ns.load(std::memory_order_relaxed);
    if (deadline != 0 && steady_ns() > deadline && !g_stalled.exchange(true, std::memory_order_relaxed) && !here) {
        render::host_window::quit_request.store(true, std::memory_order_release);
    }
    if (!g_measuring.load(std::memory_order_relaxed)) return;
    Picture picture;
    picture.ns = ns;
    picture.render_cpu = fine_cpu(g_render.clock);
    picture.game_cpu = slot_fine(g_game);
    picture.read_bytes = g_read_bytes.load(std::memory_order_relaxed);
    picture.textures = g_texture_uploads.load(std::memory_order_relaxed);
    picture.texture_bytes = g_texture_bytes.load(std::memory_order_relaxed);
    picture.draw_items = g_draw_items.load(std::memory_order_relaxed);
    picture.pipelines = render::pipeline_progress().done;
    for (std::size_t part = 0; part < kRenderParts; ++part) {
        picture.parts[part] = g_render_part_ns[part].load(std::memory_order_relaxed);
    }
    {
        const std::lock_guard<std::mutex> lock(g_picture_mutex);
        g_pictures.push_back(picture);
    }
    std::uint64_t usage = 0u, budget = 0u;
    if (g_memory_read.due() && render::video_memory(usage, budget)) {
        if (usage > g_gpu_peak.load(std::memory_order_relaxed)) g_gpu_peak.store(usage, std::memory_order_relaxed);
        g_gpu_budget.store(budget, std::memory_order_relaxed);
    }
}

void on_gpu_frame(const char* const* names, const double* ms, std::size_t count) {
    if (!g_measuring.load(std::memory_order_relaxed)) return;
    const std::lock_guard<std::mutex> lock(g_picture_mutex);
    GpuFrame frame;
    frame.picture = static_cast<std::uint32_t>(g_pictures.size());
    for (std::size_t i = 0; i < count; ++i) {
        if (!(ms[i] >= 0.0) || !std::isfinite(ms[i])) continue;
        frame.total_ms += ms[i];
        const std::size_t slot = pass_slot(names[i]);
        if (slot < kPassSlots) frame.pass_ms[slot] += static_cast<float>(ms[i]);
    }
    g_gpu_frames.push_back(frame);
}

void on_audio() {
    if (!g_audio.known.load(std::memory_order_acquire)) {
        g_audio.clock = this_thread_clock();
        g_audio.known.store(true, std::memory_order_release);
        g_audio_hook.store(nullptr, std::memory_order_release);
    }
}

// The output's size (the back buffer); 854x480 before the first picture.
unsigned output_width() {
    return render::host_window::backbuffer_width != 0u && render::host_window::backbuffer_height != 0u
               ? render::host_window::backbuffer_width
               : 854u;
}

unsigned output_lines() {
    return render::host_window::backbuffer_width != 0u && render::host_window::backbuffer_height != 0u
               ? render::host_window::backbuffer_height
               : 480u;
}

// The scene's pixels at a configuration on a `width` x `lines` output: the internal height (0: the panel's), or an
// upscaler's ratio of the output.
double scene_pixels(const render::RenderConfig& cfg, unsigned width, unsigned lines) {
    if (cfg.upscaler == 0u) {
        return render::scene_pixels(render::render_internal_height(cfg), width, lines);
    }
    // config.ini's temporal_quality ratios (render_config.h); FSR 1 at spatial_quality 0 renders at the output's size.
    constexpr double kRatios[] = {1.0, 1.5, 1.7, 2.0, 3.0, 1.3};
    const unsigned quality = cfg.upscaler == 1u ? cfg.spatial_quality : cfg.temporal_quality;
    const double ratio = cfg.upscaler == 1u && quality == 0u ? 1.0 : kRatios[std::min(quality, 5u)];
    const unsigned scene_lines = static_cast<unsigned>(std::lround(static_cast<double>(lines) / ratio));
    return render::scene_pixels(std::max(scene_lines, 1u), width, lines);
}

render::RenderConfig probe_config(ProbeCase which) {
    render::RenderConfig cfg = g_run.yours;
    cfg.upscaler = 0u;
    cfg.frame_generation = 0u;
    cfg.sharpness = 0u;
    cfg.internal_height = which == ProbeCase::Large ? 720u : 480u;
    cfg.msaa_samples = which == ProbeCase::Msaa ? 4u : 1u;
    cfg.ambient_occlusion = which == ProbeCase::Ao;
    cfg.ambient_occlusion_ssao = false;
    cfg.enhanced_lighting = which == ProbeCase::Enhanced;
    cfg.per_pixel_lighting = false;
    cfg.aa_mode = which == ProbeCase::Smaa ? 1u : 0u;
    cfg.smaa_quality = 2u;
    cfg.bloom = which == ProbeCase::Bloom ? 100u : 0u;
    return cfg;
}

render::RenderConfig item_config(const Item& item) {
    switch (item.kind) {
    case Kind::Probe:
        return probe_config(item.probe);
    case Kind::Standard:
        return g_run.standard;
    case Kind::Scene:
    case Kind::Thermal:
        break;
    }
    return g_run.yours;
}

// Whether the renderer runs what was asked: a pass the device cannot run is clamped off when set.
bool config_holds(const render::RenderConfig& asked) {
    const render::RenderConfig running = render::get_render_config();
    return running.msaa_samples == asked.msaa_samples && running.ambient_occlusion == asked.ambient_occlusion &&
           running.enhanced_lighting == asked.enhanced_lighting && running.aa_mode == asked.aa_mode &&
           running.bloom == asked.bloom && running.internal_height == asked.internal_height &&
           running.upscaler == asked.upscaler && running.aspect_mode == asked.aspect_mode;
}

void begin_window(Window& window, std::size_t expected_pictures) {
    {
        // Room for the window's pictures before it begins: a list that grew inside it would copy itself in a frame.
        const std::lock_guard<std::mutex> lock(g_picture_mutex);
        g_pictures.clear();
        g_pictures.reserve(expected_pictures);
        g_gpu_frames.clear();
        g_gpu_frames.reserve(expected_pictures);
    }
    window.start_ns = steady_ns();
    g_gpu_peak.store(0u, std::memory_order_relaxed);
    window.audio_start = slot_fine(g_audio);
    g_measuring.store(true, std::memory_order_relaxed);
}

void end_window(Window& window) {
    g_measuring.store(false, std::memory_order_relaxed);
    window.audio_end = slot_fine(g_audio);
    window.output_moved = output_width() != window.output_w || output_lines() != window.output_h;
    window.process_peak = process_peak_bytes();
    window.gpu_memory_peak = g_gpu_peak.load(std::memory_order_relaxed);
    const std::lock_guard<std::mutex> lock(g_picture_mutex);
    window.pictures.swap(g_pictures);
    window.gpu.swap(g_gpu_frames);
}

// ---- the analysis --------------------------------------------------------------------------------------------------

// The fine clocks' nanoseconds per unit over the run so far: 1 where they are nanoseconds; on Windows the threads'
// scheduler time over their cycles (both clocks of the game and render threads since the run began), 0 when that is
// too short to scale by (under 100 ms of either).
double fine_scale() {
#if defined(_WIN32)
    std::int64_t coarse = 0, fine = 0;
    if (g_game.known.load(std::memory_order_acquire) && g_run.game_fine_start >= 0 && g_run.game_coarse_start >= 0) {
        coarse += cpu_ns(g_game.clock) - g_run.game_coarse_start;
        fine += fine_cpu(g_game.clock) - g_run.game_fine_start;
    }
    if (g_render.known.load(std::memory_order_acquire) && g_render_fine_start >= 0 && g_render_coarse_start >= 0) {
        coarse += cpu_ns(g_render.clock) - g_render_coarse_start;
        fine += fine_cpu(g_render.clock) - g_render_fine_start;
    }
    return coarse >= 100'000'000 && fine > 0 ? static_cast<double>(coarse) / static_cast<double>(fine) : 0.0;
#else
    return 1.0;
#endif
}

struct PassCost {
    const char* name = nullptr;
    Spread ms;
    double share = 0.0;  // of the GPU's average frame
};

struct SlowFrame {
    double at_s = 0.0, ms = 0.0, gpu_ms = -1.0, render_ms = -1.0, game_ms = -1.0;
    Cause cause = Cause::Waiting;
    std::array<double, kRenderParts> parts{};  // the render thread's time by part, in milliseconds
};

// The render thread's milliseconds by part in the frame that ends at picture `i`.
std::array<double, kRenderParts> parts_ms(const Picture& before, const Picture& at) {
    std::array<double, kRenderParts> ms{};
    for (std::size_t part = 0; part < kRenderParts; ++part) {
        ms[part] = at.parts[part] >= before.parts[part] ? static_cast<double>(at.parts[part] - before.parts[part]) / 1e6 : 0.0;
    }
    return ms;
}

struct Analysis {
    std::vector<double> frame_ms;  // each frame's time, in order (the frame list)
    FrameStats frames;
    Spread gpu, render, game;      // count 0: not measured
    std::array<Spread, kRenderParts> parts;  // the render thread's time by part, per frame
    double audio_ms = -1.0;        // the mixing thread's CPU per game frame
    std::vector<PassCost> passes;  // heaviest first
    double draw_items = 0.0;       // per frame
    std::uint64_t texture_uploads = 0u, texture_bytes = 0u, read_bytes = 0u;
    std::uint32_t pipelines = 0u;
    std::vector<SlowFrame> slowest;  // the slowest frames, in time order
    const char* limiter = "waiting";
    bool display_limited = false;
    bool generated = false;  // interpolated frames were shown between the game's
};

// The busiest of the GPU and the two threads, as cause_of judges a frame: the limiter when it took at least half the
// average frame; "waiting" when none did. A frame rate that stood at a ceiling (ceiling_of) is that ceiling's.
const char* limiter_of(double frame_ms, double gpu_ms, double render_ms, double game_ms, const char* ceiling) {
    if (ceiling != nullptr) return ceiling;
    const char* name = "waiting";
    double busiest = 0.5 * frame_ms;
    if (gpu_ms >= busiest) {
        name = "gpu";
        busiest = gpu_ms;
    }
    if (render_ms >= busiest) {
        name = "render thread";
        busiest = render_ms;
    }
    if (game_ms >= busiest) name = "game thread";
    return name;
}

bool at_refresh(double fps, double refresh_hz) {
    return refresh_hz > 0.0 && fps > 0.0 && std::abs(fps - refresh_hz) <= kDisplayTolerance * refresh_hz;
}

// The rate the pictures stood at, average and median alike, when it is one they cannot pass: the display's refresh
// rate (presents in step with it), or the game's own 60 when no interpolated frames came between its frames (a
// renderer that makes none). nullptr: none of them.
const char* ceiling_of(const FrameStats& frames, double refresh_hz, bool generated) {
    if (frames.p50_ms <= 0.0) return nullptr;
    const auto at = [&](double hz) { return at_refresh(frames.average_fps, hz) && at_refresh(1000.0 / frames.p50_ms, hz); };
    if (at(refresh_hz)) return "display";
    if (!generated && at(static_cast<double>(kGameStepsPerSecond))) return "game frames";
    return nullptr;
}

Analysis analyse(const Window& w, double scale, double refresh_hz) {
    Analysis a;
    const std::vector<Picture>& p = w.pictures;
    const auto cpu_ms = [scale](std::int64_t from, std::int64_t to) {
        return from >= 0 && to >= from && scale > 0.0 ? static_cast<double>(to - from) * scale / 1e6 : -1.0;
    };
    std::vector<double> render_ms, game_ms;
    std::array<std::vector<double>, kRenderParts> part_ms;
    // Frame i ends at picture i + 1.
    for (std::size_t i = 1; i < p.size(); ++i) {
        if (p[i].ns <= p[i - 1u].ns) continue;
        a.frame_ms.push_back(static_cast<double>(p[i].ns - p[i - 1u].ns) / 1e6);
        if (const double ms = cpu_ms(p[i - 1u].render_cpu, p[i].render_cpu); ms >= 0.0) render_ms.push_back(ms);
        if (const double ms = cpu_ms(p[i - 1u].game_cpu, p[i].game_cpu); ms >= 0.0) game_ms.push_back(ms);
        const std::array<double, kRenderParts> parts = parts_ms(p[i - 1u], p[i]);
        for (std::size_t part = 0; part < kRenderParts; ++part) part_ms[part].push_back(parts[part]);
    }
    a.frames = frame_stats(a.frame_ms);
    a.render = spread(render_ms);
    a.game = spread(game_ms);
    for (std::size_t part = 0; part < kRenderParts; ++part) a.parts[part] = spread(std::move(part_ms[part]));
    if (w.game_frames != 0u) {
        const double audio = cpu_ms(w.audio_start, w.audio_end);
        a.audio_ms = audio >= 0.0 ? audio / static_cast<double>(w.game_frames) : -1.0;
    }

    std::vector<double> gpu_ms;
    gpu_ms.reserve(w.gpu.size());
    for (const GpuFrame& f : w.gpu) gpu_ms.push_back(f.total_ms);
    a.gpu = spread(gpu_ms);
    for (std::size_t slot = 0; slot < g_pass_count; ++slot) {
        std::vector<double> values;
        values.reserve(w.gpu.size());
        for (const GpuFrame& f : w.gpu) values.push_back(f.pass_ms[slot]);
        PassCost cost{g_pass_names[slot], spread(values), 0.0};
        if (cost.ms.average <= 0.0) continue;
        cost.share = a.gpu.average > 0.0 ? cost.ms.average / a.gpu.average : 0.0;
        a.passes.push_back(cost);
    }
    std::sort(a.passes.begin(), a.passes.end(),
              [](const PassCost& x, const PassCost& y) { return x.ms.average > y.ms.average; });

    if (p.size() >= 2u) {
        const Picture& first = p.front();
        const Picture& last = p.back();
        a.draw_items = static_cast<double>(last.draw_items - first.draw_items) / static_cast<double>(p.size() - 1u);
        a.texture_uploads = last.textures - first.textures;
        a.texture_bytes = last.texture_bytes - first.texture_bytes;
        a.read_bytes = last.read_bytes - first.read_bytes;
        a.pipelines = last.pipelines - first.pipelines;
    }

    // The slowest frames, each with what came with it (cause_of).
    std::vector<std::size_t> order;
    for (std::size_t i = 1; i < p.size(); ++i) {
        if (p[i].ns > p[i - 1u].ns) order.push_back(i);
    }
    const auto frame_of = [&](std::size_t i) { return static_cast<double>(p[i].ns - p[i - 1u].ns) / 1e6; };
    std::sort(order.begin(), order.end(), [&](std::size_t x, std::size_t y) { return frame_of(x) > frame_of(y); });
    order.resize(std::min(order.size(), kSlowestFrames));
    std::sort(order.begin(), order.end());
    for (const std::size_t i : order) {
        FrameFacts facts;
        facts.frame_ms = frame_of(i);
        facts.render_ms = cpu_ms(p[i - 1u].render_cpu, p[i].render_cpu);
        facts.game_ms = cpu_ms(p[i - 1u].game_cpu, p[i].game_cpu);
        for (const GpuFrame& f : w.gpu) {
            if (f.picture >= i && f.picture <= i + kGpuReadbackFrames) facts.gpu_ms = std::max(facts.gpu_ms, f.total_ms);
        }
        facts.pipelines = p[i].pipelines - p[i - 1u].pipelines;
        facts.read_bytes = p[i].read_bytes - p[i - 1u].read_bytes;
        facts.textures = static_cast<std::uint32_t>(p[i].textures - p[i - 1u].textures);
        a.slowest.push_back({static_cast<double>(p[i].ns - p.front().ns) / 1e9, facts.frame_ms, facts.gpu_ms,
                             facts.render_ms, facts.game_ms, cause_of(facts), parts_ms(p[i - 1u], p[i])});
    }

    // More pictures than game frames (a tenth over: the window's edges) had interpolated frames between them.
    a.generated = w.game_frames != 0u && static_cast<double>(p.size()) > 1.1 * static_cast<double>(w.game_frames);
    const char* ceiling = ceiling_of(a.frames, refresh_hz, a.generated);
    a.display_limited = ceiling != nullptr;
    a.limiter = limiter_of(a.frames.average_ms, a.gpu.count != 0u ? a.gpu.average : -1.0,
                           a.render.count != 0u ? a.render.average : -1.0, a.game.count != 0u ? a.game.average : -1.0,
                           ceiling);
    return a;
}

// ---- the result ----------------------------------------------------------------------------------------------------

std::string utc_stamp() {
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char text[32];
    std::strftime(text, sizeof(text), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return text;
}

// A JSON document written in order: each value under a key in an object, or as the next item of an array. Texts are
// printable ASCII (clean), numbers have three decimals and a '.' whatever the process locale, and a number that is not
// finite is written as null.
class JsonWriter {
public:
    JsonWriter() { first_.push_back(true); }

    void begin_object(std::string_view key = {}) { open(key, '{'); }
    void end_object() { close('}'); }
    void begin_array(std::string_view key = {}) { open(key, '['); }
    void end_array() { close(']'); }

    void text(std::string_view key, std::string_view value, std::size_t max = kNameBytes) {
        prefix(key);
        out_ += '"';
        out_ += clean(value, max);
        out_ += '"';
    }
    void number(std::string_view key, double value) {
        prefix(key);
        if (!std::isfinite(value)) {
            out_ += "null";
            return;
        }
        char digits[40];
        std::snprintf(digits, sizeof(digits), "%.3f", value);
        for (char* c = digits; *c != '\0'; ++c) {
            if (*c == ',') *c = '.';  // a process locale with a decimal comma (set by a toolkit or a plugin)
        }
        out_ += digits;
    }
    // A number known only when it is not negative (a per-frame cost): null otherwise.
    void known(std::string_view key, double value) {
        if (value < 0.0) {
            null(key);
        } else {
            number(key, value);
        }
    }
    void whole(std::string_view key, std::uint64_t value) {
        prefix(key);
        out_ += std::to_string(value);
    }
    void boolean(std::string_view key, bool value) {
        prefix(key);
        out_ += value ? "true" : "false";
    }
    void null(std::string_view key) {
        prefix(key);
        out_ += "null";
    }
    [[nodiscard]] const std::string& text() const noexcept { return out_; }

private:
    void prefix(std::string_view key) {
        if (!first_.back()) out_ += ',';
        first_.back() = false;
        if (!key.empty()) {
            out_ += '"';
            out_ += key;
            out_ += "\":";
        }
    }
    void open(std::string_view key, char bracket) {
        prefix(key);
        out_ += bracket;
        first_.push_back(true);
    }
    void close(char bracket) {
        first_.pop_back();
        out_ += bracket;
    }

    std::string out_;
    std::vector<bool> first_;
};

void write_settings(JsonWriter& j, std::string_view key, const render::RenderConfig& c) {
    j.begin_object(key);
    j.whole("internal_height", c.internal_height);
    j.whole("aspect_mode", c.aspect_mode);
    j.whole("upscaler", c.upscaler);
    j.whole("msaa_samples", c.msaa_samples);
    j.whole("aa", c.aa_mode);
    j.whole("smaa_quality", c.smaa_quality);
    j.boolean("ambient_occlusion", c.ambient_occlusion);
    j.boolean("ambient_occlusion_ssao", c.ambient_occlusion_ssao);
    j.boolean("enhanced_lighting", c.enhanced_lighting);
    j.boolean("per_pixel_lighting", c.per_pixel_lighting);
    j.whole("bloom", c.bloom);
    j.whole("vsync_interval", c.vsync_interval);
    j.number("max_fps", c.max_fps);  // null: unlimited (render_config.h kUnlimitedFrameRate)
    j.whole("frame_generation", c.frame_generation);
    j.boolean("ray_tracing", c.ray_tracing_enabled);
    j.whole("display_mode", c.display_mode);
    j.end_object();
}

void write_spread(JsonWriter& j, std::string_view key, const Spread& s) {
    if (s.count == 0u) {
        j.null(key);
        return;
    }
    j.begin_object(key);
    j.number("average", s.average);
    j.number("p99", s.p99);
    j.number("worst", s.worst);
    j.end_object();
}

void write_frame_summary(JsonWriter& j, const FrameStats& f) {
    j.whole("frames", f.frames);
    j.number("seconds", f.seconds);
    j.number("average_fps", f.average_fps);
    j.number("low_10_fps", f.low_10_fps);
    j.number("low_1_fps", f.low_1_fps);
    j.number("low_01_fps", f.low_01_fps);
    j.begin_object("frame_ms");
    j.number("average", f.average_ms);
    j.number("p50", f.p50_ms);
    j.number("p95", f.p95_ms);
    j.number("p99", f.p99_ms);
    j.number("p999", f.p999_ms);
    j.number("worst", f.worst_ms);
    j.end_object();
    j.whole("stutters", f.stutters);
}

constexpr double kMiB = 1024.0 * 1024.0;

void write_scene(JsonWriter& j, const Item& item, const Window& w, const Analysis& a) {
    j.begin_object();
    j.text("stage", item.stage.id);
    j.whole("scenario", item.stage.scenario);
    j.whole("grand_stars", item.stage.grand ? 6u : 0u);
    j.boolean("settings_held", w.held);
    j.begin_array("output");
    j.whole({}, w.output_w);
    j.whole({}, w.output_h);
    j.end_array();
    j.boolean("output_moved", w.output_moved);
    j.number("pixels", w.pixels);
    if (w.loaded) {
        j.begin_object("load");
        j.number("ms", w.load_ms);
        j.number("read_mb", static_cast<double>(w.load_read_bytes) / kMiB);
        j.whole("pipelines", w.load_pipelines);
        j.whole("textures", w.load_textures);
        j.end_object();
    }
    write_frame_summary(j, a.frames);
    j.text("limiter", a.limiter);
    write_spread(j, "gpu_ms", a.gpu);
    j.begin_object("gpu_passes");
    for (const PassCost& pass : a.passes) {
        j.begin_object(clean(pass.name));
        j.number("average", pass.ms.average);
        j.number("p99", pass.ms.p99);
        j.number("share", pass.share);
        j.end_object();
    }
    j.end_object();
    j.begin_object("cpu_ms");
    write_spread(j, "game", a.game);
    write_spread(j, "render", a.render);
    j.known("audio", a.audio_ms);
    j.end_object();
    j.begin_object("work");
    j.number("draw_items", a.draw_items);
    j.whole("texture_uploads", a.texture_uploads);
    j.number("texture_mb", static_cast<double>(a.texture_bytes) / kMiB);
    j.whole("pipelines_built", a.pipelines);
    j.number("read_mb", static_cast<double>(a.read_bytes) / kMiB);
    j.end_object();
    // The parts the render thread spent time in (wall time per frame), the ones it never entered left out.
    j.begin_object("render_parts");
    for (std::size_t part = 0; part < kRenderParts; ++part) {
        if (a.parts[part].count == 0u || a.parts[part].worst <= 0.0) continue;
        j.begin_object(kRenderPartNames[part]);
        j.number("average", a.parts[part].average);
        j.number("p99", a.parts[part].p99);
        j.number("worst", a.parts[part].worst);
        j.end_object();
    }
    j.end_object();
    j.whole("gpu_memory_peak_mb", w.gpu_memory_peak >> 20u);
    j.whole("process_peak_mb", w.process_peak >> 20u);
    j.begin_array("slowest_frames");
    for (const SlowFrame& s : a.slowest) {
        j.begin_object();
        j.number("at_s", s.at_s);
        j.number("ms", s.ms);
        j.known("gpu_ms", s.gpu_ms);
        j.known("render_ms", s.render_ms);
        j.known("game_ms", s.game_ms);
        j.text("cause", cause_name(s.cause));
        // Its render thread's time by part, those under 10 microseconds left out.
        j.begin_object("render_parts");
        for (std::size_t part = 0; part < kRenderParts; ++part) {
            if (s.parts[part] >= 0.01) j.number(kRenderPartNames[part], s.parts[part]);
        }
        j.end_object();
        j.end_object();
    }
    j.end_array();
    j.end_object();
}

// One part's frames and costs over its stages: every frame of every stage together, and the costs weighted by frames.
struct PartTotals {
    FrameStats frames;
    const char* limiter = "waiting";
    bool display_limited = false;
};

PartTotals part_totals(const std::vector<const Analysis*>& scenes, double refresh_hz) {
    PartTotals t;
    std::vector<double> all;
    double gpu = 0.0, render = 0.0, game = 0.0;
    std::size_t gpu_n = 0u, render_n = 0u, game_n = 0u;
    bool generated = false;
    for (const Analysis* a : scenes) {
        generated = generated || a->generated;
        all.insert(all.end(), a->frame_ms.begin(), a->frame_ms.end());
        gpu += a->gpu.average * static_cast<double>(a->gpu.count);
        gpu_n += a->gpu.count;
        render += a->render.average * static_cast<double>(a->render.count);
        render_n += a->render.count;
        game += a->game.average * static_cast<double>(a->game.count);
        game_n += a->game.count;
    }
    t.frames = frame_stats(all);
    const char* ceiling = ceiling_of(t.frames, refresh_hz, generated);
    t.display_limited = ceiling != nullptr;
    const auto mean = [](double sum, std::size_t n) { return n != 0u ? sum / static_cast<double>(n) : -1.0; };
    t.limiter = limiter_of(t.frames.average_ms, mean(gpu, gpu_n), mean(render, render_n), mean(game, game_n), ceiling);
    return t;
}

// A stopped run is aborted (the player's choice), never failed, whatever it was doing.
const char* status_name(bool complete) {
    return complete ? "complete" : g_run.phase == Phase::Failed && !stopping() ? "failed" : "aborted";
}

// The items whose window ended. A run that ends mid-item (a quit while its scene loads, skips or is measured) leaves out
// that item's window.
std::size_t finished_items() {
    return std::min({g_run.windows.size(), g_run.items.size(), g_run.index});
}

// Every finished window measured at one output size, unchanged inside each window: that size in `width` x `lines`.
// False (no recommendation: its pixels would mix sizes) when a window's differs from the first's or moved inside it;
// true with 0 x 0 when none finished.
bool output_held(unsigned& width, unsigned& lines) {
    width = 0u;
    lines = 0u;
    for (std::size_t i = 0; i < finished_items(); ++i) {
        const Window& w = g_run.windows[i];
        if (i == 0u) {
            width = w.output_w;
            lines = w.output_h;
        }
        if (w.output_moved || w.output_w != width || w.output_h != lines) return false;
    }
    return true;
}

// The recommendation's inputs from the analyses, at the output size the windows were measured at (output_held).
Measured measured_inputs(const std::vector<Analysis>& analyses) {
    Run& run = g_run;
    Measured m;
    (void)output_held(m.output_width, m.output_lines);
    const render::RenderConfig& c = run.chosen;
    m.current.internal_height = c.internal_height;
    m.current.msaa_samples = c.msaa_samples;
    m.current.ambient_occlusion = c.ambient_occlusion;
    m.current.enhanced_lighting = c.enhanced_lighting;
    m.current.aa = c.aa_mode;
    m.current.smaa_quality = c.smaa_quality;
    m.current.bloom = c.bloom;
    m.upscaler = c.upscaler != 0u;
    m.current_pixels = scene_pixels(run.yours, m.output_width, m.output_lines);
    const Analysis* worst = nullptr;
    const Analysis* probe[static_cast<std::size_t>(ProbeCase::Count)] = {};
    const Window* probe_window[static_cast<std::size_t>(ProbeCase::Count)] = {};
    for (std::size_t i = 0; i < analyses.size(); ++i) {
        const Analysis& a = analyses[i];
        if (run.items[i].kind == Kind::Scene) {
            if (worst == nullptr || a.gpu.average > worst->gpu.average) worst = &a;
            if (a.game.count != 0u) m.worst_game_cpu_ms = std::max(m.worst_game_cpu_ms, a.game.average);
            if (a.render.count != 0u) m.worst_render_cpu_ms = std::max(m.worst_render_cpu_ms, a.render.average);
        } else if (run.items[i].kind == Kind::Probe) {
            probe[static_cast<std::size_t>(run.items[i].probe)] = &a;
            probe_window[static_cast<std::size_t>(run.items[i].probe)] = &run.windows[i];
        }
    }
    if (worst != nullptr) {
        m.worst_gpu_ms = worst->gpu.average;
        m.tail = worst->frames.p50_ms > 0.0 ? worst->frames.p95_ms / worst->frames.p50_ms : 1.0;
    }
    const std::size_t at480 = static_cast<std::size_t>(ProbeCase::Small);
    const std::size_t at720 = static_cast<std::size_t>(ProbeCase::Large);
    const auto usable = [&](std::size_t p) {
        return probe[p] != nullptr && probe_window[p]->held && probe[p]->gpu.count != 0u;
    };
    if (usable(at480) && usable(at720)) {
        const double pixels[] = {probe_window[at480]->pixels, probe_window[at720]->pixels};
        const double ms[] = {probe[at480]->gpu.average, probe[at720]->gpu.average};
        m.base = render::fit_gpu_time(pixels, ms, 2u);
        for (std::size_t p = static_cast<std::size_t>(ProbeCase::Ao); p < static_cast<std::size_t>(ProbeCase::Count); ++p) {
            if (usable(p) && probe_window[at480]->pixels > 0.0) {
                m.price_per_mpixel[static_cast<std::size_t>(kProbeFeature[p])] =
                    std::max(0.0, (probe[p]->gpu.average - probe[at480]->gpu.average) / (probe_window[at480]->pixels / 1e6));
            }
        }
    }
    return m;
}

double refresh_rate() {
    std::int64_t vblank = 0, period = 0;
    return render::host_window::vblank_grid_known(vblank, period) && period > 0
               ? static_cast<double>(qpc_frequency()) / static_cast<double>(period)
               : 0.0;
}

// The frame lists beside the result (benchmark_stats.h): each measured stage of both parts.
std::string frame_lists(const std::vector<Analysis>& analyses) {
    std::vector<FrameList> lists;
    for (std::size_t i = 0; i < analyses.size(); ++i) {
        const Kind kind = g_run.items[i].kind;
        if (kind != Kind::Scene && kind != Kind::Standard) continue;
        lists.push_back({kind == Kind::Scene ? 0u : 1u, g_run.items[i].stage.id, analyses[i].frame_ms});
    }
    return encode_frame_lists(lists);
}

// With g_run_mutex held.
void write_result(bool complete) {
    Run& run = g_run;
    if (run.finished) return;
    run.finished = true;
    g_measuring.store(false, std::memory_order_relaxed);

    const double refresh = refresh_rate();
    const double scale = fine_scale();
    std::vector<Analysis> analyses;
    analyses.reserve(finished_items());
    for (std::size_t i = 0; i < finished_items(); ++i) analyses.push_back(analyse(run.windows[i], scale, refresh));

    const render::RenderAdapter adapter = render::render_adapter();
    const std::uint64_t budget = g_gpu_budget.load(std::memory_order_relaxed);
    unsigned held_width = 0u, held_lines = 0u;
    const bool held = output_held(held_width, held_lines);

    JsonWriter j;
    j.begin_object();
    j.whole("schema", kResultSchema);
    j.begin_object("app");
    j.text("version", NEBULA_VERSION);
    j.null("commit");
    j.end_object();
    j.text("created_utc", utc_stamp());
    j.text("status", status_name(complete));
    j.text("failure", run.failure);
    j.text("backend", backend_name());

    j.begin_object("device");
    j.text("gpu", adapter.description);
    j.whole("vendor_id", adapter.vendor_id);
    j.whole("device_id", adapter.device_id);
    if (adapter.driver.empty()) {
        j.null("driver_version");
    } else {
        j.text("driver_version", adapter.driver);
    }
    j.boolean("shared_memory", render::rt_caps().unified_memory);
    j.whole("gpu_budget_mb", budget >> 20u);
    j.text("cpu", cpu_brand());
    j.whole("logical_cores", logical_cores());
    j.whole("ram_mb", ram_bytes() >> 20u);
    j.text("os", os_version());
    j.begin_array("output");
    j.whole({}, render::host_window::backbuffer_width);
    j.whole({}, render::host_window::backbuffer_height);
    j.end_array();
    if (refresh > 0.0) {
        j.number("refresh_hz", refresh);
    } else {
        j.null("refresh_hz");
    }
    j.end_object();
    j.boolean("mods_loaded", run.options.mods_loaded);
    write_settings(j, "settings", run.chosen);

    // What the numbers mean, for whoever reads the file.
    std::vector<const Analysis*> yours, standard;
    for (std::size_t i = 0; i < analyses.size(); ++i) {
        if (run.items[i].kind == Kind::Scene) yours.push_back(&analyses[i]);
        if (run.items[i].kind == Kind::Standard) standard.push_back(&analyses[i]);
    }
    const PartTotals yours_totals = part_totals(yours, refresh);
    const PartTotals standard_totals = part_totals(standard, refresh);
    const auto note = [&j](std::string_view line) { j.text({}, line, kNoteBytes); };
    j.begin_array("limits");
    note("frame times are the time between shown pictures with frame pacing off: the frame rate is what the device can "
         "render, not the frame limit");
    note("each stage's window holds its entry (intro camera, star name) without a split: the game does not report when "
         "the intro ends");
    note("a slow frame's gpu_ms is the longest GPU frame read back within 4 frames of it");
#if defined(_WIN32)
    note("thread CPU times are cycle counts scaled by the threads' scheduler time over the run");
#endif
    if (render::render_config_first_start()) {
        // Never set up: no config.ini when the process started. The game's defaults stand as the player's.
        note("no config.ini yet: settings are the game's defaults");
    }
    if (!held) {
        note("the output size changed during the run: its windows were measured at different sizes, so there is no "
             "recommendation");
    }
    if (yours_totals.display_limited || standard_totals.display_limited) {
        note("the frame rate stood at a rate it cannot pass (the display's refresh rate, the benchmark's cap, or the "
             "game's own 60 where the renderer makes no frames between them): the device has more to give than measured");
    }
    j.end_array();

    j.begin_array("runs");
    const auto write_part = [&](const char* part, Kind kind, const render::RenderConfig& with, const PartTotals& totals) {
        j.begin_object();
        j.text("part", part);
        write_settings(j, "measured_with", with);
        j.begin_object("summary");
        write_frame_summary(j, totals.frames);
        j.text("limiter", totals.limiter);
        j.boolean("display_limited", totals.display_limited);
        j.end_object();
        j.begin_array("scenes");
        for (std::size_t i = 0; i < analyses.size(); ++i) {
            if (run.items[i].kind == kind) write_scene(j, run.items[i], run.windows[i], analyses[i]);
        }
        j.end_array();
        j.end_object();
    };
    write_part("yours", Kind::Scene, run.yours, yours_totals);
    write_part("standard", Kind::Standard, run.standard, standard_totals);
    j.end_array();

    const Measured m = measured_inputs(analyses);
    j.begin_object("probe");
    j.text("stage", run.probe_stage.id);
    j.begin_array("runs");
    for (std::size_t i = 0; i < analyses.size(); ++i) {
        if (run.items[i].kind != Kind::Probe) continue;
        j.begin_object();
        j.text("case", kProbeNames[static_cast<std::size_t>(run.items[i].probe)]);
        j.boolean("settings_held", run.windows[i].held);
        j.number("pixels", run.windows[i].pixels);
        write_spread(j, "gpu_ms", analyses[i].gpu);
        j.number("average_fps", analyses[i].frames.average_fps);
        j.end_object();
    }
    j.end_array();
    j.begin_object("fit");
    j.boolean("valid", m.base.valid);
    j.number("fixed_ms", m.base.a_ms);
    j.number("ms_per_mpixel", m.base.b_ms_per_pixel * 1e6);
    j.end_object();
    j.begin_object("price_ms_per_mpixel");
    for (std::size_t p = static_cast<std::size_t>(ProbeCase::Ao); p < static_cast<std::size_t>(ProbeCase::Count); ++p) {
        j.known(kProbeNames[p], m.price_per_mpixel[static_cast<std::size_t>(kProbeFeature[p])]);
    }
    j.end_object();
    j.end_object();

    // The heat check: the first stage at the start of the run and again at its end, at the same settings.
    const Analysis* thermal_first = nullptr;
    const Analysis* thermal_last = nullptr;
    for (std::size_t i = 0; i < analyses.size(); ++i) {
        if (run.items[i].kind == Kind::Scene && thermal_first == nullptr) thermal_first = &analyses[i];
        if (run.items[i].kind == Kind::Thermal) thermal_last = &analyses[i];
    }
    if (thermal_first != nullptr && thermal_last != nullptr && thermal_first->gpu.count != 0u &&
        thermal_last->gpu.count != 0u) {
        const bool judged = thermal_first->gpu.average >= kHeatJudgedGpuMs;
        j.begin_object("thermal");
        j.text("stage", run.stages.front().id);
        j.boolean("judged", judged);
        j.number("gpu_ms_first", thermal_first->gpu.average);
        j.number("gpu_ms_last", thermal_last->gpu.average);
        j.number("slowdown", thermal_last->gpu.average / thermal_first->gpu.average);
        j.number("average_fps_first", thermal_first->frames.average_fps);
        j.number("average_fps_last", thermal_last->frames.average_fps);
        j.end_object();
    } else {
        j.null("thermal");
    }

    const Recommendation rec = complete && held ? recommend(m) : Recommendation{};
    if (!rec.valid) {
        j.null("recommendation");
    } else {
        const Choice& c = rec.choice;
        const Choice& now = m.current;
        j.begin_object("recommendation");
        j.number("budget_ms", render::fit_budget_ms());
        j.number("predicted_ms", rec.predicted_ms);
        j.boolean("fits", rec.fits);
        j.boolean("cpu_bound", rec.cpu_bound);
        if (rec.cpu_bound) {
            j.text("cpu_thread", rec.cpu_thread);
        } else {
            j.null("cpu_thread");
        }
        j.boolean("height_left_out", rec.height_left_out);
        j.begin_array("settings");
        const auto row = [&](const char* key, unsigned current, unsigned recommended) {
            j.begin_object();
            j.text("key", key);
            j.whole("current", current);
            j.whole("recommended", recommended);
            j.end_object();
        };
        if (!rec.height_left_out) row("internal_height", now.internal_height, c.internal_height);
        row("msaa_samples", now.msaa_samples, c.msaa_samples);
        row("ambient_occlusion", now.ambient_occlusion ? 1u : 0u, c.ambient_occlusion ? 1u : 0u);
        row("enhanced_lighting", now.enhanced_lighting ? 1u : 0u, c.enhanced_lighting ? 1u : 0u);
        row("aa", now.aa, c.aa);
        row("smaa_quality", now.smaa_quality, c.smaa_quality);
        row("bloom", now.bloom, c.bloom);
        j.end_array();
        j.end_object();
    }

    // The results screen's headline: whether the player's settings hold 60 FPS, the room left at the slow end
    // (the 99th percentile frame against a 60 FPS frame), what limits the frame, the heaviest stage.
    const FrameStats& f = yours_totals.frames;
    const bool thread_bound =
        std::strcmp(yours_totals.limiter, "game thread") == 0 || std::strcmp(yours_totals.limiter, "render thread") == 0;
    const char* verdict = f.frames == 0u                ? "none"
                          : f.low_1_fps >= kFitsLowFps ? "fits"
                          : thread_bound               ? "cpu_bound"
                                                       : "below_target";
    std::string worst_stage;
    double worst_fps = 0.0;
    for (std::size_t i = 0; i < analyses.size(); ++i) {
        if (run.items[i].kind == Kind::Scene && (worst_stage.empty() || analyses[i].frames.average_fps < worst_fps)) {
            worst_fps = analyses[i].frames.average_fps;
            worst_stage = run.items[i].stage.id;
        }
    }
    j.begin_object("summary");
    j.text("verdict", verdict);
    j.number("average_fps", f.average_fps);
    j.number("low_10_fps", f.low_10_fps);
    j.number("low_1_fps", f.low_1_fps);
    j.number("spare_percent", f.p99_ms > 0.0 ? std::clamp(100.0 * (1.0 - f.p99_ms * kTargetFps / 1000.0), -100.0, 100.0)
                                             : 0.0);
    j.text("limiter", yours_totals.limiter);
    j.text("worst_stage", worst_stage);
    j.number("standard_average_fps", standard_totals.frames.average_fps);
    j.number("standard_low_10_fps", standard_totals.frames.low_10_fps);
    j.number("standard_low_1_fps", standard_totals.frames.low_1_fps);
    j.end_object();
    j.end_object();

    const std::filesystem::path& file = run.file;
    bool written = !file.empty() && io::write_file_atomically(file, j.text() + "\n");
    if (written) {
        written = io::write_file_atomically(frames_file(file), frame_lists(analyses));
        // Results in their own folder keep the newest few; a file named on the command line is the caller's.
        std::error_code error;
        const std::filesystem::path folder = results_folder();
        if (!folder.empty() && std::filesystem::equivalent(file.parent_path(), folder, error)) {
            keep_newest(folder, kKeptResults);
        }
    }
    std::printf("[benchmark] %s: %s\n", status_name(complete), written ? "result written" : "the result could not be written");
    std::fflush(stdout);
}

// ---- the state machine (game thread) -------------------------------------------------------------------------------

// The player's settings kept and the run's own in use, from the run's first frame on (its reset and title too): the
// benchmark presents its way (apply_benchmark_presenting) whatever frame rate cap or VSync the player chose. A run in
// the game holds them as it starts; a run from the command line at its first frame, once the settings file is read.
void hold_settings() {
    Run& r = g_run;
    if (r.settings_held) return;
    r.settings_held = true;
    r.chosen = render::get_render_config_chosen();
    r.yours = r.chosen;
    r.yours.frame_generation = 0u;
    r.yours.ray_tracing_enabled = false;
    render::apply_benchmark_presenting(r.yours);
    r.standard = r.chosen;
    render::apply_benchmark_standard(r.standard);
    render::set_render_config(r.yours);
}

void configure_once() {
    Run& r = g_run;
    if (r.configured) return;
    r.configured = true;
    g_game.clock = this_thread_clock();
    r.game_fine_start = fine_cpu(g_game.clock);
    r.game_coarse_start = cpu_ns(g_game.clock);
    g_game.known.store(true, std::memory_order_release);
    hold_settings();
    r.runtime = get_runtime_settings();
    RuntimeSettings settings = r.runtime;
    settings.focus_loss = FocusLoss::KeepRunning;
    set_runtime_settings(settings);
    r.gpu_timing_was_on = render::gpu_timing_on();
    render::set_gpu_timing(true);
}

void finish(bool complete) {
    Run& r = g_run;
    g_measuring.store(false, std::memory_order_relaxed);
    g_arrive_deadline_ns.store(0, std::memory_order_relaxed);
    if (!complete) r.phase = Phase::Failed;
    write_result(complete);
    if (r.settings_held) {
        render::set_render_config(r.chosen);
    }
    if (r.configured) {
        set_runtime_settings(r.runtime);
        render::set_gpu_timing(r.gpu_timing_was_on);
    }
    if (complete) r.phase = Phase::Done;
    if (!r.options.here) {
        render::host_window::quit_request.store(true, std::memory_order_release);
        return;
    }
    // The run in the running game ends: the hooks off and running() false, so the reset it asks for reads the player's
    // saves; reset_done hands the result on.
    g_picture_hook.store(nullptr, std::memory_order_release);
    g_gpu_frame_hook.store(nullptr, std::memory_order_release);
    g_audio_hook.store(nullptr, std::memory_order_release);
    g_ending.store(true, std::memory_order_release);
    g_running.store(false, std::memory_order_release);
    g_resets_asked.fetch_add(1u, std::memory_order_release);
    std::printf("[benchmark] ended: the game goes back to its title\n");
    std::fflush(stdout);
}

void enter(Phase phase) {
    g_run.phase = phase;
    g_run.phase_frames = 0u;
}

// The cover at `now`, 0 to 255: a straight fade over kCoverNs.
std::uint32_t cover_at(const Run& r, std::int64_t now) {
    const double done = std::clamp(static_cast<double>(now - r.cover_ns) / static_cast<double>(kCoverNs), 0.0, 1.0);
    return static_cast<std::uint32_t>(
        std::lround(r.cover_from + (static_cast<double>(r.cover_to) - static_cast<double>(r.cover_from)) * done));
}

// The cover fades to `to` (0 clear, 255 black) from where it is.
void fade_cover(Run& r, std::uint8_t to, std::int64_t now) {
    if (r.cover_to == to) return;
    r.cover_from = static_cast<std::uint8_t>(cover_at(r, now));
    r.cover_to = to;
    r.cover_ns = now;
}

const char* item_label(const Item& item) {
    switch (item.kind) {
    case Kind::Probe:
        return kProbeNames[static_cast<std::size_t>(item.probe)];
    case Kind::Standard:
        return "standard";
    case Kind::Thermal:
        return "again";
    case Kind::Scene:
        break;
    }
    return "your settings";
}

void progress_line() {
    const Item& item = g_run.items[g_run.index];
    std::printf("[benchmark] %zu/%zu %s %s\n", g_run.index + 1u, g_run.items.size(), item.stage.id.c_str(),
                item_label(item));
    std::fflush(stdout);
}

int ask_travel(char* stage, std::uint32_t capacity, std::int32_t* star, std::string_view current) {
    Run& r = g_run;
    const Stage& s = r.items[r.index].stage;
    std::snprintf(stage, capacity, "%s", s.id.c_str());
    *star = s.grand ? 6 : static_cast<std::int32_t>(s.scenario);
    r.stage_at_ask = std::string(current);
    r.saw_start = false;
    r.arrived = false;
    r.asked_ns = steady_ns();
    r.at_ask = counters_now();
    g_arrive_deadline_ns.store(r.asked_ns + kArriveTimeoutNs, std::memory_order_relaxed);
    ++r.asks;
    enter(Phase::Arrive);
    return s.grand ? NEBULA_GALAXY_TRAVEL_GRAND_STARS : NEBULA_GALAXY_TRAVEL_STAGE;
}

// The warm-up before a window (scene_frame), its counters from now.
void enter_warm() {
    g_run.warm_seen = counters_now();
    g_run.quiet_since = 0u;
    enter(Phase::Warm);
}

void next_item() {
    Run& r = g_run;
    render::set_render_config(item_config(r.items[r.index]));
    progress_line();
    enter(Phase::Settle);
}

// What the last run in this game left (Options::here), before another starts: its run, its samples and counters, its
// saves. The threads' clocks are taken again (their handles closed) as the new run first meets each thread.
void clear_last_run() {
    g_run = Run{};
    for (ThreadSlot* slot : {&g_game, &g_render, &g_audio}) {
        if (slot->known.exchange(false, std::memory_order_acq_rel)) close_clock(slot->clock);
    }
    g_render_fine_start = -1;
    g_render_coarse_start = -1;
    {
        const std::lock_guard<std::mutex> lock(g_picture_mutex);
        g_pictures.clear();
        g_gpu_frames.clear();
    }
    {
        const std::lock_guard<std::mutex> lock(g_recent_mutex);
        g_recent_count = 0u;
    }
    g_saves.clear();
    g_progress_shown = 0u;
    g_memory_read = galaxy::ClockInterval{500, true};
    g_measuring.store(false, std::memory_order_relaxed);
    g_gpu_peak.store(0u, std::memory_order_relaxed);
    g_arrive_deadline_ns.store(0, std::memory_order_relaxed);
    g_stalled.store(false, std::memory_order_relaxed);
    g_stop_asked.store(false, std::memory_order_relaxed);
    g_ending.store(false, std::memory_order_relaxed);
    g_resets_asked.store(0u, std::memory_order_relaxed);
    for (std::atomic<std::uint64_t>* counter : {&g_read_bytes, &g_draw_items, &g_texture_uploads, &g_texture_bytes}) {
        counter->store(0u, std::memory_order_relaxed);
    }
    for (std::atomic<std::uint64_t>& part : g_render_part_ns) part.store(0u, std::memory_order_relaxed);
}

}  // namespace

bool start(const Options& options) {
    const std::lock_guard<std::mutex> lock(g_run_mutex);
    if (running() || (g_ending.load(std::memory_order_acquire) && g_run.options.here)) return false;
    clear_last_run();
    Run& r = g_run;
    r.options = options;
    if (options.here) {
        r.phase = Phase::Reset;
        hold_settings();
    }
    r.file = options.out_file;
    if (r.file.empty()) {
        const std::filesystem::path folder = results_folder();
        std::error_code error;
        if (!folder.empty() && (std::filesystem::create_directories(folder, error), !error)) {
            r.file = new_result_file(folder);
        }
    }
    std::string text = kBuiltInStages;
    std::error_code error;
    const std::filesystem::path override_file = options.pak_folder / "benchmark.tsv";
    if (!options.pak_folder.empty() && std::filesystem::is_regular_file(override_file, error)) {
        std::ifstream in(override_file, std::ios::binary);
        std::ostringstream all;
        all << in.rdbuf();
        std::vector<Stage> stages;
        Stage probe;
        if (parse_stages(all.str(), stages, probe)) {
            text = all.str();
        } else {
            std::printf("[benchmark] benchmark.tsv has no usable stage or probe row: the built-in stages run\n");
        }
    }
    (void)parse_stages(text, r.stages, r.probe_stage);
    // Each stage at your settings and then at the standard settings, the probe, then the first stage again.
    for (const Stage& s : r.stages) {
        r.items.push_back({Kind::Scene, s, ProbeCase::Small});
        r.items.push_back({Kind::Standard, s, ProbeCase::Small});
    }
    for (unsigned p = 0; p < static_cast<unsigned>(ProbeCase::Count); ++p) {
        r.items.push_back({Kind::Probe, r.probe_stage, static_cast<ProbeCase>(p)});
    }
    r.items.push_back({Kind::Thermal, r.stages.front(), ProbeCase::Small});
    r.windows.reserve(r.items.size());
    g_here.store(options.here, std::memory_order_relaxed);
    g_picture_hook.store(on_picture, std::memory_order_release);
    g_gpu_frame_hook.store(on_gpu_frame, std::memory_order_release);
    g_audio_hook.store(on_audio, std::memory_order_release);
    g_running.store(true, std::memory_order_release);
    // Running first: the reset's saves are the run's blank ones.
    if (options.here) g_resets_asked.store(1u, std::memory_order_release);
    std::printf("[benchmark] %zu steps%s\n", r.items.size(), options.here ? ", in the running game" : "");
    std::fflush(stdout);
    return true;
}

bool take_reset() noexcept {
    std::uint32_t asked = g_resets_asked.load(std::memory_order_acquire);
    while (asked != 0u) {
        if (g_resets_asked.compare_exchange_weak(asked, asked - 1u, std::memory_order_acq_rel)) return true;
    }
    return false;
}

void reset_done() noexcept {
    try {
        std::filesystem::path result;
        void (*ended)(const std::filesystem::path&) = nullptr;
        {
            const std::lock_guard<std::mutex> lock(g_run_mutex);
            Run& r = g_run;
            if (!r.options.here) return;
            if (running() && r.phase == Phase::Reset) {
                // The start's reset: the title is next, and the run counts its frames from it.
                enter(Phase::Title);
                return;
            }
            if (!g_ending.exchange(false, std::memory_order_acq_rel)) return;
            ended = r.options.ended;
            result = r.file;
        }
        if (ended != nullptr) ended(result);
    } catch (...) {
    }
}

int scene_frame(std::string_view current_stage, char* stage, std::uint32_t capacity, std::int32_t* star) {
    if (!running()) {
        return NEBULA_GALAXY_TRAVEL_NONE;
    }
    const std::lock_guard<std::mutex> lock(g_run_mutex);
    Run& r = g_run;
    if (r.phase == Phase::Done || r.phase == Phase::Failed || r.finished) {
        return NEBULA_GALAXY_TRAVEL_NONE;
    }
    if (r.phase == Phase::Reset) {
        return NEBULA_GALAXY_TRAVEL_NONE;  // the game's reset to its title runs (reset_done)
    }
    configure_once();
    if (r.options.here && (stopping() || g_stalled.load(std::memory_order_relaxed))) {
        if (stopping()) {
            r.failure = "stopped";
        } else if (r.index < r.items.size()) {
            r.failure = "the game did not enter " + r.items[r.index].stage.id + " in " +
                        std::to_string(kArriveTimeoutNs / 1'000'000'000) + " s";
        }
        finish(false);
        return NEBULA_GALAXY_TRAVEL_NONE;
    }
    const std::int64_t now = steady_ns();
    ++r.phase_frames;
    switch (r.phase) {
    case Phase::Reset:
        break;
    case Phase::Title:
        if (r.phase_frames >= kTitleFrames) next_item();
        break;
    case Phase::Leave:
        if (cover_at(r, now) >= 255u) next_item();
        break;
    case Phase::Settle:
        if (r.phase_frames >= kSettleFrames) enter(Phase::Pipelines);
        break;
    case Phase::Pipelines:
        if (render::pipeline_progress().left == 0u || r.phase_frames >= kPipelineFrames) {
            // On the stage the step measures already: no travel, only the settings changed.
            if (r.here_known && same_scene(r.here, r.items[r.index].stage) && current_stage == r.here.id) {
                r.windows.emplace_back();
                fade_cover(r, 0u, now);
                enter_warm();
                break;
            }
            r.asks = 0u;
            return ask_travel(stage, capacity, star, current_stage);
        }
        break;
    case Phase::Arrive: {
        const std::string& target = r.items[r.index].stage.id;
        if (r.arrived) {
            // Its opening and Mario's arrival play, shown and not measured: the windows measure play.
            if (g_play.load(std::memory_order_relaxed)) {
                enter_warm();
            } else if (r.phase_frames >= kPlayFrames) {
                r.failure = "the game did not reach play in " + target;
                finish(false);
                return NEBULA_GALAXY_TRAVEL_NONE;
            }
            break;
        }
        // Another stage: its name is reported only from its own scene's draws. The stage it is on: its new scene's start.
        if (current_stage == target && (r.saw_start || r.stage_at_ask != target)) {
            g_arrive_deadline_ns.store(0, std::memory_order_relaxed);
            const Counters arrived = counters_now();
            Window& window = r.windows.emplace_back();
            window.loaded = true;
            window.load_ms = static_cast<double>(now - r.asked_ns) / 1e6;
            window.load_read_bytes = arrived.read_bytes - r.at_ask.read_bytes;
            window.load_textures = arrived.textures - r.at_ask.textures;
            window.load_pipelines = arrived.pipelines - r.at_ask.pipelines;
            r.here = r.items[r.index].stage;
            r.here_known = true;
            r.arrived = true;
            r.phase_frames = 0u;  // from here, the wait for play
            fade_cover(r, 0u, now);
        } else if (r.phase_frames >= kArriveFrames) {
            if (r.asks >= kAsks) {
                r.failure = "the game did not enter " + target;
                finish(false);
                return NEBULA_GALAXY_TRAVEL_NONE;
            }
            return ask_travel(stage, capacity, star, current_stage);
        }
        break;
    }
    case Phase::Warm: {
        // Paced as in play until the scene has made what it shows: no texture made and no pipeline built for
        // kQuietFrames, none left to build.
        const Counters seen = counters_now();
        if (seen.textures != r.warm_seen.textures || seen.pipelines != r.warm_seen.pipelines ||
            render::pipeline_progress().left != 0u) {
            r.warm_seen = seen;
            r.quiet_since = r.phase_frames;
        }
        const bool made = (r.phase_frames >= kWarmFrames && r.phase_frames - r.quiet_since >= kQuietFrames) ||
                          r.phase_frames >= kWarmMostFrames;
        if (made && cover_at(r, now) == 0u) {
            r.restarted = false;
            enter(Phase::Drain);
        }
        break;
    }
    case Phase::Drain:
        if (r.phase_frames >= kDrainFrames) {
            Window& window = r.windows.back();
            const Item& item = r.items[r.index];
            const render::RenderConfig asked = item_config(item);
            window.held = config_holds(asked);
            window.output_w = output_width();
            window.output_h = output_lines();
            window.pixels = scene_pixels(asked, window.output_w, window.output_h);
            // The pictures a second the warm-up showed (no cap: the device's own rate), a quarter again.
            const double rate = std::max(render::host_window::measured_present_hz.load(std::memory_order_relaxed),
                                         static_cast<double>(kGameStepsPerSecond));
            const double expected = std::min(rate * item.stage.seconds * 1.25 + 256.0, static_cast<double>(kMostReservedPictures));
            begin_window(window, static_cast<std::size_t>(expected));
            enter(Phase::Measure);
        }
        break;
    case Phase::Measure:
        if (r.restarted) {
            // The scene began again (the player character lost a life): the window no longer measures one scene.
            end_window(r.windows.back());
            r.failure = "the scene started again in " + r.items[r.index].stage.id;
            finish(false);
            return NEBULA_GALAXY_TRAVEL_NONE;
        }
        if (now - r.windows.back().start_ns >= std::int64_t{r.items[r.index].stage.seconds} * 1'000'000'000) {
            r.windows.back().game_frames = r.phase_frames;
            end_window(r.windows.back());
            if (++r.index == r.items.size()) {
                finish(true);
                return NEBULA_GALAXY_TRAVEL_NONE;
            }
            fade_cover(r, 255u, now);
            enter(Phase::Leave);
        }
        break;
    case Phase::Done:
    case Phase::Failed:
        break;
    }
    return NEBULA_GALAXY_TRAVEL_NONE;
}

void scene_started() noexcept {
    g_play.store(false, std::memory_order_relaxed);
    if (!running()) return;
    try {
        const std::lock_guard<std::mutex> lock(g_run_mutex);
        if (g_run.phase == Phase::Arrive) g_run.saw_start = true;
        if (g_run.phase == Phase::Drain || g_run.phase == Phase::Measure) g_run.restarted = true;
    } catch (...) {
    }
}

void note_play(bool play) noexcept {
    g_play.store(play, std::memory_order_relaxed);
}

std::int32_t save_request(int op, std::string_view name, void* buffer, std::uint32_t size, std::uint32_t* length) noexcept {
    return g_saves.request(op, name, buffer, size, length);
}

void at_exit() noexcept {
    if (!running()) return;
    try {
        const std::lock_guard<std::mutex> lock(g_run_mutex);
        Run& r = g_run;
        if (stopping() && !r.finished) {
            r.failure = "stopped";
        } else if (g_stalled.load(std::memory_order_relaxed) && r.phase == Phase::Arrive && r.index < r.items.size()) {
            // The quit on_picture asked for: the travel nothing answered fails the run.
            r.phase = Phase::Failed;
            r.failure = "the game did not enter " + r.items[r.index].stage.id + " in " +
                        std::to_string(kArriveTimeoutNs / 1'000'000'000) + " s";
        }
        write_result(false);
    } catch (...) {
    }
}

bool live_state(LiveState& out) {
    if (!running()) return false;
    const std::lock_guard<std::mutex> lock(g_run_mutex);
    const Run& r = g_run;
    if (r.items.empty() || r.finished || r.phase == Phase::Done || r.phase == Phase::Failed) return false;
    const std::size_t index = std::min(r.index, r.items.size() - 1u);
    const Item& item = r.items[index];
    out.step = static_cast<std::uint32_t>(index + 1u);
    out.steps = static_cast<std::uint32_t>(r.items.size());
    out.part = item.kind == Kind::Standard ? Part::Standard
               : item.kind == Kind::Probe  ? Part::Probe
               : item.kind == Kind::Thermal ? Part::Again
                                            : Part::Yours;
    out.showing = r.phase == Phase::Leave || (r.phase == Phase::Arrive && r.arrived) || r.phase == Phase::Warm ||
                  r.phase == Phase::Drain || r.phase == Phase::Measure;
    out.cover = cover_at(r, steady_ns());
    out.stopping = stopping();
    out.stage = item.stage.id;
    // The step's part done: a share for each phase before its frames, then its measured frames.
    std::uint32_t within = 0u;
    switch (r.phase) {
    case Phase::Reset:
    case Phase::Title:
    case Phase::Leave:
    case Phase::Settle:
        break;
    case Phase::Pipelines:
        within = kStepBeforeMeasure / 6u;
        break;
    case Phase::Arrive:
        within = kStepBeforeMeasure / 3u;
        break;
    case Phase::Warm:
        within = kStepBeforeMeasure * 2u / 3u;
        break;
    case Phase::Drain:
        within = kStepBeforeMeasure * 5u / 6u;
        break;
    case Phase::Measure: {
        const std::int64_t window_ns = std::int64_t{item.stage.seconds} * 1'000'000'000;
        const std::int64_t elapsed = std::clamp<std::int64_t>(steady_ns() - r.windows.back().start_ns, 0, window_ns);
        within = kStepBeforeMeasure + static_cast<std::uint32_t>(std::uint64_t{10000u - kStepBeforeMeasure} *
                                                                 static_cast<std::uint64_t>(elapsed) /
                                                                 static_cast<std::uint64_t>(std::max<std::int64_t>(window_ns, 1)));
        break;
    }
    case Phase::Done:
    case Phase::Failed:
        within = 10000u;
        break;
    }
    const std::uint64_t done = std::uint64_t{index} * 10000u + within;
    g_progress_shown = std::max(g_progress_shown, static_cast<std::uint32_t>(done / r.items.size()));
    out.progress = std::min(g_progress_shown, 10000u);
    // The pictures shown a second: every present, the game's frames and the pictures drawn between them (the rate the
    // FRAME RATE row shows, measured by the renderer over the last second). A game picture older than the window means
    // the game stalls: no rate is shown then.
    out.fps_tenths = 0u;
    {
        const std::lock_guard<std::mutex> recent(g_recent_mutex);
        if (g_recent_count != 0u) {
            const std::uint64_t newest = g_recent[(g_recent_count - 1u) % kRecentPictures];
            const auto now = static_cast<std::uint64_t>(steady_ns());
            const double presented = render::host_window::measured_present_hz.load(std::memory_order_relaxed);
            if (now >= newest && now - newest < static_cast<std::uint64_t>(kRateWindowNs) && presented > 0.0) {
                out.fps_tenths = static_cast<std::uint32_t>(std::llround(std::min(presented, 1e8) * 10.0));
            }
        }
    }
    return true;
}

}  // namespace galaxy::bench
