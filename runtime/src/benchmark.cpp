// SPDX-License-Identifier: GPL-3.0-only
// The benchmark mode (benchmark.h): its stages, the run on the game thread, the measurements and the result file.
#include "galaxy/benchmark.h"

#include "galaxy/atomic_file.h"
#include "galaxy/benchmark_recommend.h"
#include "galaxy/cpu_count.h"
#include "galaxy/platform_paths.h"
#include "galaxy/platform_time.h"
#include "galaxy/render/resolution_fit.h"
#include "galaxy/render/galaxy_select.h"
#include "galaxy/render/host_window.h"
#include "galaxy/render/render_config.h"
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
#include <map>
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

// Stage, scenario, kind (stage: measured at the player's settings; grand6: the same with the Observatory's six Grand
// Stars, so its domes are built; probe: the stage the probe enters again for each configuration) and measured frames.
// Why each: docs in board/bench.md (one per cost class: open planets, bloom, water, particles, the hub).
constexpr const char* kBuiltInStages =
    "CosmosGardenGalaxy\t1\tstage\t540\n"
    "BattleShipGalaxy\t1\tstage\t540\n"
    "HeavenlyBeachGalaxy\t1\tstage\t540\n"
    "HellProminenceGalaxy\t1\tstage\t540\n"
    "AstroGalaxy\t1\tgrand6\t540\n"
    "BattleShipGalaxy\t1\tprobe\t300\n";

constexpr std::uint32_t kSkipFrames = 60u;       // after arrival: the wipe opening, the first pipelines, the pass averages
constexpr std::uint32_t kTitleFrames = 120u;     // title frames before the first travel
constexpr std::uint32_t kArriveFrames = 900u;    // scene frames a travel may go unanswered before it is asked again
constexpr std::uint32_t kAsks = 3u;              // asks before the run fails
constexpr std::uint32_t kPipelineFrames = 1200u; // the longest wait for a settings change's pipelines
constexpr std::uint32_t kSettleFrames = 10u;     // frames after a settings change before its pipelines are counted
constexpr std::int64_t kLoadGapNs = 200'000'000; // a pause in scene frames this long after a travel is its load

struct Stage {
    std::string id;
    std::uint32_t scenario = 1u;
    bool grand = false;
    std::uint32_t frames = 540u;
};

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
        if (!(row >> s.id >> s.scenario >> kind >> s.frames) || !known_stage(s.id, s.scenario) || s.frames < 60u ||
            s.frames > 3600u) {
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

// ---- thread clocks (the clocks perf_hint.cpp reads) ----------------------------------------------------------------

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

// CPU nanoseconds the thread has run; -1 when unknown.
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

std::int64_t process_cpu_ns() {
#if defined(_WIN32)
    FILETIME created{}, exited{}, kernel{}, user{};
    return GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user) ? filetime_ns(kernel) + filetime_ns(user)
                                                                                   : -1;
#else
    timespec now{};
    return clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &now) == 0 ? std::int64_t{now.tv_sec} * 1000000000 + now.tv_nsec : -1;
#endif
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

// Device strings go into the result: printable ASCII only, at most 96 bytes, trimmed.
std::string clean(std::string_view text) {
    std::string out;
    for (const char c : text) {
        if (out.size() >= 96u) break;
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
#else
    return "d3d12";
#endif
}

// ---- the run -------------------------------------------------------------------------------------------------------

enum class Kind { Scene, Probe, Thermal };

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

struct PassTime {
    const char* name = nullptr;  // static (render_api.h GpuPassTime)
    double sum = 0.0;
};

struct Result {
    std::uint32_t frames = 0u;
    double load_ms = 0.0;
    std::vector<double> intervals_ms;
    std::vector<PassTime> passes;
    double gpu_ms = 0.0;
    double game_ms = -1.0, render_ms = -1.0, audio_ms = -1.0, process_ms = -1.0;
    std::uint64_t gpu_memory_peak = 0u, gpu_budget = 0u, process_peak = 0u;
    double p50 = 0.0, p95 = 0.0, p99 = 0.0, worst = 0.0;
    std::uint32_t over_16 = 0u, over_33 = 0u;
    double pixels = 0.0;     // the scene's pixels at the item's settings
    bool applied = true;     // the settings asked for are the ones that ran (a pass the device cannot run is clamped)
};

enum class Phase { Title, Settle, Pipelines, Travel, Arrive, Skip, Measure, Done, Failed };

struct ThreadSlot {
    std::atomic<bool> known{false};
    ThreadClock clock;
};

struct Run {
    Options options;
    std::filesystem::path file;  // where the result goes (start())
    std::vector<Stage> stages;
    Stage probe_stage;
    std::vector<Item> items;
    std::vector<Result> results;
    std::size_t index = 0u;
    Phase phase = Phase::Title;
    std::uint32_t phase_frames = 0u;
    std::uint32_t asks = 0u;
    std::string stage_at_ask;
    bool saw_gap = false;
    std::int64_t asked_ns = 0, last_frame_ns = 0;
    std::string failure;

    bool configured = false;
    render::RenderConfig chosen;  // the player's, put back at the end
    render::RenderConfig phase1;  // what the stages run at
    RuntimeSettings runtime;      // the player's runtime settings, put back at the end

    // The measured window's start.
    ThreadClock game_clock;
    std::int64_t start_ns = 0, start_game = -1, start_render = -1, start_audio = -1, start_process = -1;
    bool finished = false;
};

Run g_run;
// Held by the game thread's step and by the exit: the result is written once, never while the run changes it.
std::mutex g_run_mutex;

ThreadSlot g_render;
ThreadSlot g_audio;

// The measured window's pictures (render thread).
std::mutex g_picture_mutex;
std::vector<std::uint64_t> g_pictures;
std::atomic<bool> g_measuring{false};
// The GPU memory high-water of the window and the last budget read, sampled on the render thread every 30 pictures.
std::atomic<std::uint64_t> g_gpu_peak{0u};
std::atomic<std::uint64_t> g_gpu_budget{0u};
std::uint32_t g_memory_count = 0u;

// The game's saves while it runs.
std::mutex g_save_mutex;
std::map<std::string, std::string, std::less<>> g_saves;

void on_picture(std::uint64_t ns) {
    if (!g_render.known.load(std::memory_order_acquire)) {
        g_render.clock = this_thread_clock();
        g_render.known.store(true, std::memory_order_release);
    }
    if (g_measuring.load(std::memory_order_relaxed)) {
        {
            const std::lock_guard<std::mutex> lock(g_picture_mutex);
            g_pictures.push_back(ns);
        }
        std::uint64_t usage = 0u, budget = 0u;
        if (++g_memory_count % 30u == 1u && render::video_memory(usage, budget)) {
            if (usage > g_gpu_peak.load(std::memory_order_relaxed)) g_gpu_peak.store(usage, std::memory_order_relaxed);
            g_gpu_budget.store(budget, std::memory_order_relaxed);
        }
    }
}

void on_audio() {
    if (!g_audio.known.load(std::memory_order_acquire)) {
        g_audio.clock = this_thread_clock();
        g_audio.known.store(true, std::memory_order_release);
        g_audio_hook.store(nullptr, std::memory_order_release);
    }
}

std::int64_t slot_ns(ThreadSlot& slot) {
    return slot.known.load(std::memory_order_acquire) ? cpu_ns(slot.clock) : -1;
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

// The scene's pixels at a configuration: the internal height (0: the panel's), or an upscaler's ratio of the output.
double scene_pixels(const render::RenderConfig& cfg) {
    const unsigned width = output_width(), lines = output_lines();
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
    render::RenderConfig cfg = g_run.phase1;
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
    return item.kind == Kind::Probe ? probe_config(item.probe) : g_run.phase1;
}

// Whether the renderer runs what was asked: a pass the device cannot run is clamped off when set.
bool config_holds(const render::RenderConfig& asked) {
    const render::RenderConfig running = render::get_render_config();
    return running.msaa_samples == asked.msaa_samples && running.ambient_occlusion == asked.ambient_occlusion &&
           running.enhanced_lighting == asked.enhanced_lighting && running.aa_mode == asked.aa_mode &&
           running.bloom == asked.bloom && running.internal_height == asked.internal_height &&
           running.upscaler == asked.upscaler;
}

double percentile(const std::vector<double>& sorted, double p) {
    if (sorted.empty()) return 0.0;
    const std::size_t rank = static_cast<std::size_t>(std::ceil(p * static_cast<double>(sorted.size()))) ;
    return sorted[std::min(sorted.size() - 1u, rank == 0u ? 0u : rank - 1u)];
}

void begin_window() {
    Run& r = g_run;
    {
        const std::lock_guard<std::mutex> lock(g_picture_mutex);
        g_pictures.clear();
    }
    g_gpu_peak.store(0u, std::memory_order_relaxed);
    g_measuring.store(true, std::memory_order_relaxed);
    r.start_ns = steady_ns();
    r.start_game = cpu_ns(r.game_clock);
    r.start_render = slot_ns(g_render);
    r.start_audio = slot_ns(g_audio);
    r.start_process = process_cpu_ns();
}

void sample_frame(Result& result) {
    render::GpuPassTime passes[24];
    const std::size_t count = render::gpu_pass_times(passes, std::size(passes));
    for (std::size_t i = 0; i < count; ++i) {
        auto at = std::find_if(result.passes.begin(), result.passes.end(),
                               [&](const PassTime& p) { return std::strcmp(p.name, passes[i].name) == 0; });
        if (at == result.passes.end()) {
            result.passes.push_back({passes[i].name, 0.0});
            at = result.passes.end() - 1;
        }
        at->sum += passes[i].ms;
    }
}

void end_window(Result& result) {
    Run& r = g_run;
    g_measuring.store(false, std::memory_order_relaxed);
    std::vector<std::uint64_t> pictures;
    {
        const std::lock_guard<std::mutex> lock(g_picture_mutex);
        pictures.swap(g_pictures);
    }
    const double frames = std::max(1.0, static_cast<double>(result.frames));
    const auto per_frame = [&](std::int64_t start, std::int64_t end) {
        return start >= 0 && end >= start ? static_cast<double>(end - start) / 1e6 / frames : -1.0;
    };
    result.game_ms = per_frame(r.start_game, cpu_ns(r.game_clock));
    result.render_ms = per_frame(r.start_render, slot_ns(g_render));
    result.audio_ms = per_frame(r.start_audio, slot_ns(g_audio));
    result.process_ms = per_frame(r.start_process, process_cpu_ns());
    result.process_peak = process_peak_bytes();
    result.gpu_memory_peak = g_gpu_peak.load(std::memory_order_relaxed);
    result.gpu_budget = g_gpu_budget.load(std::memory_order_relaxed);
    result.gpu_ms = 0.0;
    for (PassTime& pass : result.passes) {
        pass.sum /= frames;
        result.gpu_ms += pass.sum;
    }
    for (std::size_t i = 1; i < pictures.size(); ++i) {
        if (pictures[i] > pictures[i - 1u]) {
            result.intervals_ms.push_back(static_cast<double>(pictures[i] - pictures[i - 1u]) / 1e6);
        }
    }
    std::vector<double> sorted = result.intervals_ms;
    std::sort(sorted.begin(), sorted.end());
    result.p50 = percentile(sorted, 0.50);
    result.p95 = percentile(sorted, 0.95);
    result.p99 = percentile(sorted, 0.99);
    result.worst = sorted.empty() ? 0.0 : sorted.back();
    // A frame missed by more than a quarter of a frame: 60 Hz pacing jitter is not a late frame.
    constexpr double kStep = 1000.0 / 60.0;
    for (const double ms : sorted) {
        result.over_16 += ms > kStep * 1.25 ? 1u : 0u;
        result.over_33 += ms > kStep * 2.25 ? 1u : 0u;
    }
    result.intervals_ms.clear();
    result.intervals_ms.shrink_to_fit();
}

// ---- the result ----------------------------------------------------------------------------------------------------

std::string number(double value) {
    if (!std::isfinite(value)) return "null";
    char text[32];
    std::snprintf(text, sizeof(text), "%.3f", value);
    for (char* p = text; *p != '\0'; ++p) {
        if (*p == ',') *p = '.';  // a process locale with a decimal comma (set by a toolkit or a plugin): JSON has '.'
    }
    return text;
}

std::string quoted(std::string_view text) {
    return "\"" + clean(text) + "\"";
}

std::string utc_stamp(bool file_name) {
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    char text[32];
    std::strftime(text, sizeof(text), file_name ? "%Y%m%dT%H%M%SZ" : "%Y-%m-%dT%H:%M:%SZ", &utc);
    return text;
}

void settings_json(std::ostringstream& out, const render::RenderConfig& c) {
    out << "{\"internal_height\":" << c.internal_height << ",\"upscaler\":" << c.upscaler << ",\"msaa_samples\":"
        << c.msaa_samples << ",\"ambient_occlusion\":" << (c.ambient_occlusion ? 1 : 0) << ",\"ambient_occlusion_ssao\":"
        << (c.ambient_occlusion_ssao ? 1 : 0) << ",\"enhanced_lighting\":" << (c.enhanced_lighting ? 1 : 0)
        << ",\"per_pixel_lighting\":" << (c.per_pixel_lighting ? 1 : 0) << ",\"aa\":" << c.aa_mode << ",\"smaa_quality\":"
        << c.smaa_quality << ",\"bloom\":" << c.bloom << ",\"vsync_interval\":" << c.vsync_interval << ",\"max_fps\":"
        << number(c.max_fps) << ",\"frame_generation\":" << c.frame_generation << ",\"ray_tracing_enabled\":"
        << (c.ray_tracing_enabled ? 1 : 0) << ",\"display_mode\":" << c.display_mode << "}";
}

void result_json(std::ostringstream& out, const Result& r) {
    out << "\"load_ms\":" << number(r.load_ms) << ",\"frames\":" << r.frames << ",\"pixels\":" << number(r.pixels)
        << ",\"settings_held\":" << (r.applied ? "true" : "false") << ",\"interval_ms\":{\"p50\":" << number(r.p50)
        << ",\"p95\":" << number(r.p95) << ",\"p99\":" << number(r.p99) << ",\"worst\":" << number(r.worst)
        << ",\"over_16_7\":" << r.over_16 << ",\"over_33_3\":" << r.over_33 << "},\"gpu_ms\":" << number(r.gpu_ms)
        << ",\"gpu_pass_ms\":{";
    for (std::size_t i = 0; i < r.passes.size(); ++i) {
        out << (i ? "," : "") << quoted(r.passes[i].name) << ":" << number(r.passes[i].sum);
    }
    const auto cpu = [](double ms) { return ms < 0.0 ? std::string("null") : number(ms); };
    out << "},\"cpu_ms\":{\"game\":" << cpu(r.game_ms) << ",\"render\":" << cpu(r.render_ms) << ",\"audio\":"
        << cpu(r.audio_ms) << ",\"process\":" << cpu(r.process_ms) << "},\"draws\":null,\"triangles\":null"
        << ",\"gpu_memory_peak_mb\":" << (r.gpu_memory_peak >> 20u) << ",\"process_peak_mb\":" << (r.process_peak >> 20u);
}

const char* status_name(bool complete) {
    return complete ? "complete" : g_run.phase == Phase::Failed ? "failed" : "aborted";
}

// The recommendation's inputs from the results.
Measured measured_inputs() {
    Run& run = g_run;
    Measured m;
    m.output_width = output_width();
    m.output_lines = output_lines();
    const render::RenderConfig& c = run.chosen;
    m.current.internal_height = c.internal_height;
    m.current.msaa_samples = c.msaa_samples;
    m.current.ambient_occlusion = c.ambient_occlusion;
    m.current.enhanced_lighting = c.enhanced_lighting;
    m.current.aa = c.aa_mode;
    m.current.smaa_quality = c.smaa_quality;
    m.current.bloom = c.bloom;
    m.upscaler = c.upscaler != 0u;
    m.current_pixels = scene_pixels(run.phase1);
    const Result* worst = nullptr;
    const Result* probe[static_cast<std::size_t>(ProbeCase::Count)] = {};
    for (std::size_t i = 0; i < run.items.size() && i < run.results.size(); ++i) {
        const Result& r = run.results[i];
        if (run.items[i].kind == Kind::Scene) {
            if (worst == nullptr || r.gpu_ms > worst->gpu_ms) worst = &r;
            m.worst_game_cpu_ms = std::max(m.worst_game_cpu_ms, r.game_ms);
            m.worst_render_cpu_ms = std::max(m.worst_render_cpu_ms, r.render_ms);
        } else if (run.items[i].kind == Kind::Probe) {
            probe[static_cast<std::size_t>(run.items[i].probe)] = &r;
        }
    }
    if (worst != nullptr) {
        m.worst_gpu_ms = worst->gpu_ms;
        m.tail = worst->p50 > 0.0 ? worst->p95 / worst->p50 : 1.0;
    }
    const Result* at480 = probe[static_cast<std::size_t>(ProbeCase::Small)];
    const Result* at720 = probe[static_cast<std::size_t>(ProbeCase::Large)];
    if (at480 != nullptr && at720 != nullptr && at480->applied && at720->applied) {
        const double pixels[] = {at480->pixels, at720->pixels};
        const double ms[] = {at480->gpu_ms, at720->gpu_ms};
        m.base = render::fit_gpu_time(pixels, ms, 2u);
        for (std::size_t p = static_cast<std::size_t>(ProbeCase::Ao); p < static_cast<std::size_t>(ProbeCase::Count); ++p) {
            if (probe[p] != nullptr && probe[p]->applied && at480->pixels > 0.0) {
                m.price_per_mpixel[static_cast<std::size_t>(kProbeFeature[p])] =
                    std::max(0.0, (probe[p]->gpu_ms - at480->gpu_ms) / (at480->pixels / 1e6));
            }
        }
    }
    return m;
}

// With g_run_mutex held.
void write_result(bool complete) {
    Run& run = g_run;
    if (run.finished) return;
    run.finished = true;
    std::ostringstream out;
    out.imbue(std::locale::classic());
    const render::RenderAdapter adapter = render::render_adapter();
    const std::uint64_t budget = g_gpu_budget.load(std::memory_order_relaxed);
    std::int64_t vblank = 0, period = 0;
    const double refresh = render::host_window::vblank_grid_known(vblank, period) && period > 0
                               ? static_cast<double>(qpc_frequency()) / static_cast<double>(period)
                               : 0.0;
    out << "{\"schema\":1,\"app\":{\"version\":" << quoted(NEBULA_VERSION) << ",\"commit\":null},\"created_utc\":"
        << quoted(utc_stamp(false)) << ",\"status\":\"" << status_name(complete) << "\",\"failure\":" << quoted(run.failure)
        << ",\"backend\":\"" << backend_name() << "\",\"device\":{\"gpu\":" << quoted(adapter.description)
        << ",\"vendor_id\":" << adapter.vendor_id << ",\"device_id\":" << adapter.device_id
        << ",\"driver_version\":null,\"api_level\":null,\"shared_memory\":" << (render::rt_caps().unified_memory ? "true" : "false")
        << ",\"gpu_budget_mb\":" << (budget >> 20u) << ",\"cpu\":" << quoted(cpu_brand()) << ",\"logical_cores\":"
        << logical_cores() << ",\"ram_mb\":" << (ram_bytes() >> 20u) << ",\"os\":" << quoted(os_version())
        << ",\"output\":[" << render::host_window::backbuffer_width << "," << render::host_window::backbuffer_height
        << "],\"refresh_hz\":" << (refresh > 0.0 ? number(refresh) : std::string("null")) << "},\"mods_loaded\":false,\"settings\":";
    settings_json(out, run.chosen);
    out << ",\"measured_with\":";
    settings_json(out, run.phase1);
    out << ",\"limits\":[\"the game does not report when a stage's intro camera ends: each scene's window holds its entry "
           "(intro camera, star name, start) without a split\",\"draws and triangles: no back-end-neutral counter\","
           "\"gpu pass times are each back end's ~10-frame running average, averaged over the window\"],\"scenes\":[";
    bool first = true;
    for (std::size_t i = 0; i < run.results.size() && i < run.items.size(); ++i) {
        if (run.items[i].kind != Kind::Scene) continue;
        out << (first ? "" : ",") << "{\"stage\":" << quoted(run.items[i].stage.id) << ",\"scenario\":"
            << run.items[i].stage.scenario << ",\"grand_stars\":" << (run.items[i].stage.grand ? 6 : 0) << ",";
        result_json(out, run.results[i]);
        out << "}";
        first = false;
    }
    out << "],\"probe\":{\"stage\":" << quoted(run.probe_stage.id) << ",\"runs\":[";
    first = true;
    for (std::size_t i = 0; i < run.results.size() && i < run.items.size(); ++i) {
        if (run.items[i].kind != Kind::Probe) continue;
        out << (first ? "" : ",") << "{\"case\":" << quoted(kProbeNames[static_cast<std::size_t>(run.items[i].probe)]) << ",";
        result_json(out, run.results[i]);
        out << "}";
        first = false;
    }
    const Measured m = measured_inputs();
    out << "],\"fit\":{\"valid\":" << (m.base.valid ? "true" : "false") << ",\"fixed_ms\":" << number(m.base.a_ms)
        << ",\"ms_per_mpixel\":" << number(m.base.b_ms_per_pixel * 1e6) << "},\"price_ms_per_mpixel\":{";
    for (std::size_t p = static_cast<std::size_t>(ProbeCase::Ao); p < static_cast<std::size_t>(ProbeCase::Count); ++p) {
        const double price = m.price_per_mpixel[static_cast<std::size_t>(kProbeFeature[p])];
        out << (p == static_cast<std::size_t>(ProbeCase::Ao) ? "" : ",") << quoted(kProbeNames[p]) << ":"
            << (price < 0.0 ? std::string("null") : number(price));
    }
    out << "}},\"thermal\":";
    const Result* first_scene = nullptr;
    const Result* last_scene = nullptr;
    for (std::size_t i = 0; i < run.results.size() && i < run.items.size(); ++i) {
        if (run.items[i].kind == Kind::Scene && first_scene == nullptr) first_scene = &run.results[i];
        if (run.items[i].kind == Kind::Thermal) last_scene = &run.results[i];
    }
    if (first_scene != nullptr && last_scene != nullptr && first_scene->gpu_ms > 0.0) {
        out << "{\"stage\":" << quoted(run.stages.front().id) << ",\"gpu_ms_first\":" << number(first_scene->gpu_ms)
            << ",\"gpu_ms_last\":" << number(last_scene->gpu_ms) << ",\"gpu_ratio\":"
            << number(last_scene->gpu_ms / first_scene->gpu_ms) << ",\"p95_first\":" << number(first_scene->p95)
            << ",\"p95_last\":" << number(last_scene->p95) << "}";
    } else {
        out << "null";
    }
    out << ",\"recommendation\":";
    const Recommendation rec = complete ? recommend(m) : Recommendation{};
    if (!rec.valid) {
        out << "null";
    } else {
        const Choice& c = rec.choice;
        const Choice& now = m.current;
        out << "{\"budget_ms\":" << number(render::fit_budget_ms()) << ",\"predicted_ms\":" << number(rec.predicted_ms)
            << ",\"fits\":" << (rec.fits ? "true" : "false") << ",\"cpu_bound\":" << (rec.cpu_bound ? "true" : "false")
            << ",\"cpu_thread\":" << (rec.cpu_bound ? quoted(rec.cpu_thread) : std::string("null"))
            << ",\"height_left_out\":" << (rec.height_left_out ? "true" : "false") << ",\"settings\":[";
        const auto row = [&](const char* key, unsigned current, unsigned recommended, bool comma) {
            out << (comma ? "," : "") << "{\"key\":\"" << key << "\",\"current\":" << current << ",\"recommended\":"
                << recommended << "}";
        };
        bool comma = false;
        if (!rec.height_left_out) {
            row("internal_height", now.internal_height, c.internal_height, false);
            comma = true;
        }
        row("msaa_samples", now.msaa_samples, c.msaa_samples, comma);
        row("ambient_occlusion", now.ambient_occlusion ? 1u : 0u, c.ambient_occlusion ? 1u : 0u, true);
        row("enhanced_lighting", now.enhanced_lighting ? 1u : 0u, c.enhanced_lighting ? 1u : 0u, true);
        row("aa", now.aa, c.aa, true);
        row("smaa_quality", now.smaa_quality, c.smaa_quality, true);
        row("bloom", now.bloom, c.bloom, true);
        out << "]}";
    }
    // The apps' summary, from their own translated texts: verdict, the heaviest stage, its frame.
    const char* verdict = !rec.valid ? "none" : rec.cpu_bound ? "cpu_bound" : rec.fits ? "fits" : "below_target";
    std::string worst_stage;
    double worst_gpu = -1.0, worst_p95 = 0.0;
    for (std::size_t i = 0; i < run.results.size() && i < run.items.size(); ++i) {
        if (run.items[i].kind == Kind::Scene && run.results[i].gpu_ms > worst_gpu) {
            worst_gpu = run.results[i].gpu_ms;
            worst_p95 = run.results[i].p95;
            worst_stage = run.items[i].stage.id;
        }
    }
    out << ",\"summary\":{\"verdict\":\"" << verdict << "\",\"worst_stage\":" << quoted(worst_stage)
        << ",\"worst_gpu_ms\":" << number(std::max(worst_gpu, 0.0)) << ",\"worst_p95_ms\":" << number(worst_p95) << "}}\n";

    const std::filesystem::path& file = run.file;
    const bool written = !file.empty() && io::write_file_atomically(file, out.str());
    std::printf("[benchmark] %s: %s\n", status_name(complete), written ? "result written" : "the result could not be written");
    std::fflush(stdout);
}

// ---- the state machine (game thread) -------------------------------------------------------------------------------

void configure_once() {
    Run& r = g_run;
    if (r.configured) return;
    r.configured = true;
    r.game_clock = this_thread_clock();
    r.chosen = render::get_render_config_chosen();
    r.phase1 = r.chosen;
    r.phase1.vsync_interval = 0u;
    r.phase1.frame_generation = 0u;
    r.phase1.max_fps = 60.0f;
    r.phase1.ray_tracing_enabled = false;
    r.runtime = get_runtime_settings();
    RuntimeSettings settings = r.runtime;
    settings.focus_loss = FocusLoss::KeepRunning;
    set_runtime_settings(settings);
    render::set_gpu_timing(true);
}

void finish(bool complete) {
    Run& r = g_run;
    g_measuring.store(false, std::memory_order_relaxed);
    if (!complete) r.phase = Phase::Failed;
    write_result(complete);
    if (r.configured) {
        render::set_render_config(r.chosen);
        set_runtime_settings(r.runtime);
    }
    if (complete) r.phase = Phase::Done;
    render::host_window::quit_request.store(true, std::memory_order_release);
}

void enter(Phase phase) {
    g_run.phase = phase;
    g_run.phase_frames = 0u;
}

void progress_line() {
    const Item& item = g_run.items[g_run.index];
    std::printf("[benchmark] %zu/%zu %s %s\n", g_run.index + 1u, g_run.items.size(), item.stage.id.c_str(),
                item.kind == Kind::Probe ? kProbeNames[static_cast<std::size_t>(item.probe)]
                : item.kind == Kind::Thermal ? "again" : "");
    std::fflush(stdout);
}

int ask_travel(char* stage, std::uint32_t capacity, std::int32_t* star, std::string_view current) {
    Run& r = g_run;
    const Stage& s = r.items[r.index].stage;
    std::snprintf(stage, capacity, "%s", s.id.c_str());
    *star = s.grand ? 6 : static_cast<std::int32_t>(s.scenario);
    r.stage_at_ask = std::string(current);
    r.saw_gap = false;
    r.asked_ns = steady_ns();
    ++r.asks;
    enter(Phase::Arrive);
    return s.grand ? NEBULA_GALAXY_TRAVEL_GRAND_STARS : NEBULA_GALAXY_TRAVEL_STAGE;
}

}  // namespace

void start(const Options& options) {
    Run& r = g_run;
    r.options = options;
    r.file = options.out_file;
    if (r.file.empty()) {
        const std::filesystem::path folder = nebula_diagnostics_directory();
        if (!folder.empty()) r.file = folder / ("benchmark-" + utc_stamp(true) + ".json");
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
    for (const Stage& s : r.stages) r.items.push_back({Kind::Scene, s, ProbeCase::Small});
    for (unsigned p = 0; p < static_cast<unsigned>(ProbeCase::Count); ++p) {
        r.items.push_back({Kind::Probe, r.probe_stage, static_cast<ProbeCase>(p)});
    }
    r.items.push_back({Kind::Thermal, r.stages.front(), ProbeCase::Small});
    r.results.reserve(r.items.size());
    g_picture_hook.store(on_picture, std::memory_order_release);
    g_audio_hook.store(on_audio, std::memory_order_release);
    g_running.store(true, std::memory_order_release);
    std::printf("[benchmark] %zu steps\n", r.items.size());
    std::fflush(stdout);
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
    configure_once();
    const std::int64_t now = steady_ns();
    if (r.last_frame_ns != 0 && now - r.last_frame_ns >= kLoadGapNs && r.phase == Phase::Arrive) {
        r.saw_gap = true;
    }
    r.last_frame_ns = now;
    ++r.phase_frames;
    switch (r.phase) {
    case Phase::Title:
        if (r.phase_frames >= kTitleFrames) {
            render::set_render_config(item_config(r.items[r.index]));
            progress_line();
            enter(Phase::Settle);
        }
        break;
    case Phase::Settle:
        if (r.phase_frames >= kSettleFrames) enter(Phase::Pipelines);
        break;
    case Phase::Pipelines:
        if (render::pipeline_progress().left == 0u || r.phase_frames >= kPipelineFrames) {
            r.asks = 0u;
            return ask_travel(stage, capacity, star, current_stage);
        }
        break;
    case Phase::Arrive: {
        const std::string& target = r.items[r.index].stage.id;
        if (current_stage == target && (r.saw_gap || r.stage_at_ask != target)) {
            r.results.emplace_back();
            r.results.back().load_ms = static_cast<double>(now - r.asked_ns) / 1e6;
            enter(Phase::Skip);
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
    case Phase::Skip:
        if (r.phase_frames >= kSkipFrames) {
            Result& result = r.results.back();
            const render::RenderConfig asked = item_config(r.items[r.index]);
            result.applied = config_holds(asked);
            result.pixels = scene_pixels(asked);
            begin_window();
            enter(Phase::Measure);
        }
        break;
    case Phase::Measure: {
        Result& result = r.results.back();
        result.frames = r.phase_frames;
        sample_frame(result);
        if (r.phase_frames >= r.items[r.index].stage.frames) {
            end_window(result);
            if (++r.index == r.items.size()) {
                finish(true);
                return NEBULA_GALAXY_TRAVEL_NONE;
            }
            render::set_render_config(item_config(r.items[r.index]));
            progress_line();
            enter(Phase::Settle);
        }
        break;
    }
    case Phase::Done:
    case Phase::Failed:
        break;
    }
    return NEBULA_GALAXY_TRAVEL_NONE;
}

std::int32_t save_request(int op, std::string_view name, void* buffer, std::uint32_t size, std::uint32_t* length) noexcept {
    try {
        const std::lock_guard<std::mutex> lock(g_save_mutex);
        const auto at = g_saves.find(name);
        switch (op) {
        case NEBULA_SAVE_READ:
            if (at == g_saves.end()) return NEBULA_SAVE_NO_FILE;
            if (length != nullptr) *length = static_cast<std::uint32_t>(at->second.size());
            if (at->second.size() > size) return NEBULA_SAVE_TOO_LARGE;
            std::memcpy(buffer, at->second.data(), at->second.size());
            return NEBULA_SAVE_DONE;
        case NEBULA_SAVE_WRITE:
            g_saves[std::string(name)].assign(static_cast<const char*>(buffer), size);
            return NEBULA_SAVE_DONE;
        case NEBULA_SAVE_DELETE:
            if (at == g_saves.end()) return NEBULA_SAVE_NO_FILE;
            g_saves.erase(at);
            return NEBULA_SAVE_DONE;
        default:
            return NEBULA_SAVE_FAILED;
        }
    } catch (...) {
        return NEBULA_SAVE_FAILED;
    }
}

void at_exit() noexcept {
    if (!running()) return;
    try {
        const std::lock_guard<std::mutex> lock(g_run_mutex);
        write_result(false);
    } catch (...) {
    }
}

std::filesystem::path result_file() {
    return g_run.file;
}

bool returns_home() noexcept {
    if (!running() || !g_run.options.return_home) return false;
    try {
        // A run that ended (complete or failed); a window the player closed ends the game.
        const std::lock_guard<std::mutex> lock(g_run_mutex);
        return g_run.phase == Phase::Done || g_run.phase == Phase::Failed;
    } catch (...) {
        return false;
    }
}

bool read_recommendation(const std::filesystem::path& file, render::RenderConfig& config) {
    std::string text;
    {
        std::ifstream in(file, std::ios::binary);
        if (!in) return false;
        text.resize(1u << 20);
        in.read(text.data(), static_cast<std::streamsize>(text.size()));
        text.resize(static_cast<std::size_t>(in.gcount()));
    }
    return parse_recommendation(text, config);
}

}  // namespace galaxy::bench
