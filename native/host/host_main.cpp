// The native game's host (docs/NATIVE_PORT.md, "Module layout"): loads the game module (smg_game.dll), gives it
// Nebula's services (nebula_game_api.h) and runs it. Built three ways:
//   - nebula_native_host (native/CMakeLists.txt): headless. Records are dropped as they arrive; audio is paced but not
//     played; no remotes connect.
//   - NebulaNative (the top-level build, NEBULA_HOST_RENDERER): the records go to Nebula's D3D12 renderer.
//   - On SDL platforms (platform/sdl, NEBULA_HOST_SDL): the same with the Vulkan renderer, SDL's window, sound and
//     input; on Android SDL's activity calls SDL_main.
//
//   <host> --pak <game.pak> --saves <dir> [--module <smg_game.dll>] [--seconds N] (docs/COMMAND_LINE.md)
#if defined(_WIN32)
#include "galaxy/windows_api.h"
#include "galaxy/windows_paths.h"
#endif
#if defined(_WIN32) && defined(NEBULA_HOST_RENDERER) && !defined(NEBULA_HOST_SDL)
#include <dwmapi.h>
#endif
#include "galaxy/frame_phase.h"
#include "galaxy/render/present_clock.h"
#include <nebula_cooked_texture.h>
#include <nebula_game_api.h>

#include "galaxy/atomic_file.h"
#include "galaxy/launch_args.h"
#include "galaxy/host_extension.h"
#include "galaxy/path_text.h"
#include "galaxy/platform_paths.h"
#include "galaxy/game_step.h"
#include "galaxy/asset_store.h"
#include "galaxy/save_files.h"
#include "galaxy/crash_report.h"
#if defined(NEBULA_HOST_RENDERER) && defined(_WIN32)
#include "galaxy/hidden_start.h"
#endif
#include "galaxy/discord_presence.h"
#include "galaxy/mii_formats.h"
#include "galaxy/mii_import.h"
#include "galaxy/mods.h"
#include "galaxy/render/host_window.h"
#include "galaxy/mod_log.h"
#include "galaxy/mod_screens.h"
#include "galaxy/mod_plugins.h"
#include "galaxy/render/mod_shaders.h"
#include "galaxy/render/program_table.h"
#include "galaxy/pointer_control.h"
#if defined(NEBULA_HOST_RENDERER)
#include "galaxy/experimental_ultrawide_aspect.h"
#include "galaxy/frame_timeline.h"
#if defined(NEBULA_HOST_RENDERER) && defined(_WIN32)
#include "galaxy/controller_hid.h"
#endif
#include "galaxy/render_api.h"
#include "galaxy/native_audio.h"
#include "galaxy/game_paths.h"
#include "galaxy/native_input.h"
#include "galaxy/loss_report.h"
#if defined(NEBULA_HOST_RENDERER) && defined(_WIN32)
#include "galaxy/render/host_window_win32.h"
#endif
#include "galaxy/perf_hint.h"
#include "galaxy/power_policy.h"
#if defined(_WIN32)
#include "galaxy/windows_power.h"
#endif
#include "galaxy/platform_time.h"
#include "galaxy/input/controls_engine.h"
#include "galaxy/input/wiimote_motion.h"
#include "galaxy/achievements.h"
#include "galaxy/stats.h"
#if (defined(_WIN32) && !defined(NEBULA_HOST_SDL)) || defined(__ANDROID__)
// A real Wii Remote (wiimote_hid.h): over Windows HID, or on a DolphinBar through Android's USB host.
#define NEBULA_REAL_WIIMOTE 1
#include "galaxy/input/wiimote_input.h"
#include "galaxy/input/wiimote_motion_pointer.h"
#include "galaxy/wiimote_hid.h"
#endif
#include "galaxy/runtime_settings.h"
#include "galaxy/render/galaxy_select.h"
#include "galaxy/render/render_config.h"
#include "galaxy/render/texture_store.h"
#if defined(NEBULA_HOST_RENDERER)
#include "render/core/frame_core.h"
#endif
#include <nebula_frame_list.h>
#include "galaxy/render/rt_materials.h"
#include "galaxy/render/texture_pack.h"
#if defined(NEBULA_HOST_D3D12)
#endif

#if defined(_WIN32)
#include <crtdbg.h>
#else
#include "galaxy/virtual_keys.h"
#endif
#endif
#if !defined(NEBULA_HOST_RENDERER)
namespace galaxy::host {
// The headless host gives the platform no performance hints.
inline void perf_hint_add_current_thread() noexcept {}
inline void perf_hint_set_target(int64_t) noexcept {}
inline int64_t perf_hint_frame_done() noexcept { return 0; }
inline bool perf_hint_on_battery() noexcept { return false; }
}  // namespace galaxy::host
#endif
#if defined(NEBULA_HOST_SDL)
#include "sdl_host.h"

// The host of platform/apple gives the same services without SDL.
#if !defined(NEBULA_HOST_NATIVE_APPLE)
#include <SDL3/SDL_main.h>
#endif
#endif
#if defined(NEBULA_HOST_APPLE)
#include "apple_host.h"
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <span>
#if defined(_WIN32)
#include <io.h>
#endif
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#include <shellapi.h>
#include <intrin.h>
#include <dbghelp.h>
#include <psapi.h>
#else
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

using Clock = std::chrono::steady_clock;

// Sleeps until `when` on a high-resolution waitable timer (Windows 10 1803+; the scheduler tick otherwise), so the
// frame, the audio interface and the remotes keep their periods to well under a millisecond.
void sleep_until_precise(Clock::time_point when) {
#if defined(_WIN32)
    thread_local HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                                       TIMER_ALL_ACCESS);
    const auto now = Clock::now();
    if (when <= now) {
        return;
    }
    if (timer == nullptr) {
        std::this_thread::sleep_until(when);
        return;
    }
    LARGE_INTEGER due;
    due.QuadPart = -static_cast<LONGLONG>((std::chrono::duration_cast<std::chrono::nanoseconds>(when - now).count() + 99) / 100);
    SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
    WaitForSingleObject(timer, INFINITE);
#else
    std::this_thread::sleep_until(when);
#endif
}
const Clock::time_point kStart = Clock::now();

std::string utf8_of(const fs::path& path) {
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

void log_line(const char* tag, const char* text) {
    const double t = std::chrono::duration<double>(Clock::now() - kStart).count();
    std::fprintf(stdout, "[%9.3f] %s %s\n", t, tag, text);
    std::fflush(stdout);
    galaxy::extensions::log_line(tag, text);
}

// The start, step by step ("boot:" lines, milliseconds from the process's creation): the black screen before the
// title is their sum.
void boot_mark(const char* step) {
    double ms = std::chrono::duration<double, std::milli>(Clock::now() - kStart).count();
#if defined(_WIN32)
    FILETIME created{}, exited{}, kernel{}, user{}, now{};
    if (GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) {
        GetSystemTimePreciseAsFileTime(&now);
        const auto ticks = [](const FILETIME& f) {
            return static_cast<double>((static_cast<std::uint64_t>(f.dwHighDateTime) << 32) | f.dwLowDateTime);
        };
        ms = (ticks(now) - ticks(created)) / 1.0e4;
    }
#endif
    char text[128];
    std::snprintf(text, sizeof(text), "%s at %.0f ms", step, ms);
    log_line("boot:", text);
}

// Crash reports (galaxy/crash_report.h): why Nebula stopped, as a code with what to do. What the failing thread knew,
// kept for the report (gathered on that thread: its activity is its own).
galaxy::crash::Facts gCrashFacts;
char gCrashText[1024];
char gCrashActivityFile[512];
char gCrashModRead[640];
char gCrashPlugin[128];

// While mods' code runs: the file a crash leaves, so that the next start leaves the code off once.
std::filesystem::path gCodeCrashFile;

void gather_crash_facts(galaxy::crash::Facts::Kind kind, const char* text, bool in_game = false,
                        bool stack_overflow = false) noexcept {
#if defined(NEBULA_HOST_RENDERER) && defined(_WIN32)
    galaxy::host::stop_all_rumble();
#endif
    if (!gCodeCrashFile.empty()) {
        std::filesystem::path file;
        file.swap(gCodeCrashFile);
#if defined(_WIN32)
        if (FILE* out = _wfopen(file.c_str(), L"wb")) {
            std::fclose(out);
        }
#else
        if (FILE* out = std::fopen(file.c_str(), "wb")) {
            std::fclose(out);
        }
#endif
    }
    std::snprintf(gCrashText, sizeof(gCrashText), "%s", text != nullptr ? text : "");
    gCrashFacts.kind = kind;
    gCrashFacts.in_game = in_game;
    gCrashFacts.stack_overflow = stack_overflow;
    gCrashFacts.text = gCrashText;
    gCrashFacts.activity = galaxy::crash::current_activity(gCrashActivityFile, sizeof(gCrashActivityFile));
    gCrashFacts.activity_file = gCrashActivityFile;
    double seconds = -1.0;
    if (galaxy::crash::last_mod_read(gCrashModRead, sizeof(gCrashModRead), &seconds)) {
        gCrashFacts.last_mod_read = gCrashModRead;
        gCrashFacts.last_mod_seconds = seconds;
    }
    gCrashFacts.mods_change_game = galaxy::crash::session_mods_change_game();
}

// Crash dumps and reports: --crash-dumps, else crashes\ in Nebula's data folder for the player's executable and crashes\
// in the working directory for the headless host.
fs::path crash_folder() {
    if (!::galaxy::launch_args().crash_dumps.empty()) return ::galaxy::launch_args().crash_dumps;
#if defined(_WIN32) && defined(NEBULA_HOST_RENDERER)
    if (const fs::path data = ::galaxy::nebula_data_directory(); !data.empty()) return data / "crashes";
#endif
    return fs::path("crashes");
}

// The player sees the report; a hidden start (a test) and the headless host keep it in its file.
bool crash_dialog_wanted() {
#if defined(NEBULA_HOST_RENDERER) || defined(NEBULA_HOST_SDL)
    return !::galaxy::launch_args().started_hidden;
#else
    return false;
#endif
}

FILE* open_for_writing(const fs::path& file) {
#if defined(_WIN32)
    return _wfopen(file.c_str(), L"wb");
#else
    return std::fopen(file.c_str(), "wb");
#endif
}

#if defined(NEBULA_HOST_RENDERER)
// The unlocks and counts waiting out their write delay (galaxy::achievements 0.25 s, galaxy::stats 1.5 s) are written before a
// process ends the hard way (a crash, a fatal error). On a thread of its own and waited for at most `wait_ms`: the thread that
// failed may hold the lock of one of them, and the report must not wait on it for ever.
#if defined(_WIN32)
DWORD WINAPI flush_progress_thread(LPVOID) {
    galaxy::achievements::flush();
    galaxy::stats::flush();
    return 0;
}
#endif
void flush_progress_bounded(unsigned wait_ms) {
#if defined(_WIN32)
    if (const HANDLE thread = CreateThread(nullptr, 0, flush_progress_thread, nullptr, 0, nullptr); thread != nullptr) {
        WaitForSingleObject(thread, wait_ms);
        CloseHandle(thread);
    }
#else
    (void)wait_ms;
    galaxy::achievements::flush();
    galaxy::stats::flush();
#endif
}
#endif

// gCrashFacts as a report: its code logged, its text written beside `dump` (or as nebula-<time>.txt in the crash
// folder) and, for the player, shown by a new instance of this executable (this process may be what failed; it ends
// right after). The SDL hosts show it in a message box here.
void report_crash_facts(const fs::path& dump) {
    const galaxy::crash::Report report = galaxy::crash::classify(gCrashFacts);
    char line[160];
    std::snprintf(line, sizeof(line), "%s %s", report.code, report.title);
    log_line("crash:", line);
    char technical[1600];
    std::snprintf(technical, sizeof(technical), "%s%s%s%s%s", gCrashText,
                  gCrashFacts.last_mod_read.empty() ? "" : "\nLast file read from a mod: ", gCrashModRead,
                  dump.empty() ? "" : "\nDump: ", dump.empty() ? "" : utf8_of(dump.filename()).c_str());
    static char text[8192];
    const std::size_t length = galaxy::crash::format_report(report, technical, text, sizeof(text));
    std::error_code error;
    fs::path file = dump;
    if (file.empty()) {
        const fs::path folder = crash_folder();
        fs::create_directories(folder, error);
        const auto stamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        char name[64];
        std::strftime(name, sizeof(name), "nebula-%Y%m%d-%H%M%S.txt", std::localtime(&stamp));
        file = folder / name;
    } else {
        file.replace_extension(".txt");
    }
    if (FILE* out = open_for_writing(file)) {
        std::fwrite(text, 1, length, out);
        std::fclose(out);
        std::snprintf(line, sizeof(line), "report %s", utf8_of(file.filename()).c_str());
        log_line("crash:", line);
    }
    if (!crash_dialog_wanted()) return;
#if defined(_WIN32) && defined(NEBULA_HOST_RENDERER)
    const std::wstring exe = ::galaxy::windows::executable_path();
    std::wstring command = L"\"" + exe + L"\" --crash-report \"" + file.wstring() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!exe.empty() && CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
#elif defined(NEBULA_HOST_SDL)
    const std::string_view shown(text, length);
    const std::size_t technical_at = shown.find("\nTechnical details:");
    const std::string message(shown.substr(0, technical_at));
    galaxy::sdl::show_fatal(message.c_str());
#endif
}

[[noreturn]] void die(const char* format, ...) {
    char text[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    log_line("fatal:", text);
    std::fflush(stdout);
    gather_crash_facts(galaxy::crash::Facts::Kind::Startup, text);
    report_crash_facts({});
#if defined(NEBULA_HOST_RENDERER)
    flush_progress_bounded(1500u);
#endif
    std::fflush(stdout);
    std::_Exit(3);
}

// The module's entry points.
struct Module {
    decltype(&nebula_game_attach) attach;
    decltype(&nebula_game_run) run;
    void* module;
    decltype(&nebula_game_sound_frame) audio_tick;
    decltype(&nebula_game_mii_valid) mii_valid;
    decltype(&nebula_game_mii_changed) mii_changed;
};
Module gModule;

// Progress counters for the host's log.
std::atomic<uint32_t> gFileReads{0};
std::atomic<uint32_t> gSaveOps{0};
std::atomic<uint32_t> gSoundBlocks{0};
std::atomic<uint32_t> gSoundSubframes{0};
std::atomic<uint32_t> gSceneBegins{0};
// The game frame running (frame_begun): it numbers the frame lists the game submits.
std::atomic<uint32_t> gGameFrames{0};

void* find_symbol(void* module, const char* name) {
#if defined(_WIN32)
    void* p = reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(module), name));
#else
    void* p = dlsym(module, name);
#endif
    if (p == nullptr) {
        die("the game module has no %s", name);
    }
    return p;
}

#if defined(_WIN32)
// --diagnostics profile: the game thread (every game thread runs on it) sampled each millisecond from its start to the
// run's end (at most 10 minutes), with its callers, into the diagnostics folder's game-profile-<pid>.txt at exit: the
// modules loaded ("module <base> <size> <name>"), then a line a sample (return addresses, the innermost first).
// tools/perf/game_profile.py names them from the game module's link map.
namespace game_profile {

constexpr int kDepth = 24;
using Frames = std::array<DWORD64, kDepth>;

// The suspended thread's stack from `context`, without allocating (the thread may hold the heap's lock).
int walk(CONTEXT context, Frames& frames) noexcept {
    int depth = 0;
    __try {
        while (depth < kDepth && context.Rip != 0) {
            frames[depth++] = context.Rip;
            DWORD64 image = 0;
            PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(context.Rip, &image, nullptr);
            if (function == nullptr) {
                context.Rip = *reinterpret_cast<const DWORD64*>(context.Rsp);
                context.Rsp += 8;
            } else {
                void* handler = nullptr;
                DWORD64 establisher = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, context.Rip, function, &context, &handler, &establisher,
                                 nullptr);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    return depth;
}

constexpr std::size_t kMostSamples = 600'000u;  // 10 minutes, 115 MB
std::thread g_sampler;
std::atomic<bool> g_stop{false};

void run(HANDLE thread) {
    std::vector<std::pair<int, Frames>> samples;
    samples.reserve(60'000u);
    auto next = Clock::now();
    // Samples are added after the thread resumes: a vector's growth never waits on a heap lock the thread holds.
    while (!g_stop.load(std::memory_order_relaxed) && samples.size() < kMostSamples) {
        if (SuspendThread(thread) != static_cast<DWORD>(-1)) {
            CONTEXT context{};
            context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
            Frames frames{};
            const int depth = GetThreadContext(thread, &context) ? walk(context, frames) : 0;
            ResumeThread(thread);
            if (depth > 0) samples.emplace_back(depth, frames);
        }
        next += std::chrono::milliseconds(1);
        sleep_until_precise(next);
    }
    const fs::path folder = galaxy::nebula_diagnostics_directory();
    const fs::path name = folder / ("game-profile-" + std::to_string(galaxy::current_process_id()) + ".txt");
    std::FILE* file = folder.empty() ? nullptr : _wfopen(name.c_str(), L"wb");
    if (file == nullptr) return;
    std::array<HMODULE, 512> modules{};
    DWORD needed = 0;
    if (K32EnumProcessModules(GetCurrentProcess(), modules.data(), static_cast<DWORD>(sizeof(modules)), &needed)) {
        for (DWORD i = 0; i < needed / sizeof(HMODULE) && i < modules.size(); ++i) {
            MODULEINFO info{};
            char name[MAX_PATH] = {};
            if (K32GetModuleInformation(GetCurrentProcess(), modules[i], &info, sizeof(info)) &&
                K32GetModuleBaseNameA(GetCurrentProcess(), modules[i], name, MAX_PATH) != 0) {
                std::fprintf(file, "module %llx %lx %s\n", static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(info.lpBaseOfDll)),
                             static_cast<unsigned long>(info.SizeOfImage), name);
            }
        }
    }
    for (const auto& [depth, frames] : samples) {
        std::fputs("sample", file);
        for (int i = 0; i < depth; ++i) std::fprintf(file, " %llx", static_cast<unsigned long long>(frames[i]));
        std::fputc('\n', file);
    }
    std::fclose(file);
    char text[96];
    std::snprintf(text, sizeof(text), "%zu samples in game-profile-%lu.txt", samples.size(), galaxy::current_process_id());
    log_line("profile:", text);
}

void start(DWORD thread_id) {
    const HANDLE sampled = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, thread_id);
    if (sampled == nullptr) {
        return;
    }
    g_sampler = std::thread([sampled] {
        run(sampled);
        CloseHandle(sampled);
    });
}

// At exit: the samples so far, written.
void finish() {
    if (g_sampler.joinable()) {
        g_stop.store(true, std::memory_order_relaxed);
        g_sampler.join();
    }
}

}  // namespace game_profile
#endif

// The mods' code, started once the game is attached.
std::vector<galaxy::mods::PluginFile> gPlugins;
galaxy::mods::ModRows gModRows;
bool gModsChangeGame = false;
// The mods on this session (the scan's enabled ones): the mod log is made only when there are any.
std::uint32_t gModsOn = 0;
// The placed packages of this start, for the start's check (modlog::check_packages).
std::vector<galaxy::modlog::StartPackage> gStartPackages;

void load_module(const fs::path& path) {
#if defined(_WIN32)
    void* module = LoadLibraryW(path.wstring().c_str());
#else
    // Global: find_function (a native plugin's hook) finds the game's functions here.
    void* module = dlopen(path.string().c_str(), RTLD_NOW | RTLD_GLOBAL);
#endif
    if (module == nullptr) {
        die("cannot load the game module %s", utf8_of(path.filename()).c_str());
    }
    gModule.attach = reinterpret_cast<decltype(gModule.attach)>(find_symbol(module, "nebula_game_attach"));
    gModule.run = reinterpret_cast<decltype(gModule.run)>(find_symbol(module, "nebula_game_run"));
    gModule.module = module;
    gModule.audio_tick = reinterpret_cast<decltype(gModule.audio_tick)>(find_symbol(module, "nebula_game_sound_frame"));
    gModule.mii_valid = reinterpret_cast<decltype(gModule.mii_valid)>(find_symbol(module, "nebula_game_mii_valid"));
    gModule.mii_changed =
        reinterpret_cast<decltype(gModule.mii_changed)>(find_symbol(module, "nebula_game_mii_changed"));
}

#if defined(NEBULA_HOST_RENDERER)


#endif

// The game: Setup's game.pak, the asset store (galaxy/asset_store.h).
struct Pak {
    char id[7] = {};  // the game id
    galaxy::assets::AssetStore store;
    fs::path store_file;
};
Pak gPak;

bool open_pak(const fs::path& path) {
    gPak.store_file = path;
    if (!gPak.store.open(path)) {
        log_line("import:", "The game files need importing again. Open Nebula Setup and import the game.");
#if defined(NEBULA_HOST_RENDERER) && defined(_WIN32)
        if (!::galaxy::launch_args().started_hidden) {
            MessageBoxW(nullptr, L"Open Nebula Setup and import the game again.", L"Nebula", MB_OK | MB_ICONINFORMATION);
        }
#endif
        return false;
    }
    // The releases the module plays (NebulaRegion.hpp): North America, Europe and Japan.
    std::snprintf(gPak.id, sizeof(gPak.id), "%.6s", std::string(gPak.store.game_id()).c_str());
    if (std::strcmp(gPak.id, "RMGE01") != 0 && std::strcmp(gPak.id, "RMGP01") != 0 &&
        std::strcmp(gPak.id, "RMGJ01") != 0) {
        die("%s holds %.6s, not Super Mario Galaxy for North America, Europe or Japan", utf8_of(path.filename()).c_str(), gPak.id);
    }
    return true;
}

// The calling thread's name, as profilers, debuggers and the CPU report show it.
void name_thread(const wchar_t* name) {
#if defined(_WIN32)
    SetThreadDescription(GetCurrentThread(), name);
#else
    char narrow[16];
    std::snprintf(narrow, sizeof(narrow), "%ls", name);
#if defined(__APPLE__)
    pthread_setname_np(narrow);
#else
    pthread_setname_np(pthread_self(), narrow);
#endif
#endif
}

// A worker thread taking jobs in order.
class Worker {
public:
    explicit Worker(const wchar_t* name) : name_(name) {}

    template <typename F>
    void post(F&& job) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            jobs_.emplace_back(std::forward<F>(job));
        }
        ready_.notify_one();
    }

    void start() {
        thread_ = std::thread([this] {
            name_thread(name_);
            for (;;) {
                std::function<void()> job;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    ready_.wait(lock, [this] { return !jobs_.empty(); });
                    job = std::move(jobs_.front());
                    jobs_.pop_front();
                }
                job();
            }
        });
        thread_.detach();
    }

private:
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<std::function<void()>> jobs_;
    std::thread thread_;
    const wchar_t* name_;
};

Worker gFs(L"Nebula Host Saves");

// The save folder's root: the game's paths under it, each checked.
fs::path gSaveRoot;

// The game's save folder under it (its four-character code); gOldSaveDataPath is where earlier versions kept the
// save (title/00010000/<the code in hex>/data), read once; a mod's clone copies it too).
std::string gSaveDataPath;
std::string gOldSaveDataPath;
// A mod's own save folder (Riivolution's savegame): the game's save files go there instead.
fs::path gModSaveRoot;

bool save_folder_path(const char* path, fs::path* out) {
    if (path == nullptr || path[0] != '/') {
        return false;
    }
    fs::path result = !gModSaveRoot.empty() && !gSaveDataPath.empty() && (std::string_view(path).starts_with(gSaveDataPath) || std::string_view(path).starts_with(gOldSaveDataPath))
                          ? gModSaveRoot
                          : gSaveRoot;
    for (const fs::path& part : fs::path(path + 1)) {
        // A part with a drive or a root would replace the folder instead of joining it.
        if (part == ".." || part == "." || part.has_root_name() || part.has_root_directory()) {
            return false;
        }
        result /= part;
    }
    *out = result;
    return true;
}

// The sound clock: the game starts it with its frame's length (sound ticks a second); the lock guards the running
// flag and the rate, and the clock thread waits on the condition variable until both are set.
std::mutex gSoundClockLock;
std::condition_variable gSoundClockWake;
uint32_t gSoundTicksPerSecond = 0;
bool gSoundClockRunning = false;

#if defined(NEBULA_HOST_RENDERER)
extern galaxy::host::NativeGameAudio* gAudio;
#endif

// The app out of sight (SDL hosts: Android's background): the display and audio clocks stop, so the game waits at
// its next frame; they restart from its return, with nothing to catch up.
std::mutex gSuspendLock;
std::condition_variable gSuspendWake;
bool gSuspended = false;  // guarded by gSuspendLock

// The window lost focus and the player chose PAUSE (runtime settings, focus_loss): set when it is lost, taken by the game
// once it can open its pause menu (svc_take_pause_request), dropped when the window is in front again unless it was minimised
// (the game is held then and takes it at its first frame after the return).
std::atomic<bool> gPauseRequest{false};

// When the player last gave the game an input (a button, a stick pushed, the pointer moved) and when the game last played a
// movie's sound, in steady-clock milliseconds (power_policy.h): the host's main loop tells the OS whether anyone is playing.
// 0 until the first; the start counts as an input.
#if defined(NEBULA_HOST_RENDERER)
std::atomic<int64_t> gLastInputMs{0};
std::atomic<int64_t> gLastMovieMs{-1};

int64_t steady_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
#endif

// The start runs ahead: the game shows nothing until its first 3D scene (the title's, or a test's galaxy), and its
// loads advance a step each frame while it takes well under a millisecond a frame, so until then frames come every
// kBootPeriod and one frame a display period is presented. The first 3D scene, or kBootFrames frames, end it.
constexpr auto kBootPeriod = std::chrono::microseconds(2000);
constexpr uint32_t kBootFrames = 600;
std::atomic<bool> gBootFast{true};

void end_boot_fast(const char* why) {
    if (gBootFast.exchange(false, std::memory_order_relaxed)) {
        boot_mark(why);
#if defined(NEBULA_HOST_RENDERER)
        galaxy::render::set_frame_end_presents(true);
#endif
    }
}

// An extension's fixed steps (NEBULA_EXT_FIXED_STEPS): the game's time is its frame count. The host's frame and audio
// clocks do not run; the game takes each frame when it has nothing to run (test_frame_step), disc reads complete inside
// their call, and the steady clock counts frames. Two runs of one build then match frame for frame.
bool deterministic_mode() {
    return (galaxy::extensions::flags() & NEBULA_EXT_FIXED_STEPS) != 0u;
}
std::atomic<uint64_t> gDeterministicFrames{0};

// True when it waited.
bool wait_while_suspended() {
    std::unique_lock<std::mutex> lock(gSuspendLock);
    if (!gSuspended) {
        return false;
    }
    gSuspendWake.wait(lock, [] { return !gSuspended; });
    return true;
}

#if !defined(NEBULA_HOST_RENDERER)
// Headless, nothing is mixed: the game's sound logic runs on the clock.
void sound_clock_thread() {
    if (deterministic_mode()) {
        return;  // the audio ticks come with each frame (svc_test_frame_step)
    }
    name_thread(L"Nebula Host Audio");
    auto next = std::chrono::steady_clock::now();
    for (;;) {
        uint32_t ticks_per_second = 0;
        {
            std::unique_lock<std::mutex> lock(gSoundClockLock);
            gSoundClockWake.wait(lock, [] { return gSoundClockRunning && gSoundTicksPerSecond != 0; });
            ticks_per_second = gSoundTicksPerSecond;
        }
        gSoundBlocks.fetch_add(1, std::memory_order_relaxed);
        gModule.audio_tick();
        next += std::chrono::nanoseconds(static_cast<int64_t>(1e9 / ticks_per_second));
        std::this_thread::sleep_until(next);
    }
}
#endif

// The device asks for the game's next frame of sound, on its thread.
void audio_frame_tick() {
    gSoundBlocks.fetch_add(1, std::memory_order_relaxed);
    gModule.audio_tick();
}

#if defined(_WIN32) && defined(NEBULA_HOST_RENDERER) && !defined(NEBULA_HOST_SDL)
// The display's refreshes by the compositor's clock, looked at twice a second.
std::optional<galaxy::host::DisplayRefresh> display_refresh() {
    static std::optional<galaxy::host::DisplayRefresh> known;
    static int64_t earlier_vblank_ns = 0;
    static uint32_t frames = 0;
    if (frames++ % 30u != 0u) return known;
    known.reset();
    DWM_TIMING_INFO timing{};
    timing.cbSize = sizeof(timing);
    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    if (SUCCEEDED(DwmGetCompositionTimingInfo(nullptr, &timing)) && timing.qpcRefreshPeriod != 0u) {
        const auto to_ns = [&](uint64_t qpc) {
            const uint64_t whole = qpc / static_cast<uint64_t>(frequency.QuadPart);
            const uint64_t part = qpc % static_cast<uint64_t>(frequency.QuadPart);
            return static_cast<int64_t>(whole * 1000000000ull + part * 1000000000ull / static_cast<uint64_t>(frequency.QuadPart));
        };
        const galaxy::host::DisplayRefresh now{to_ns(timing.qpcVBlank), to_ns(timing.qpcRefreshPeriod)};
        if (earlier_vblank_ns != 0 && galaxy::host::refreshes_in_step(earlier_vblank_ns, now)) known = now;
        earlier_vblank_ns = now.vblank_ns;
    }
    return known;
}

// The next frame's start after the one due at `start` (frame_phase.h).
Clock::time_point next_frame_start(Clock::time_point start, std::chrono::nanoseconds period) {
    static galaxy::host::FramePhase phase;
    static int64_t previous_ns = 0;
    const int64_t start_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(start.time_since_epoch()).count();
    const int64_t present_ns = galaxy::render::g_last_present_ns.load(std::memory_order_relaxed);
    const int64_t work_ns = previous_ns != 0 && present_ns > previous_ns ? present_ns - previous_ns : 0;
    previous_ns = start_ns;
    const int64_t next_ns = phase.next_start(start_ns, work_ns, period.count(), display_refresh());
    return start + std::chrono::nanoseconds(next_ns - start_ns);
}

// Tells the OS about the player's presence (power_policy.h) when it changes: the process asks for efficiency (EcoQoS) while the
// window is behind another, minimised, or left alone for a minute with no cutscene; the display is held on only while the window
// is in front and someone plays or watches. Called from the host's main loop, which is also the thread that holds the display.
void apply_power_wish(bool background, bool minimised) {
    static galaxy::host::PowerWish applied;  // the OS's state at the start: full speed, the display's own timers
    static const int64_t started_ms = steady_ms();  // the start counts as an input
    const int64_t now_ms = steady_ms();
    const galaxy::host::PowerFacts facts{now_ms, std::max(gLastInputMs.load(std::memory_order_relaxed), started_ms),
                                         gLastMovieMs.load(std::memory_order_relaxed), background, minimised};
    const galaxy::host::PowerWish wish = galaxy::host::power_wish(facts);
    if (wish == applied) {
        return;
    }
    if (wish.efficient != applied.efficient) {
        galaxy::host::set_process_efficiency(wish.efficient);
        log_line("[power]", wish.efficient ? "efficiency requested (away)" : "full speed (in front)");
    }
    if (wish.display_required != applied.display_required) {
        galaxy::host::keep_display_on(wish.display_required);
        log_line("[power]", wish.display_required ? "display held on (playing)" : "display left to the OS's timers");
    }
    applied = wish;
}
#endif

// The display's frame at 60 Hz: 59.94 would repeat a frame every 17 s on a 60 Hz display. A late
// wake-up skips the fields it missed, as a display does, rather than delivering them back to back. On Android the
// frame follows the display's own vsync (every second one at 120 Hz), so frames never drift against it and each
// stays on screen for the same time; the host's clock takes over where the display gives none.
void svc_frame_wait(void*) {
    const auto display_period = std::chrono::nanoseconds(galaxy::kGameStepNs);
    static auto period = gBootFast.load(std::memory_order_relaxed) ? std::chrono::nanoseconds(kBootPeriod) : display_period;
    static auto next = Clock::now() + period;
#if defined(NEBULA_HOST_SDL)
    static int64_t vsync_ns = 0;
    static uint32_t on_vsync = 0;
    static uint32_t frames = 0;
    static int64_t work_sum = 0;
    static int64_t work_max = 0;
#endif
    for (;;) {
#if defined(NEBULA_HOST_SDL)
        const bool vsync = galaxy::sdl::wait_display_frame(period.count(), vsync_ns);
        if (!vsync) {
            sleep_until_precise(next);
        }
#else
        sleep_until_precise(next);
#endif
        if (wait_while_suspended()) {
            next = Clock::now() + period;
            continue;
        }
        // The last frame's work (the game and render threads' CPU time), to the performance hints (Android).
        [[maybe_unused]] const int64_t work = galaxy::host::perf_hint_frame_done();
        if (period != display_period && !gBootFast.load(std::memory_order_relaxed)) {
            period = display_period;
        }
        // The hints' budget is the frame the clock keeps now (the short boot frame, then the game step).
        galaxy::host::perf_hint_set_target(period.count());
#if defined(_WIN32) && defined(NEBULA_HOST_RENDERER) && !defined(NEBULA_HOST_SDL)
        next = next_frame_start(next, period);
#else
        next += period;
#endif
        const auto now = Clock::now();
        if (now >= next) {
            next = now + period;
        }
#if defined(NEBULA_HOST_SDL)
        if (vsync) {
            next = now + period;  // the host's clock continues from the display's
            ++on_vsync;
        }
        // Every 10 s: how the frames were paced, and the frame work (the busier of the game and render threads).
        work_sum += work;
        work_max = std::max(work_max, work);
        if (++frames == 600u) {
            std::fprintf(stderr, "[pacing] %u frames, %u on the display's vsync (%.1f Hz); frame work %.2f ms average, "
                         "%.2f ms longest\n", frames, on_vsync, galaxy::sdl::display_refresh_hz(),
                         static_cast<double>(work_sum) / frames / 1e6, static_cast<double>(work_max) / 1e6);
            frames = on_vsync = 0u;
            work_sum = work_max = 0;
        }
#endif
        return;
    }
}

// Deterministic mode's frame: the game has nothing to run. The audio ticks of one frame's length (the audio clock's
// period, without the device's correction) and the clock.
void svc_test_frame_step(void*) {
    static double due_ns = 0.0;
    uint32_t ticks_per_second = 0;
    {
        std::lock_guard<std::mutex> lock(gSoundClockLock);
        ticks_per_second = gSoundClockRunning ? gSoundTicksPerSecond : 0u;
    }
    if (ticks_per_second != 0u) {
        due_ns += 1e9 / galaxy::kGameStepRate;
        const double period_ns = 1e9 / ticks_per_second;
        while (due_ns >= period_ns) {
            due_ns -= period_ns;
            gSoundBlocks.fetch_add(1, std::memory_order_relaxed);
            gModule.audio_tick();
        }
    } else {
        due_ns = 0.0;
    }
    gDeterministicFrames.fetch_add(1, std::memory_order_relaxed);
}

void svc_log(void*, const char* text) {
    log_line("game:", text);
}

#if defined(_WIN32)
// --diagnostics thread-cpu: each thread's CPU time and context switches over the last second, by name (or,
// unnamed, the module and offset it started at), busiest first; threads under 0.5 ms/s are counted in the total only.
// The report's own cost (a snapshot of every thread in the system) is left out of the reporting thread's figure.
void report_thread_cpu(double tsc_hz) {
    static std::unordered_map<DWORD, uint64_t> last;
    static std::unordered_map<DWORD, uint32_t> last_switches;
    static uint64_t report_cycles = 0;
    ULONG64 report_start = 0;
    QueryThreadCycleTime(GetCurrentThread(), &report_start);
    // The process's threads and their context switches: NtQuerySystemInformation(SystemProcessInformation). On x64
    // each process record is 0x100 bytes followed by 0x50-byte thread records (thread id at +0x30, switches at +0x40).
    using SystemQueryFn = LONG(WINAPI*)(int, void*, ULONG, ULONG*);
    static const auto system_query = reinterpret_cast<SystemQueryFn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation")));
    static std::vector<uint8_t> buffer(4u << 20);
    struct ThreadSample {
        DWORD id;
        uint32_t switches;
    };
    std::vector<ThreadSample> threads;
    ULONG needed = 0;
    LONG status = system_query != nullptr ? system_query(5, buffer.data(), (ULONG)buffer.size(), &needed) : -1;
    if (status == static_cast<LONG>((3u << 30) | 4u) /* STATUS_INFO_LENGTH_MISMATCH */ && needed != 0) {
        buffer.resize(needed + (1u << 20));
        status = system_query(5, buffer.data(), (ULONG)buffer.size(), &needed);
    }
    if (status != 0) {
        return;
    }
    const DWORD pid = GetCurrentProcessId();
    for (size_t offset = 0;;) {
        const uint8_t* process = buffer.data() + offset;
        uint32_t next = 0, count = 0;
        uintptr_t process_id = 0;
        std::memcpy(&next, process, 4);
        std::memcpy(&count, process + 4, 4);
        std::memcpy(&process_id, process + 0x50, sizeof(process_id));
        if (process_id == pid) {
            for (uint32_t i = 0; i < count; ++i) {
                const uint8_t* thread = process + 0x100 + i * 0x50;
                uintptr_t thread_id = 0;
                uint32_t switches = 0;
                std::memcpy(&thread_id, thread + 0x30, sizeof(thread_id));
                std::memcpy(&switches, thread + 0x40, 4);
                threads.push_back({(DWORD)thread_id, switches});
            }
            break;
        }
        if (next == 0) {
            break;
        }
        offset += next;
    }
    using QueryFn = LONG(WINAPI*)(HANDLE, int, void*, ULONG, ULONG*);
    static const auto query = reinterpret_cast<QueryFn>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQueryInformationThread")));
    struct Row {
        double ms;
        std::string name;
        uint32_t switches;
    };
    std::vector<Row> rows;
    double total = 0.0;
    const DWORD self = GetCurrentThreadId();
    for (const ThreadSample& sample : threads) {
        const DWORD thread_id = sample.id;
        const HANDLE thread = OpenThread(THREAD_QUERY_INFORMATION, FALSE, thread_id);
        if (thread == nullptr) {
            continue;
        }
        ULONG64 cycles = 0;
        QueryThreadCycleTime(thread, &cycles);
        if (thread_id == self) {
            cycles = report_start - report_cycles;
        }
        std::string name;
        PWSTR description = nullptr;
        if (SUCCEEDED(GetThreadDescription(thread, &description)) && description != nullptr && description[0] != 0) {
            // A description longer than the buffer converts to nothing: the thread then shows by its start address.
            char narrow[64] = {};
            if (WideCharToMultiByte(CP_UTF8, 0, description, -1, narrow, sizeof(narrow), nullptr, nullptr) > 0) {
                name = narrow;
            }
        }
        LocalFree(description);
        void* start = nullptr;
        HMODULE owner = nullptr;
        if (name.empty()) {
            // ThreadQuerySetWin32StartAddress (9).
            char where[160] = "?";
            if (query != nullptr && query(thread, 9, &start, sizeof(start), nullptr) == 0 &&
                GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   static_cast<LPCWSTR>(start), &owner)) {
                char path[MAX_PATH];
                const DWORD length = GetModuleFileNameA(owner, path, sizeof(path));
                const char* file = length != 0 ? std::strrchr(path, '\\') : nullptr;
                std::snprintf(where, sizeof(where), "%s+0x%llx", file != nullptr ? file + 1 : "?",
                              (unsigned long long)(static_cast<char*>(start) - reinterpret_cast<char*>(owner)));
            }
            name = "tid " + std::to_string(thread_id) + " " + where;
        }
        CloseHandle(thread);
        uint64_t& before = last[thread_id];
        const double ms = (double)(cycles - before) * 1000.0 / tsc_hz;
        before = cycles;
        total += ms;
        uint32_t& switches_before = last_switches[thread_id];
        const uint32_t switches = sample.switches - switches_before;
        switches_before = sample.switches;
        if (ms >= 0.5) {
            rows.push_back({ms, std::move(name), switches});
        }
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.ms > b.ms; });
    std::string line = "total " + std::to_string((int)(total + 0.5)) + " ms/s";
    for (const Row& row : rows) {
        char item[160];
        std::snprintf(item, sizeof(item), "; %s %.1f (%u sw)", row.name.c_str(), row.ms, row.switches);
        line += item;
    }
    log_line("cpu:", line.c_str());
    ULONG64 report_end = 0;
    QueryThreadCycleTime(GetCurrentThread(), &report_end);
    report_cycles += report_end - report_start;
}
#endif

std::string gSceneStage;

#if defined(_WIN32)
// Crash reports. A fatal exception in the game module or the host, or the game's own fatal call, writes a minidump and
// the report (report_crash_facts) to the crash folder from a thread kept for it (the failing thread's stack may be what
// failed), logs where it happened and the game's threads, and ends the process.
using MiniDumpWriteDumpFn = decltype(&MiniDumpWriteDump);  // loaded at the crash: dbghelp.dll is not linked

HANDLE gCrashRequest;
HANDLE gCrashDone;
EXCEPTION_POINTERS* gCrashInfo;
DWORD gCrashThread;
std::atomic<int> gCrashing{0};

void crash_writer() {
    for (;;) {
        WaitForSingleObject(gCrashRequest, INFINITE);
        const fs::path folder = crash_folder();
        std::error_code ignored;
        fs::create_directories(folder, ignored);
        SYSTEMTIME now;
        GetLocalTime(&now);
        // The file name is fixed width (a date, a time and a process id); the folder is any length.
        wchar_t dump_name[64];
        std::swprintf(dump_name, sizeof(dump_name) / sizeof(dump_name[0]), L"nebula-native-%04u%02u%02u-%02u%02u%02u-%lu.dmp", now.wYear,
                      now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, GetCurrentProcessId());
        const std::wstring path = (folder / dump_name).wstring();
        const HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll");
        const auto write = dbghelp != nullptr
                               ? reinterpret_cast<MiniDumpWriteDumpFn>(GetProcAddress(dbghelp, "MiniDumpWriteDump"))
                               : nullptr;
        const HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (write != nullptr && file != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION exception{gCrashThread, gCrashInfo, FALSE};
            // Stacks and what they point at (the module's data segments would add its half-gigabyte image window).
            const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo);
            const BOOL ok = write(GetCurrentProcess(), GetCurrentProcessId(), file, type,
                                  gCrashInfo != nullptr ? &exception : nullptr, nullptr, nullptr);
            char text[MAX_PATH + 32];
            std::snprintf(text, sizeof(text), "%s %ls", ok ? "minidump" : "minidump failed:", fs::path(path).filename().c_str());
            log_line("crash:", text);
        }
        if (file != INVALID_HANDLE_VALUE) {
            CloseHandle(file);
        }
        report_crash_facts(fs::path(path));
#if defined(NEBULA_HOST_RENDERER)
        flush_progress_bounded(1500u);
#endif
        SetEvent(gCrashDone);
        // The dump and report are written: whatever the crashed thread still does (a flush behind a lock another thread
        // held when it stopped) never keeps the process alive.
        Sleep(5000);
        TerminateProcess(GetCurrentProcess(), 4);
    }
}

void report_crash(EXCEPTION_POINTERS* info) {
    if (gCrashRequest == nullptr) {
        return;
    }
    gCrashInfo = info;
    gCrashThread = GetCurrentThreadId();
    SetEvent(gCrashRequest);
    WaitForSingleObject(gCrashDone, 60000);
    std::fflush(stdout);
}

// The mod whose plugin's code was running at a fault: the fault's own place, else the nearest plugin frame the stack
// unwinds to from it (the game's code a plugin's call reached). Null: none, or the stack can't be read.
__declspec(noinline) const char* plugin_in_fault(const EXCEPTION_POINTERS* info, bool* in_plugin) noexcept {
    if (const char* mod = galaxy::mods::plugin_at(info->ExceptionRecord->ExceptionAddress)) {
        *in_plugin = true;
        return mod;
    }
    *in_plugin = false;
    if (info->ExceptionRecord->ExceptionCode == EXCEPTION_STACK_OVERFLOW) {
        return nullptr;
    }
    const char* found = nullptr;
    __try {
        CONTEXT context = *info->ContextRecord;
#if defined(_M_ARM64)
        DWORD64& pc = context.Pc;
#else
        DWORD64& pc = context.Rip;
#endif
        for (int frame = 0; frame < 64 && pc != 0 && found == nullptr; ++frame) {
            DWORD64 image = 0;
            const PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(pc, &image, nullptr);
            if (function == nullptr) {
                // A leaf function: its return address is where the stack (or the link register) holds it.
#if defined(_M_ARM64)
                pc = context.Lr;
#else
                pc = *reinterpret_cast<const DWORD64*>(context.Rsp);
                context.Rsp += 8;
#endif
            } else {
                void* handler_data = nullptr;
                DWORD64 establisher = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, image, pc, function, &context, &handler_data, &establisher, nullptr);
            }
            found = galaxy::mods::plugin_at(reinterpret_cast<const void*>(pc));
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        found = nullptr;
    }
    return found;
}

// The crash's mod, when a plugin's code was running (gCrashFacts after gather_crash_facts).
void note_crash_plugin(const EXCEPTION_POINTERS* info) noexcept {
    bool in_plugin = false;
    if (const char* mod = plugin_in_fault(info, &in_plugin)) {
        std::snprintf(gCrashPlugin, sizeof(gCrashPlugin), "%s", mod);
        gCrashFacts.plugin_mod = gCrashPlugin;
        gCrashFacts.in_plugin = in_plugin;
        char line[192];
        std::snprintf(line, sizeof(line), in_plugin ? "in the code of the mod %s" : "in a call the code of the mod %s made", gCrashPlugin);
        log_line("crash:", line);
    }
}

// The report, out of line: the filter below runs for every exception in the process and keeps its frame small.
[[noreturn]] __declspec(noinline) void report_fatal_exception(EXCEPTION_POINTERS* info, HMODULE owner) {
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    const void* address = info->ExceptionRecord->ExceptionAddress;
    wchar_t name[MAX_PATH] = {};
    GetModuleFileNameW(owner, name, MAX_PATH);
    char text[MAX_PATH + 160];
    std::snprintf(text, sizeof(text), "exception 0x%08lX at %ls+0x%llx (thread %lu), address 0x%llx", code,
                  fs::path(name).filename().c_str(),
                  static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(address) - reinterpret_cast<uintptr_t>(owner)),
                  GetCurrentThreadId(),
                  static_cast<unsigned long long>(info->ExceptionRecord->NumberParameters >= 2
                                                      ? info->ExceptionRecord->ExceptionInformation[1]
                                                      : 0));
    log_line("crash:", text);
    log_line("crash: stage", gSceneStage.c_str());
    gather_crash_facts(galaxy::crash::Facts::Kind::Fault, text, owner == GetModuleHandleW(L"smg_game.dll"),
                       code == EXCEPTION_STACK_OVERFLOW);
    note_crash_plugin(info);
    report_crash(info);
    TerminateProcess(GetCurrentProcess(), 4);
    std::_Exit(4);
}

// Set while describe_cpp_exception reads a record: a fault there belongs to its own __except, which the vectored
// handler would otherwise see first and report as the crash.
thread_local bool tDescribingException = false;

// A C++ exception's record (MSVC's 0xE06D7363: the object, its ThrowInfo, the image base its offsets start from):
// the thrown type and, for a std::exception, what(). A rethrow (`throw;`) has neither object nor ThrowInfo. Read
// under SEH: the record comes from a failing process.
void describe_cpp_exception(const EXCEPTION_RECORD* record, char* out, std::size_t size) {
    std::snprintf(out, size, "uncaught C++ exception");
    if (record->NumberParameters < 4u) {
        return;
    }
    if (record->ExceptionInformation[1] == 0u || record->ExceptionInformation[2] == 0u) {
        std::snprintf(out, size, "uncaught C++ exception (rethrown)");
        return;
    }
    tDescribingException = true;
    __try {
        const auto base = static_cast<uintptr_t>(record->ExceptionInformation[3]);
        const auto* object = reinterpret_cast<const char*>(record->ExceptionInformation[1]);
        // ThrowInfo: attributes, unwind, forward compat, catchable types (32-bit offsets from the image base).
        const auto* throw_info = reinterpret_cast<const int32_t*>(record->ExceptionInformation[2]);
        const auto* types = reinterpret_cast<const int32_t*>(base + static_cast<uint32_t>(throw_info[3]));
        const char* thrown = nullptr;
        const char* what = nullptr;
        for (int32_t i = 0; i < types[0] && i < 16; ++i) {
            // CatchableType: properties, type descriptor, this displacement (mdisp, pdisp, vdisp), size, copy.
            const auto* type = reinterpret_cast<const int32_t*>(base + static_cast<uint32_t>(types[1 + i]));
            const char* name = reinterpret_cast<const char*>(base + static_cast<uint32_t>(type[1])) + 16;
            if (thrown == nullptr) {
                thrown = name;
            }
            if (std::strcmp(name, ".?AVexception@std@@") == 0) {
                what = reinterpret_cast<const std::exception*>(object + type[2])->what();
            }
        }
        std::snprintf(out, size, "uncaught C++ exception %s: %s", thrown != nullptr ? thrown : "?",
                      what != nullptr ? what : "(not a std::exception)");
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
    tDescribingException = false;
}

#if defined(__clang__)
__attribute__((no_sanitize("address", "undefined")))
#endif
LONG CALLBACK crash_handler(EXCEPTION_POINTERS* info) {
    if (tDescribingException) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    // A C++ exception, logged where it is thrown (the first 32): one thrown on the game's own stacks cannot unwind
    // through them and ends the process before any filter runs. Handled or not, the line says what was thrown.
    if (code == 0xE06D7363u) {
        static std::atomic<unsigned> logged{0u};
        if (logged.fetch_add(1u, std::memory_order_relaxed) < 32u) {
            char text[512];
            describe_cpp_exception(info->ExceptionRecord, text, sizeof(text));
            char line[640];
            std::snprintf(line, sizeof(line), "thrown: %s (thread %lu)", text + std::strlen("uncaught "),
                          GetCurrentThreadId());
            log_line("exception:", line);
            void* frames[16];
            const USHORT count = RtlCaptureStackBackTrace(1, 16, frames, nullptr);
            const auto exe = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
            for (USHORT f = 0; f < count; ++f) {
                const auto at = reinterpret_cast<std::uintptr_t>(frames[f]);
                std::snprintf(line, sizeof(line), "NebulaNative.exe+0x%llx", static_cast<unsigned long long>(at - exe));
                log_line("thrown from:", line);
            }
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_ILLEGAL_INSTRUCTION && code != EXCEPTION_PRIV_INSTRUCTION &&
        code != EXCEPTION_STACK_OVERFLOW && code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_INT_OVERFLOW &&
        code != EXCEPTION_IN_PAGE_ERROR) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    // Only faults in the game module or this executable: drivers and system libraries handle their own.
    HMODULE owner = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCWSTR>(info->ExceptionRecord->ExceptionAddress), &owner) ||
        (owner != GetModuleHandleW(nullptr) && owner != GetModuleHandleW(L"smg_game.dll") &&
         galaxy::mods::plugin_at(info->ExceptionRecord->ExceptionAddress) == nullptr)) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (gCrashing.exchange(1) != 0) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    report_fatal_exception(info, owner);
}

// abort(), which std::terminate calls for an uncaught C++ exception: no exception filter sees it. Logs what was
// thrown, when that is why, and writes the dump from the aborting thread's context.
void abort_handler(int) {
    const char* text = "abort";
    std::string what;
    if (const std::exception_ptr active = std::current_exception()) {
        try {
            std::rethrow_exception(active);
        } catch (const std::exception& error) {
            what = std::string("uncaught exception: ") + error.what();
            text = what.c_str();
        } catch (...) {
            text = "uncaught exception of unknown type";
        }
    }
    log_line("crash:", text);
    if (gCrashing.exchange(1) == 0) {
        gather_crash_facts(galaxy::crash::Facts::Kind::Abort, text);
        CONTEXT context{};
        RtlCaptureContext(&context);
        EXCEPTION_RECORD record{};
        record.ExceptionCode = 0xE0000003u;  // not a system code: marks the dump as an abort
        record.ExceptionAddress = _ReturnAddress();
        EXCEPTION_POINTERS pointers{&record, &context};
        report_crash(&pointers);
    }
    TerminateProcess(GetCurrentProcess(), 3);
    std::_Exit(3);
}

// The last filter: an exception no frame handled, of any kind. A C++ exception that leaves a thread through C or
// system frames reaches no terminate handler, so without this filter the process would end with 0xE06D7363 and no
// line or dump.
LONG WINAPI unhandled_filter(EXCEPTION_POINTERS* info) {
    if (gCrashing.exchange(1) != 0) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    char text[512];
    const bool cpp = info->ExceptionRecord->ExceptionCode == 0xE06D7363u;
    if (cpp) {
        describe_cpp_exception(info->ExceptionRecord, text, sizeof(text));
    } else {
        std::snprintf(text, sizeof(text), "unhandled exception 0x%08lX at 0x%p", info->ExceptionRecord->ExceptionCode,
                      info->ExceptionRecord->ExceptionAddress);
    }
    char line[640];
    std::snprintf(line, sizeof(line), "%s (thread %lu)", text, GetCurrentThreadId());
    log_line("crash:", line);
    HMODULE owner = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       static_cast<LPCWSTR>(info->ExceptionRecord->ExceptionAddress), &owner);
    gather_crash_facts(cpp ? galaxy::crash::Facts::Kind::Exception : galaxy::crash::Facts::Kind::Fault, line,
                       !cpp && owner != nullptr && owner == GetModuleHandleW(L"smg_game.dll"),
                       info->ExceptionRecord->ExceptionCode == EXCEPTION_STACK_OVERFLOW);
    if (!cpp) {
        note_crash_plugin(info);
    }
    report_crash(info);
    TerminateProcess(GetCurrentProcess(), 4);
    std::_Exit(4);
}

#if defined(NEBULA_HOST_RENDERER)
// NebulaNative --crash-report <file>: the report of an instance that stopped (report_crash_facts), shown by this new
// process. The technical details stay in the file, which the dialog offers to show.
// The language folder whose words the crash dialog uses: the player's setting, for Auto (or settings that do not load,
// the reason for some reports) the system's.
const char* crash_dialog_language() {
    galaxy::GameLanguage language = galaxy::GameLanguage::Auto;
    try {
        galaxy::load_runtime_settings_from_file();
        language = galaxy::get_runtime_settings().language;
    } catch (const std::exception&) {
    }
    const LANGID system = GetUserDefaultUILanguage();
    const auto region = [system](WORD sublanguage) { return SUBLANGID(system) == sublanguage; };
    const bool spain = PRIMARYLANGID(system) == LANG_SPANISH && region(SUBLANG_SPANISH_MODERN);
    const bool canada = PRIMARYLANGID(system) == LANG_FRENCH && region(SUBLANG_FRENCH_CANADIAN);
    switch (language) {
    case galaxy::GameLanguage::Japanese: return "JpJapanese";
    case galaxy::GameLanguage::English: return "UsEnglish";
    case galaxy::GameLanguage::German: return "EuGerman";
    case galaxy::GameLanguage::French: return canada ? "UsFrench" : "EuFrench";
    case galaxy::GameLanguage::Spanish: return spain ? "EuSpanish" : "UsSpanish";
    case galaxy::GameLanguage::Italian: return "EuItalian";
    case galaxy::GameLanguage::Dutch: return "EuDutch";
    case galaxy::GameLanguage::Auto: break;
    }
    switch (PRIMARYLANGID(system)) {
    case LANG_JAPANESE: return "JpJapanese";
    case LANG_GERMAN: return "EuGerman";
    case LANG_FRENCH: return canada ? "UsFrench" : "EuFrench";
    case LANG_SPANISH: return spain ? "EuSpanish" : "UsSpanish";
    case LANG_ITALIAN: return "EuItalian";
    case LANG_DUTCH: return "EuDutch";
    case LANG_PORTUGUESE: return "UsPortuguese";
    case LANG_KOREAN: return "KrKorean";
    case LANG_CHINESE:
        return region(SUBLANG_CHINESE_SIMPLIFIED) || region(SUBLANG_CHINESE_SINGAPORE) ? "CnSimpChinese" : "TwTradChinese";
    default: return "UsEnglish";
    }
}

std::wstring wide_of(std::string_view text) {
    std::wstring wide(static_cast<std::size_t>(
                          MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0)),
                      L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), static_cast<int>(wide.size()));
    return wide;
}

int show_crash_report_dialog() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    int count = 0;
    LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (args == nullptr) return 1;
    const fs::path file = count >= 3 ? fs::path(args[2]) : fs::path();
    LocalFree(args);
    std::string text;
    if (FILE* in = file.empty() ? nullptr : _wfopen(file.c_str(), L"rb")) {
        char buffer[4096];
        std::size_t n = 0;
        while ((n = std::fread(buffer, 1, sizeof(buffer), in)) > 0) text.append(buffer, n);
        std::fclose(in);
    }
    if (text.empty()) return 1;
    if (const std::size_t technical = text.find("\nTechnical details:"); technical != std::string::npos) {
        text.resize(technical);
    }
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    const char* language = crash_dialog_language();
    text = galaxy::crash::translated_report(text, language);
    text += "\n\n";
    text += galaxy::crash::translated("Open the folder with the report?", language);
    const std::wstring wide = wide_of(text);
    const std::wstring caption = wide_of(galaxy::crash::translated("Nebula stopped", language));
    if (MessageBoxW(nullptr, wide.c_str(), caption.c_str(), MB_ICONERROR | MB_YESNO | MB_SETFOREGROUND | MB_TOPMOST) ==
        IDYES) {
        const std::wstring select = L"/select,\"" + file.wstring() + L"\"";
        ShellExecuteW(nullptr, L"open", L"explorer.exe", select.c_str(), nullptr, SW_SHOWNORMAL);
    }
    return 0;
}
#endif

void install_crash_reporter() {
    gCrashRequest = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    gCrashDone = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    std::thread(crash_writer).detach();
    AddVectoredExceptionHandler(0, crash_handler);  // last: the module's own handlers (division, stack guards) first
    SetUnhandledExceptionFilter(unhandled_filter);  // whatever no frame handled, C++ exceptions included
    std::signal(SIGABRT, abort_handler);            // process-wide in the UCRT, unlike set_terminate
}
#endif

#if defined(_WIN32)
// The stack that called a fatal, as module + offset (no folder names).
void log_fatal_stack() {
    void* frames[32];
    const USHORT count = RtlCaptureStackBackTrace(1, 32, frames, nullptr);
    for (USHORT i = 0; i < count; i++) {
        HMODULE module = nullptr;
        char name[MAX_PATH] = "?";
        if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               static_cast<const char*>(frames[i]), &module)) {
            K32GetModuleBaseNameA(GetCurrentProcess(), module, name, MAX_PATH);
        }
        char line[MAX_PATH + 32];
        std::snprintf(line, sizeof(line), "%s+0x%llx", name,
                      static_cast<unsigned long long>(static_cast<const char*>(frames[i]) - reinterpret_cast<const char*>(module)));
        log_line("fatal stack:", line);
    }
}
#endif

void svc_fatal(void*, const char* text) {
    log_line("fatal:", text);
#if defined(_WIN32)
    log_fatal_stack();
#endif
    gather_crash_facts(galaxy::crash::Facts::Kind::GameFatal, text);
#if defined(_WIN32)
    if (gCrashing.exchange(1) == 0) {
        report_crash(nullptr);
    }
#else
    report_crash_facts({});
#endif
    std::fflush(stdout);
    std::_Exit(3);
}

#if defined(_WIN32)
// Arguments a restart adds to this start's command line (--gpu-start-retry).
std::wstring gRestartArguments;
#endif

#if defined(NEBULA_HOST_RENDERER)
// The renderer: the game's records reach it in frames (render_frame at the game's frame end and, while the start runs
// ahead, at the frame, which also presents). All of this runs on the simulation thread, where the game makes these
// calls.
std::atomic<uint64_t> gSubmissions{0};  // render_frame calls with commands: the renderer's frame index

galaxy::host::NativeGameAudio* gAudio = nullptr;

void submit_frame_unchecked(bool present, bool black) {
    galaxy::render::render_frame(present, black);
    gSubmissions.fetch_add(1, std::memory_order_relaxed);
}

[[noreturn]] void finish_and_exit(bool restart);

// The card has no memory free at the start: another program holds it for a moment. A new process tries again
// (--gpu-start-retry counts the tries), up to five times, two seconds apart; after that it is a fault to report.
bool retry_start_for_gpu_memory(const std::string& reason) {
#if defined(_WIN32)
    const unsigned count = ::galaxy::launch_args().gpu_start_retry;
    char out_of_memory[16];
    std::snprintf(out_of_memory, sizeof(out_of_memory), "0x%08lX", static_cast<unsigned long>(E_OUTOFMEMORY));
    const bool wanted = reason.find(out_of_memory) != std::string::npos || reason.find("VK_ERROR_OUT_OF") != std::string::npos ||
                        reason.find("VkResult -1") != std::string::npos || reason.find("VkResult -2") != std::string::npos;
    if (!wanted || count >= 5u) {
        return false;
    }
    gRestartArguments = L" --gpu-start-retry " + std::to_wstring(count + 1u);
    log_line("host: no graphics memory free at the start, trying again:", reason.c_str());
    std::this_thread::sleep_for(std::chrono::seconds(2));
    return true;
#else
    (void)reason;
    return false;
#endif
}

// The renderer's hard failures end the game with their line and a dump. Caught here: thrown on the game's stacks they
// could not unwind through its frames.
void submit_frame(bool present, bool black) {
    try {
        submit_frame_unchecked(present, black);
    } catch (const std::exception& error) {
        const std::string text = std::string("the renderer stopped: ") + error.what();
        svc_fatal(nullptr, text.c_str());
    }
}
#endif

// The stage the game reports before each 3D scene (game_stage): a crash report names it.

void svc_game_stage(void*, const char* name) {
    if (name != nullptr && gSceneStage != name) {
        gSceneStage = name;
    }
}

// The Discord application Nebula's status shows under ("Super Mario Galaxy").
constexpr const char* kDiscordClientId = "1562359210463727617";

// The game's message text (UTF-16, its tags skipped: 0x1A, then the tag's size in bytes in the next unit's high byte)
// as UTF-8.
std::string message_utf8(const uint16_t* text) {
    std::string out;
    if (text == nullptr) return out;
    for (size_t i = 0; i < 256u && text[i] != 0u; ++i) {
        uint32_t c = text[i];
        if (c == 0x1Au) {
            const uint32_t units = (static_cast<uint32_t>(text[i + 1u]) >> 8u) / 2u;
            if (units < 2u) break;
            i += units - 1u;
            continue;
        }
        if (c >= 0xD800u && c <= 0xDBFFu && text[i + 1u] >= 0xDC00u && text[i + 1u] <= 0xDFFFu) {
            c = 0x10000u + ((c - 0xD800u) << 10u) + (text[i + 1u] - 0xDC00u);
            ++i;
        }
        if (c < 0x20u) c = ' ';
        if (c < 0x80u) {
            out += static_cast<char>(c);
        } else if (c < 0x800u) {
            out += static_cast<char>(0xC0u | (c >> 6u));
            out += static_cast<char>(0x80u | (c & 0x3Fu));
        } else if (c < 0x10000u) {
            out += static_cast<char>(0xE0u | (c >> 12u));
            out += static_cast<char>(0x80u | ((c >> 6u) & 0x3Fu));
            out += static_cast<char>(0x80u | (c & 0x3Fu));
        } else {
            out += static_cast<char>(0xF0u | (c >> 18u));
            out += static_cast<char>(0x80u | ((c >> 12u) & 0x3Fu));
            out += static_cast<char>(0x80u | ((c >> 6u) & 0x3Fu));
            out += static_cast<char>(0x80u | (c & 0x3Fu));
        }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    while (!out.empty() && out.front() == ' ') out.erase(out.begin());
    return out;
}

// What the player is doing, for Discord: the galaxy (or the file select) and the star, else the Power Stars.
void svc_presence(void*, const uint16_t* galaxy, const uint16_t* star, int32_t power_stars) {
    galaxy::discord::Activity activity;
    if (power_stars < 0) {
        activity.details = "Choosing a file";
    } else {
        activity.details = message_utf8(galaxy);
        activity.state = message_utf8(star);
        if (activity.details.empty()) {
            activity.details = "At the title screen";  // the title's stage has no galaxy name
            activity.state.clear();
            static bool reached = false;
            if (!reached) {
                reached = true;
                boot_mark("title screen");
            }
        } else if (activity.state.empty()) {
            activity.state = std::to_string(power_stars) + (power_stars == 1 ? " Power Star" : " Power Stars");
        }
    }
    static std::string last;
    const std::string line = activity.details + " | " + activity.state;
    if (line != last) {
        last = line;
        log_line("presence:", line.c_str());
    }
    galaxy::discord::set_activity(activity);
}

// An achievement the save data meets: unlocked once (achievements.ini), with its English name. The game names it in
// the played language itself (NebulaText). The headless host keeps no achievements.
int svc_achievement(void*, const char* id, uint16_t* name, uint32_t capacity) {
#if !defined(NEBULA_HOST_RENDERER)
    (void)id, (void)name, (void)capacity;
    return 0;
#else
    const galaxy::achievements::Info* info = id != nullptr ? galaxy::achievements::find(id) : nullptr;
    if (info == nullptr || !galaxy::achievements::unlock(id)) return 0;
    uint32_t i = 0;
    for (; name != nullptr && i + 1u < capacity && info->name[i] != '\0'; ++i) {
        name[i] = static_cast<unsigned char>(info->name[i]);
    }
    if (name != nullptr && capacity != 0u) name[i] = 0u;
    log_line("achievement:", info->name);
    return 1;
#endif
}

int32_t svc_achievement_date(void*, const char* id) {
#if !defined(NEBULA_HOST_RENDERER)
    (void)id;
    return 0;
#else
    return id != nullptr ? galaxy::achievements::unlocked_date(id) : 0;
#endif
}

// The running mods' achievements (galaxy::mods). The headless host shows none.
int32_t svc_mod_achievement_count(void*) {
#if !defined(NEBULA_HOST_RENDERER)
    return 0;
#else
    return static_cast<int32_t>(std::min<std::size_t>(galaxy::mods::mod_achievement_count(), 1024u));
#endif
}

int32_t svc_mod_achievement(void*, int32_t index, const char** key, const char** name, const char** description, const char** mod,
                            int32_t* hidden) {
    const galaxy::mods::AchievementToAdd* achievement =
        index >= 0 && index < svc_mod_achievement_count(nullptr) ? galaxy::mods::mod_achievement(static_cast<std::size_t>(index)) : nullptr;
    if (achievement == nullptr) return 0;
    if (key != nullptr) *key = achievement->key.c_str();
    if (name != nullptr) *name = achievement->name.c_str();
    if (description != nullptr) *description = achievement->description.c_str();
    if (mod != nullptr) *mod = achievement->mod_name.c_str();
    if (hidden != nullptr) *hidden = achievement->hidden ? 1 : 0;
    return 1;
}

int32_t svc_mod_achievement_unlocked(void*) {
#if !defined(NEBULA_HOST_RENDERER)
    return -1;
#else
    const std::int32_t index = galaxy::mods::next_mod_achievement_unlocked();
    if (index >= 0) {
        if (const galaxy::mods::AchievementToAdd* achievement = galaxy::mods::mod_achievement(static_cast<std::size_t>(index))) {
            log_line("achievement:", achievement->key.c_str());
        }
    }
    return index;
#endif
}

// The all-time counters the achievement board shows (stats.ini). The headless host keeps none.
void svc_stat_add(void*, const char* name, uint64_t delta) {
#if defined(NEBULA_HOST_RENDERER)
    if (name != nullptr) galaxy::stats::add(name, delta);
#else
    (void)name, (void)delta;
#endif
}

uint64_t svc_stat_get(void*, const char* name) {
#if defined(NEBULA_HOST_RENDERER)
    return name != nullptr ? galaxy::stats::get(name) : 0u;
#else
    (void)name;
    return 0u;
#endif
}

#if defined(NEBULA_HOST_RENDERER)
// The settings view as the game was last given it. The host owns the strings, the page names and the rows that
// NebulaSettingsView points at; they stay as they are until the content changes, and the revision counts the changes.
struct SettingsRowContent {
    std::string label;
    std::string value;
    uint32_t flags = 0;
    uint16_t slider_steps = 0;
    uint16_t slider_at = 0;
    bool operator==(const SettingsRowContent&) const = default;
};

struct SettingsContent {
    uint32_t open = 0;
    uint32_t page = 0;
    uint32_t controls_page = 0;
    uint32_t input = 0;
    uint32_t selected = 0;
    uint32_t selected_button = 0;
    uint32_t mod_list_count[2] = {0, 0};
    uint32_t status = 0;
    int32_t status_number = 0;
    std::vector<std::string> pages;
    std::vector<SettingsRowContent> rows;
    std::string help;
    uint32_t help_as_is = 0;
    std::string status_text;
    std::string long_text;
    // Held here while the view points at its pixels; the same pointer is the same picture.
    std::shared_ptr<const galaxy::render::NativeSettingsPicture> picture;
    bool operator==(const SettingsContent&) const = default;
};

struct SettingsViewStorage {
    SettingsContent content;
    bool built = false;
    uint32_t revision = 0;
    // What the view points at, made from the content whenever it changes.
    std::vector<const char*> page_names;
    std::vector<NebulaSettingsRow> rows;
};

SettingsViewStorage g_settings_view;
#endif

#if defined(NEBULA_HOST_RENDERER)
static_assert(galaxy::kGameStepsPerSecond == NEBULA_STEPS_PER_SECOND, "the game and the host step at one rate");
static_assert(static_cast<uint32_t>(galaxy::render::NativeSettingsStatus::Preparing) == NEBULA_SETTINGS_STATUS_PREPARING &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsStatus::Arrange) == NEBULA_SETTINGS_STATUS_ARRANGE &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsStatus::ModAbout) == NEBULA_SETTINGS_STATUS_MOD_ABOUT &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsStatus::ModRemoveAsk) == NEBULA_SETTINGS_STATUS_MOD_REMOVE_ASK &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsStatus::Leave) == NEBULA_SETTINGS_STATUS_LEAVE &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsStatus::ModsRestartAsk) == NEBULA_SETTINGS_STATUS_MODS_RESTART_ASK &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsStatus::KeepDisplay) == NEBULA_SETTINGS_STATUS_KEEP_DISPLAY &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsStatus::JoyCon) == NEBULA_SETTINGS_STATUS_JOYCON &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsStatus::RtRestartAsk) ==
                  NEBULA_SETTINGS_STATUS_RT_RESTART_ASK &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsStatus::DiagnosticsAsk) ==
                  NEBULA_SETTINGS_STATUS_DIAGNOSTICS_ASK &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsStatus::ModsCodeAsk) == NEBULA_SETTINGS_STATUS_MODS_CODE_ASK &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsStatus::XessAsk) == NEBULA_SETTINGS_STATUS_XESS_ASK &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsStatus::License) == NEBULA_SETTINGS_STATUS_LICENSE &&
              galaxy::render::kJoyConLeftBit == NEBULA_JOYCON_LEFT && galaxy::render::kJoyConRightBit == NEBULA_JOYCON_RIGHT &&
              galaxy::render::kJoyConLeftHeldBit == NEBULA_JOYCON_LEFT_HELD &&
              galaxy::render::kJoyConRightHeldBit == NEBULA_JOYCON_RIGHT_HELD &&
              galaxy::render::kJoyConPairedBit == NEBULA_JOYCON_PAIRED &&
              galaxy::render::kJoyConLeftPressesShift == NEBULA_JOYCON_LEFT_PRESSES_SHIFT &&
              galaxy::render::kJoyConRightPressesShift == NEBULA_JOYCON_RIGHT_PRESSES_SHIFT,
              "the settings statuses are numbered alike");
static_assert(static_cast<uint32_t>(galaxy::render::NativeSettingsInput::Open) == NEBULA_SETTINGS_OPEN &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsInput::Revert) == NEBULA_SETTINGS_REVERT &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsInput::MoveMod) == NEBULA_SETTINGS_MOVE_MOD &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsInput::SetValue) == NEBULA_SETTINGS_SET_VALUE,
              "the settings inputs are numbered alike");
#endif

// The settings menu for the game's own (NebulaSettingsMenu): the overlay's state, which the game draws from then on.
// The strings, the page names and the rows are held here, whole, until the next call. The revision changes whenever the
// content differs from the previous call's. The headless host has no settings menu: always closed, nothing pointed at.
void svc_settings_view(void*, NebulaSettingsView* view) {
    std::memset(view, 0, sizeof(*view));
#if defined(NEBULA_HOST_RENDERER)
    const galaxy::render::NativeSettingsView source = galaxy::render::native_settings_view();
    SettingsContent next;
    next.open = source.open ? 1u : 0u;
    next.page = source.page;
    next.pages = source.pages;
    next.controls_page = source.controls_page;
    next.input = source.input;
    const std::size_t total = source.rows.size();
    const std::size_t selected = std::min<std::size_t>(source.selected, total == 0u ? 0u : total - 1u);
    next.rows.reserve(total);
    for (std::size_t r = 0; r < total; ++r) {
        const galaxy::render::NativeSettingsViewRow& row = source.rows[r];
        SettingsRowContent& out = next.rows.emplace_back();
        out.label = row.label;
        out.value = row.value;
        out.flags = (row.heading ? NEBULA_SETTINGS_ROW_HEADING : 0u) |
                    (row.changes ? NEBULA_SETTINGS_ROW_CHANGES : 0u) | (row.name ? NEBULA_SETTINGS_ROW_NAME : 0u) |
                    (row.active ? NEBULA_SETTINGS_ROW_ACTIVE : 0u) |
                    (row.label_name ? NEBULA_SETTINGS_ROW_LABEL_NAME : 0u) |
                    (row.buttons ? NEBULA_SETTINGS_ROW_BUTTONS : 0u) |
                    (row.mod_list == 1u ? NEBULA_SETTINGS_ROW_MOD_OFF : 0u) |
                    (row.mod_list == 2u ? NEBULA_SETTINGS_ROW_MOD_ON : 0u) | (row.held ? NEBULA_SETTINGS_ROW_HELD : 0u) |
                    (row.slider_steps != 0u ? NEBULA_SETTINGS_ROW_SLIDER : 0u) |
                    (row.inputs ? NEBULA_SETTINGS_ROW_BINDING : 0u) |
                    (row.shared ? NEBULA_SETTINGS_ROW_SHARED : 0u);
        out.slider_steps = static_cast<uint16_t>(row.slider_steps);
        out.slider_at = static_cast<uint16_t>(row.slider_at);
        if (row.mod_list != 0u && row.mod_list <= 2u && !row.heading) ++next.mod_list_count[row.mod_list - 1u];
    }
    next.selected = static_cast<uint32_t>(selected);
    next.selected_button = source.selected_button;
    next.help = source.help;
    next.help_as_is = source.help_as_is ? 1u : 0u;
    next.status = static_cast<uint32_t>(source.status);
    next.status_number = source.status_number;
    next.status_text = source.status_text;
    next.long_text = source.long_text;
    next.picture = source.picture;

    SettingsViewStorage& held = g_settings_view;
    if (!held.built || !(next == held.content)) {
        held.content = std::move(next);
        held.built = true;
        ++held.revision;
        held.page_names.clear();
        for (const std::string& name : held.content.pages) held.page_names.push_back(name.c_str());
        held.rows.clear();
        for (const SettingsRowContent& row : held.content.rows) {
            held.rows.push_back(NebulaSettingsRow{row.label.c_str(), row.value.c_str(), row.flags, row.slider_steps, row.slider_at});
        }
    }
    const SettingsContent& content = held.content;
    view->open = content.open;
    view->page = content.page;
    view->page_count = static_cast<uint32_t>(held.page_names.size());
    view->pages = held.page_names.data();
    view->controls_page = content.controls_page;
    view->input = content.input;
    view->revision = held.revision;
    view->total_rows = static_cast<uint32_t>(held.rows.size());
    view->rows = held.rows.data();
    view->selected = content.selected;
    view->selected_button = content.selected_button;
    view->mod_list_count[0] = content.mod_list_count[0];
    view->mod_list_count[1] = content.mod_list_count[1];
    view->help = content.help.c_str();
    view->help_as_is = content.help_as_is;
    view->status = content.status;
    view->status_number = content.status_number;
    view->status_text = content.status_text.c_str();
    view->long_text = content.long_text.c_str();
    if (content.picture != nullptr && !content.picture->rgba.empty()) {
        view->picture = content.picture->rgba.data();
        view->picture_width = content.picture->width;
        view->picture_height = content.picture->height;
    }
#endif
}

// The pointer's input to the settings menu: NEBULA_SETTINGS_* are galaxy::render::NativeSettingsInput's values.
void svc_settings_input(void*, uint32_t action, int32_t arg) {
#if defined(NEBULA_HOST_RENDERER)
    if (action >= NEBULA_SETTINGS_OPEN && action <= NEBULA_SETTINGS_SET_VALUE) {
        galaxy::render::native_settings_input(static_cast<galaxy::render::NativeSettingsInput>(action), arg);
    }
#else
    (void)action, (void)arg;
#endif
}

// Nebula's screens with small text (sharp_2d): the 2D layer at the window's resolution while they show.
void svc_sharp_2d(void*, uint32_t on) {
#if defined(NEBULA_HOST_RENDERER)
    galaxy::render::request_sharp_2d(on != 0u);
#else
    (void)on;
#endif
}

void svc_movie_frame(void*, const char* name, int32_t frame, float frame_rate, const void* data, uint64_t size,
                     uint32_t width, uint32_t height) {
#if defined(NEBULA_HOST_RENDERER)
    galaxy::render::attach_movie_frame(name, frame, frame_rate, data, size, width, height);
#else
    (void)name, (void)frame, (void)frame_rate, (void)data, (void)size, (void)width, (void)height;
#endif
}

// The game's steady clock: nanoseconds since the process's first read, plus one (0 means "never" to its users).
uint64_t svc_clock_ns(void*) {
    if (deterministic_mode()) {
        return gDeterministicFrames.load(std::memory_order_relaxed) * 1000000000ull / static_cast<uint64_t>(galaxy::kGameStepsPerSecond) + 1u;
    }
    static const Clock::time_point start = Clock::now();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count()) + 1u;
}

int64_t svc_calendar_ns(void*) {
    if (deterministic_mode()) {
        // A fixed date (2026-01-01 from 2000-01-01), then the frames' time.
        constexpr int64_t kFixedDate = 820454400;
        return kFixedDate * 1000000000ll + static_cast<int64_t>(gDeterministicFrames.load(std::memory_order_relaxed)) * galaxy::kGameStepNs;
    }
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &seconds);
    const std::time_t fields = _mkgmtime(&local);
#else
    localtime_r(&seconds, &local);
    const std::time_t fields = timegm(&local);
#endif
    const int64_t unix_to_2000 = 946684800;
    const int64_t fraction = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count() %
        1000000000;
    return (static_cast<int64_t>(fields) - unix_to_2000) * 1000000000 + fraction;
}

std::atomic<int32_t> gStarPointerMode{-1};

void svc_button_icons(void*, NebulaButtonIcons* icons) {
    *icons = NebulaButtonIcons{};
#if defined(NEBULA_HOST_RENDERER)
    static_assert(galaxy::input::kButtonIconCount == NEBULA_BUTTON_ICONS &&
                  static_cast<int>(galaxy::input::ButtonIconStyle::Keyboard) == NEBULA_ICON_STYLE_KEYBOARD &&
                  static_cast<int>(galaxy::input::ButtonGlyph::Key) == NEBULA_ICON_KEY &&
                  static_cast<int>(galaxy::input::ButtonGlyph::StickAxis) == NEBULA_ICON_STICK_AXIS &&
                  static_cast<int>(galaxy::input::ButtonIconModel::GameCube) == NEBULA_ICON_MODEL_GAMECUBE &&
                  static_cast<int>(galaxy::input::ButtonIconModel::Steam) == NEBULA_ICON_MODEL_STEAM &&
                  static_cast<int>(galaxy::input::ButtonIconModel::DualSenseEdge) == NEBULA_ICON_MODEL_DUALSENSE_EDGE &&
                  galaxy::input::kKeyIconLayoutFirst == NEBULA_KEY_LAYOUT_FIRST &&
                  galaxy::input::kKeyIconLayoutCount == NEBULA_KEY_LAYOUT_KEYS &&
                  galaxy::input::kKeyIconMouseLeft == NEBULA_KEY_MOUSE_LEFT &&
                  galaxy::input::kKeyIconOther == NEBULA_KEY_OTHER && galaxy::input::kKeyIconCount == NEBULA_KEYS &&
                  NEBULA_KEY_SPACE == 75 && NEBULA_KEY_RIGHT_WIN == 106 && NEBULA_KEYS < galaxy::input::kKeyIconNone);
    const int32_t mode = gStarPointerMode.load(std::memory_order_relaxed);
    const bool menu = mode >= 0 && static_cast<uint32_t>(mode) < galaxy::input::kStarPointerModeCount &&
                      galaxy::input::play_context_from_star_pointer_mode(static_cast<uint32_t>(mode)) !=
                          galaxy::input::PlayContext::Play;
    const galaxy::input::ButtonIconSpec spec = galaxy::host::button_icons(menu);
    icons->style = static_cast<uint8_t>(spec.style);
    icons->model = static_cast<uint8_t>(spec.model);
    for (size_t i = 0; i < NEBULA_BUTTON_ICONS; ++i) {
        icons->glyphs[i] = static_cast<uint8_t>(spec.glyphs[i]);
        icons->places[i] = spec.places[i];
    }
    for (size_t i = 0; i < 4u; ++i) icons->pad_keys[i] = spec.pad_keys[i];
    for (size_t i = 0; i < NEBULA_KEY_LAYOUT_KEYS; ++i) icons->layout[i] = spec.layout[i];
    for (size_t i = 0; i < NEBULA_BUTTON_ICONS; ++i) {
        std::memcpy(icons->other[i], spec.other[i].data(), NEBULA_KEY_NAME_CHARS);
        icons->other[i][NEBULA_KEY_NAME_CHARS - 1] = '\0';
    }
#endif
#if defined(NEBULA_REAL_WIIMOTE)
    icons->wii_remote = galaxy::host::real_wiimote_in_use() ? 1u : 0u;
#endif
}

// The scale the renderer last drew the 2D layer at (render_core: the scene target in use when the scene ends).
uint32_t svc_layer_2d_lines(void*) {
#if defined(NEBULA_HOST_RENDERER)
    return galaxy::render::layer_2d_lines();
#else
    return 0u;
#endif
}

// The game asks while it could pause: 1 once per request.
int svc_take_pause_request(void*) {
    return gPauseRequest.exchange(false, std::memory_order_acq_rel) ? 1 : 0;
}

#if defined(NEBULA_HOST_RENDERER)
// The settings' galaxy select: its galaxies for the game to name, and the names it gives.
int svc_galaxy_select_stage(void*, uint32_t index, char* id, uint32_t capacity, uint32_t* stars) {
    if (index >= galaxy::render::kGalaxyStageCount || id == nullptr || capacity == 0u) {
        return 0;
    }
    std::snprintf(id, capacity, "%s", galaxy::render::kGalaxyStages[index].id);
    if (stars != nullptr) *stars = galaxy::render::kGalaxyStages[index].stars;
    return 1;
}

int svc_galaxy_select_names_wanted(void*) {
    return galaxy::render::take_galaxy_select_names_wanted() ? 1 : 0;
}

void svc_galaxy_select_name(void*, uint32_t index, uint32_t star, const uint16_t* name) {
    galaxy::render::set_galaxy_select_name(index, star, message_utf8(name));
}
#else
int svc_galaxy_select_stage(void*, uint32_t, char*, uint32_t, uint32_t*) { return 0; }
int svc_galaxy_select_names_wanted(void*) { return 0; }
void svc_galaxy_select_name(void*, uint32_t, uint32_t, const uint16_t*) {}
#endif

// What the game draws from, registered once. A service reads what it is given before it returns.
#if defined(NEBULA_HOST_RENDERER)
uint32_t svc_model_register(void*, const void* file, uint32_t* first_mesh) {
    return galaxy::render::registries().model_register(file, first_mesh);
}
void svc_model_free(void*, uint32_t model) {
    galaxy::render::registries().model_free(model);
}
uint32_t svc_mesh_register(void*, const NebulaFrameMesh* mesh) {
    return galaxy::render::registries().mesh_register(*mesh);
}
void svc_mesh_free(void*, uint32_t mesh) {
    galaxy::render::registries().mesh_free(mesh);
}
uint32_t svc_texture_register(void*, const NebulaTextureImage* image) {
    return galaxy::render::textures().register_image(*image);
}
void svc_texture_update(void*, uint32_t texture, const void* bytes, uint32_t size) {
    galaxy::render::textures().update_image(texture, bytes, size);
}
void svc_texture_free(void*, uint32_t texture) {
    galaxy::render::textures().free_image(texture);
}
uint32_t svc_texture_target(void*) {
    return galaxy::render::textures().new_target();
}
uint32_t svc_texture_table(void*, const NebulaFrameTexture* list, uint32_t count) {
    return galaxy::render::registries().texture_table(list, count);
}
uint32_t svc_material_register(void*, const float (*rows)[4], uint32_t vertex_rows, uint32_t pixel_rows) {
    return galaxy::render::registries().material_register(rows, vertex_rows, pixel_rows);
}
uint32_t svc_pipeline_register(void*, const NebulaPipelineDesc* desc, const NebulaLayoutInput* inputs) {
    return galaxy::render::registries().pipeline_register(*desc, inputs);
}
void svc_texture_hd(void*, uint32_t texture, const void* hd) {
    galaxy::render::textures().set_built_in(texture, hd);
}
void svc_submit_frame(void*, NebulaFrameList* list) {
    if ((list->flags & NEBULA_FRAME_HAS_SCENE) != 0u) {
        gSceneBegins.fetch_add(1, std::memory_order_relaxed);
        end_boot_fast("first 3D scene");
    }
    list->game_frame = gGameFrames.load(std::memory_order_relaxed);
    galaxy::render::attach_frame_list(list);
    submit_frame(false, false);
}
uint32_t svc_probe_results(void*, uint32_t channel, NebulaScreenProbe* probes, uint32_t capacity) {
    static_assert(sizeof(NebulaScreenProbe) == sizeof(galaxy::render::ScreenProbe));
    return galaxy::render::screen_probe_results(channel, reinterpret_cast<galaxy::render::ScreenProbe*>(probes), capacity);
}
#else
// With no renderer nothing is drawn: every id is 0 and a list is read by nobody.
uint32_t svc_model_register(void*, const void*, uint32_t* first_mesh) {
    *first_mesh = 0u;
    return 0u;
}
void svc_model_free(void*, uint32_t) {}
uint32_t svc_mesh_register(void*, const NebulaFrameMesh*) {
    return 0u;
}
void svc_mesh_free(void*, uint32_t) {}
uint32_t svc_texture_register(void*, const NebulaTextureImage*) {
    return 0u;
}
void svc_texture_update(void*, uint32_t, const void*, uint32_t) {}
void svc_texture_free(void*, uint32_t) {}
uint32_t svc_texture_target(void*) {
    return 0u;
}
uint32_t svc_texture_table(void*, const NebulaFrameTexture*, uint32_t) {
    return 0u;
}
uint32_t svc_material_register(void*, const float (*)[4], uint32_t, uint32_t) {
    return 0u;
}
uint32_t svc_pipeline_register(void*, const NebulaPipelineDesc*, const NebulaLayoutInput*) {
    return 0u;
}
void svc_texture_hd(void*, uint32_t, const void*) {}
void svc_submit_frame(void*, NebulaFrameList* list) {
    if ((list->flags & NEBULA_FRAME_HAS_SCENE) != 0u) {
        gSceneBegins.fetch_add(1, std::memory_order_relaxed);
        end_boot_fast("first 3D scene");
    }
    list->released = 1u;
}
uint32_t svc_probe_results(void*, uint32_t, NebulaScreenProbe*, uint32_t) {
    return 0u;
}
#endif

// The Mii library's file (mii_import.h) in the data folder; a Wii save folder's Mii database (or an earlier Nebula's)
// is where it starts the first time. The game reads its Miis by place as typed records.
fs::path mii_library_path() {
    return gSaveRoot / "nebula-miis.txt";
}
fs::path console_mii_database_path() {
    return gSaveRoot / fs::path(galaxy::host::wii_mii_database_member());
}

NebulaMii nebula_mii(const galaxy::host::MiiFields& f) {
    NebulaMii m{};
    m.sex = f.sex;
    m.birth_month = f.birth_month;
    m.birth_day = f.birth_day;
    m.favorite_color = f.favorite_color;
    m.favorite = f.favorite;
    m.local_only = f.local_only;
    m.height = f.height;
    m.build = f.build;
    m.face_type = f.face_type;
    m.face_color = f.face_color;
    m.face_texture = f.face_texture;
    m.hair_type = f.hair_type;
    m.hair_color = f.hair_color;
    m.hair_flip = f.hair_flip;
    m.eye_type = f.eye_type;
    m.eye_color = f.eye_color;
    m.eye_scale = f.eye_scale;
    m.eye_rotate = f.eye_rotate;
    m.eye_x = f.eye_x;
    m.eye_y = f.eye_y;
    m.eyebrow_type = f.eyebrow_type;
    m.eyebrow_color = f.eyebrow_color;
    m.eyebrow_scale = f.eyebrow_scale;
    m.eyebrow_rotate = f.eyebrow_rotate;
    m.eyebrow_x = f.eyebrow_x;
    m.eyebrow_y = f.eyebrow_y;
    m.nose_type = f.nose_type;
    m.nose_scale = f.nose_scale;
    m.nose_y = f.nose_y;
    m.mouth_type = f.mouth_type;
    m.mouth_color = f.mouth_color;
    m.mouth_scale = f.mouth_scale;
    m.mouth_y = f.mouth_y;
    m.mustache = f.mustache;
    m.beard_type = f.beard_type;
    m.beard_color = f.beard_color;
    m.beard_scale = f.beard_scale;
    m.beard_y = f.beard_y;
    m.glass_type = f.glass_type;
    m.glass_color = f.glass_color;
    m.glass_scale = f.glass_scale;
    m.glass_y = f.glass_y;
    m.mole_type = f.mole_type;
    m.mole_scale = f.mole_scale;
    m.mole_x = f.mole_x;
    m.mole_y = f.mole_y;
    std::copy(f.name.begin(), f.name.end(), m.name);
    std::copy(f.creator.begin(), f.creator.end(), m.creator);
    std::copy(f.create_id.begin(), f.create_id.end(), m.create_id);
    return m;
}

uint32_t svc_mii_count(void*) {
    return galaxy::host::mii_library_count();
}

int32_t svc_mii_record(void*, uint32_t place, NebulaMii* mii) {
    const std::optional<galaxy::host::MiiFields> fields = galaxy::host::mii_library_mii(place);
    if (!fields) {
        return 0;
    }
    if (mii != nullptr) {
        *mii = nebula_mii(*fields);
    }
    return 1;
}

// The wheel's turn the window gathered since the game last asked.
int32_t svc_pointer_wheel(void*) {
#if defined(NEBULA_HOST_RENDERER)
    return galaxy::render::take_pointer_wheel();
#else
    return 0;
#endif
}

std::atomic<uint32_t> gTiltSection{0};
std::atomic<uint8_t> gMotor{0};

// The game's thread only: the pointer mode the controls follow, held while a closed wipe covers the screen.
galaxy::input::CoveredModeHold gModeHold;

void follow_star_pointer_mode(int32_t mode) {
    char text[64];
    if (gStarPointerMode.exchange(mode, std::memory_order_relaxed) != mode) {
        std::snprintf(text, sizeof(text), "star pointer mode %d", static_cast<int>(mode));
        log_line("context:", text);
    }
}

void svc_game_context(void*, uint32_t what, int32_t value) {
    char text[64];
    if (what == NEBULA_CONTEXT_STAR_POINTER_MODE) {
        const bool was_holding = gModeHold.holding();
        follow_star_pointer_mode(gModeHold.game(value));
        if (gModeHold.holding() && !was_holding) {
            std::snprintf(text, sizeof(text), "star pointer mode %d held under the wipe", static_cast<int>(value));
            log_line("context:", text);
        }
    } else if (what == NEBULA_CONTEXT_SCREEN_COVERED) {
        follow_star_pointer_mode(gModeHold.covered(value != 0));
    } else if (what == NEBULA_CONTEXT_TILT_SECTION) {
        if (gTiltSection.exchange(static_cast<uint32_t>(value), std::memory_order_relaxed) != static_cast<uint32_t>(value)) {
            std::snprintf(text, sizeof(text), "tilt section %d", static_cast<int>(value));
            log_line("context:", text);
        }
    } else if (what == NEBULA_CONTEXT_FRAME_GENERATION) {
#if defined(NEBULA_HOST_RENDERER)
        galaxy::render::note_frame_generation_allowed(value != 0);
#endif
    } else if (what == NEBULA_CONTEXT_CAMERA_WATER) {
#if defined(NEBULA_HOST_RENDERER)
        galaxy::render::note_camera_water(value > 0 ? static_cast<float>(value - 1) : -1.0f);
#endif
    } else if (what == NEBULA_CONTEXT_TOUCH_TARGET) {
#if defined(NEBULA_HOST_RENDERER)
        galaxy::host::note_touch_target(static_cast<std::uint32_t>(value));
#endif
    }
}

const galaxy::assets::AssetStore& asset_table() {
    return gPak.store;
}

int asset_entry_out(const std::optional<galaxy::assets::Entry>& entry, NebulaAssetEntry* out) {
    if (!entry) {
        return 0;
    }
    *out = {};
    out->index = entry->index;
    out->length = static_cast<uint32_t>(entry->length);
    out->position = entry->position;
    out->children = entry->children;
    out->folder = entry->folder ? 1 : 0;
    out->kind = entry->kind;
    out->name = entry->name;
    return 1;
}

int svc_asset_find(void*, const char* path, NebulaAssetEntry* out) {
    return path != nullptr ? asset_entry_out(asset_table().find(path), out) : 0;
}

int svc_asset_find_game(void*, const char* path, NebulaAssetEntry* out) {
    return path != nullptr ? asset_entry_out(asset_table().find_game(path), out) : 0;
}

int svc_asset_entry(void*, int32_t index, NebulaAssetEntry* out) {
    return asset_entry_out(asset_table().entry(index), out);
}

int svc_asset_added_child(void*, const char* path, uint32_t child, NebulaAssetEntry* out) {
    return path != nullptr ? asset_entry_out(asset_table().added_child(path, child), out) : 0;
}

int svc_asset_child(void*, int32_t folder, uint32_t child, NebulaAssetEntry* out) {
    return asset_entry_out(asset_table().child(folder, child), out);
}

// The game's memory. An arena is reserved from the top of the address space down, far above 4 GB and the file views, so an
// address squeezed through 32 bits fails at once, in every run.
void* svc_memory_reserve(void*, uint64_t size) {
#if defined(_WIN32)
    return VirtualAlloc(nullptr, static_cast<SIZE_T>(size), MEM_RESERVE | MEM_TOP_DOWN, PAGE_NOACCESS);
#else
    void* const block = mmap(nullptr, static_cast<size_t>(size), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    return block != MAP_FAILED ? block : nullptr;
#endif
}

int svc_memory_commit(void*, void* start, uint64_t size) {
#if defined(_WIN32)
    return VirtualAlloc(start, static_cast<SIZE_T>(size), MEM_COMMIT, PAGE_READWRITE) != nullptr ? 1 : 0;
#else
    (void)start, (void)size;
    return 1;
#endif
}

void svc_memory_decommit(void*, void* start, uint64_t size) {
#if defined(_WIN32)
    VirtualFree(start, static_cast<SIZE_T>(size), MEM_DECOMMIT);
#else
    madvise(start, static_cast<size_t>(size), MADV_DONTNEED);
#endif
}

void svc_memory_release(void*, void* start, uint64_t size) {
#if defined(_WIN32)
    (void)size;
    VirtualFree(start, 0, MEM_RELEASE);
#else
    munmap(start, static_cast<size_t>(size));
#endif
}

// An extension's slow drive (disc_read_delay_us): each way to the store takes this long, so waits on loads are exercised.
void test_drive_delay() {
    const uint32_t delay_us = galaxy::extensions::disc_read_delay_us();
    if (delay_us != 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(delay_us));
    }
}

const void* svc_asset_map(void*, int32_t file) {
    test_drive_delay();
    const std::optional<galaxy::assets::Entry> entry = asset_table().entry(file);
    if (!entry) {
        return nullptr;
    }
    return gPak.store.map(entry->position, static_cast<std::size_t>(entry->length));
}

void* svc_asset_map_private(void*, int32_t file) {
    test_drive_delay();
    const std::optional<galaxy::assets::Entry> entry = asset_table().entry(file);
    if (!entry) {
        return nullptr;
    }
    void* const data = gPak.store.map_private(entry->position, static_cast<std::size_t>(entry->length));
    const bool trace_reads = ::galaxy::diagnosing(::galaxy::kDiagnoseFileReads);
    if (trace_reads) {
        std::fprintf(stderr, "[read] frame %llu entry %d mapped size %llu %s at %p\n", static_cast<unsigned long long>(gDeterministicFrames.load()), file,
                     static_cast<unsigned long long>(entry->length), entry->name != nullptr ? entry->name : "?", data);
    }
    return data;
}

void svc_asset_unmap(void*, void* data, uint64_t size) {
    const bool trace_reads = ::galaxy::diagnosing(::galaxy::kDiagnoseFileReads);
    if (trace_reads) {
        std::fprintf(stderr, "[read] frame %llu unmapped %p size %llu\n", static_cast<unsigned long long>(gDeterministicFrames.load()), data, static_cast<unsigned long long>(size));
    }
    gPak.store.unmap_private(data, static_cast<std::size_t>(size));
}

int32_t svc_asset_read(void*, int32_t file, uint64_t file_offset, uint32_t size, void* dst) {
    test_drive_delay();
    gFileReads.fetch_add(1, std::memory_order_relaxed);
    // --diagnostics file-reads: each read with the frame it came on: two runs of deterministic mode compare by it.
    const bool trace_reads = ::galaxy::diagnosing(::galaxy::kDiagnoseFileReads);
    const std::optional<galaxy::assets::Entry> entry = asset_table().entry(file);
    if (trace_reads) {
        std::fprintf(stderr, "[read] frame %llu entry %d at %llu size %u %s\n", static_cast<unsigned long long>(gDeterministicFrames.load(std::memory_order_relaxed)),
                     file, static_cast<unsigned long long>(file_offset), size, entry && entry->name != nullptr ? entry->name : "?");
    }
    if (!entry || file_offset > entry->length || entry->length - file_offset < size ||
        !gPak.store.read(entry->position + file_offset, size, dst)) {
        return -1;
    }
    return static_cast<int32_t>(size);
}

const char* svc_game_id(void*) {
    return gPak.id;
}

void svc_frame_begun(void*, int black, uint32_t frame) {
    gGameFrames.store(frame, std::memory_order_relaxed);
#if defined(NEBULA_HOST_RENDERER)
    // --diagnostics frame-times: each frame's time, where a scripted press begins (press-to-present measurements).
    if (::galaxy::diagnosing(::galaxy::kDiagnoseFrameTimes)) std::fprintf(stderr, "[latency] frame=%u qpc=%lld\n", frame, static_cast<long long>(galaxy::qpc_now()));
#endif
#if defined(NEBULA_HOST_RENDERER)
#if defined(_WIN32)
    galaxy::timeline::record(galaxy::timeline::Event::GameFrame, frame, 0u);
#endif
    if (frame >= kBootFrames) {
        end_boot_fast("start's frames run out");
    }
#endif
    galaxy::extensions::frame({frame});
    // The Mods screens' answers to mods' requests (galaxy/mod_screens.h), on the game thread at the frame's start.
    galaxy::mods::run_screen_answers();
#if defined(NEBULA_HOST_RENDERER)
    {
        // While the start runs ahead, one frame a display period goes to the screen.
        bool present = true;
        if (gBootFast.load(std::memory_order_relaxed) && !deterministic_mode()) {
            static Clock::time_point shown{};
            const Clock::time_point now = Clock::now();
            present = now - shown >= std::chrono::milliseconds(16);
            if (present) shown = now;
        }
        // The renderer's newest picture, or black while the game shows black.
        submit_frame(present, black != 0);
    }
    // Miis imported from the settings while the game runs: the game reads the library again.
    if (const auto placed = galaxy::host::take_mii_installs(); !placed.empty()) {
        gModule.mii_changed();
        std::fprintf(stderr, "[mii] %zu imported while running\n", placed.size());
    }
    // The output's shape (the window's, or the chosen aspect) is latched once this frame's picture is presented:
    // queued frames finish first, so a picture is never fitted with the next frame's camera and layouts.
    {
        const auto client = galaxy::render::aspect_latch_extent();
        if (client.width > 1 && client.height > 1 &&
            galaxy::experimental_dynamic_aspect_change_pending(client.width, client.height)) {
            galaxy::render::wait_for_render_idle();
            const auto settled = galaxy::render::aspect_latch_extent();
            if (settled.width > 1 && settled.height > 1) {
                galaxy::experimental_latch_dynamic_aspect(settled.width, settled.height);
            }
        }
    }
#else
    (void)black;
#endif
}

// The shape the game draws for (display_native.c turns it into the game's screen width and camera aspect): the
// latched output shape; headless, 16:9.
uint32_t svc_display_extent(void*) {
#if defined(NEBULA_HOST_RENDERER)
    return static_cast<uint32_t>(galaxy::g_experimental_dynamic_aspect_word.load(std::memory_order_acquire));
#else
    return 0;
#endif
}

// While their files cook at the start: the phase NEBULA_OPTION_PREPARATION reports, and where the cooker says how far it is.
std::atomic<uint32_t> gModsPreparing{0u};
#if defined(NEBULA_HOST_RENDERER)
bool gPipelinesPrepared = false;
#endif
fs::path gModsProgressFile;

uint32_t svc_option(void*, uint32_t what) {
    switch (what) {
    case NEBULA_OPTION_TEXT_SPEED:
#if defined(NEBULA_HOST_RENDERER)
        return std::clamp(galaxy::get_runtime_settings().text_speed, 1u, 4u);
#else
        return 1;
#endif
    case NEBULA_OPTION_PIXEL_LIT:
#if defined(NEBULA_HOST_RENDERER)
    {
        const galaxy::render::RenderConfig render = galaxy::render::get_render_config();
        return render.per_pixel_lighting || render.enhanced_lighting ? 1u : 0u;
    }
#else
        return 0;
#endif
    case NEBULA_OPTION_INPUT_MODE:
#if defined(NEBULA_HOST_RENDERER)
        switch (galaxy::get_runtime_input_mode_state().mode) {
        case galaxy::RuntimeInputMode::Controller: return NEBULA_INPUT_MODE_CONTROLLER;
        case galaxy::RuntimeInputMode::RealWiimote:
        case galaxy::RuntimeInputMode::WiimoteMotion: return NEBULA_INPUT_MODE_WII_REMOTE;
        case galaxy::RuntimeInputMode::Phone:
        case galaxy::RuntimeInputMode::Auto: return NEBULA_INPUT_MODE_AUTO;
        case galaxy::RuntimeInputMode::KeyboardMouse: break;
        }
#endif
        return NEBULA_INPUT_MODE_KEYBOARD_MOUSE;
    case NEBULA_OPTION_MENU_TEXT_SIZE:
#if defined(NEBULA_HOST_RENDERER)
        return galaxy::get_runtime_settings().menu_text_size;
#else
        return 0;
#endif
    case NEBULA_OPTION_MODS_CHANGED:
        if (galaxy::extensions::mods_changed()) {
            return 1u;
        }
#if defined(NEBULA_HOST_RENDERER)
        return galaxy::render::native_settings_mods_changed() ? 1u : 0u;
#else
        return 0;
#endif
    case NEBULA_OPTION_WINDOW_WIDTH_DP:
#if defined(NEBULA_HOST_RENDERER)
        return galaxy::host::window_width_dp();
#else
        return 0;
#endif
    case NEBULA_OPTION_BLOOM:
#if defined(NEBULA_HOST_RENDERER)
        return galaxy::render::bloom_route();
#else
        return 0;
#endif
    case NEBULA_OPTION_PREPARE_KIND:
        return gModsPreparing.load(std::memory_order_acquire) != 0u ? 1u : 2u;
    case NEBULA_OPTION_PREPARATION: {
        const uint32_t mods = gModsPreparing.load(std::memory_order_acquire);
        if (mods != 0u) {
            return mods;
        }
#if defined(NEBULA_HOST_RENDERER)
        // The title waits once for the pipeline list; what a later settings change adds prepares behind the game.
        // Deterministic test mode doesn't: how long the workers take is the machine's, so the frames waited would differ
        // run to run, and a draw makes its own pipeline when it isn't made yet.
        if (!gPipelinesPrepared) {
            gPipelinesPrepared = deterministic_mode() || galaxy::render::pipeline_progress().left == 0u;
        }
        return gPipelinesPrepared ? 0u : 2u;
#else
        return 0u;
#endif
    }
    case NEBULA_OPTION_PREPARE_PROGRESS: {
        if (gModsPreparing.load(std::memory_order_acquire) == 0u) {
#if defined(NEBULA_HOST_RENDERER)
            const galaxy::render::PipelineProgress progress = galaxy::render::pipeline_progress();
            const uint64_t all = uint64_t{progress.done} + progress.left;
            return all != 0u ? static_cast<uint32_t>(progress.done * uint64_t{10000u} / all) : 0u;
#else
            return 0u;
#endif
        }
        std::ifstream in(gModsProgressFile);
        uint64_t done = 0;
        uint64_t total = 0;
        in >> done >> total;
        return total != 0u ? static_cast<uint32_t>(done * 10000u / total) : 0u;
    }
    case NEBULA_OPTION_DIAGNOSE_SCENE_LOADS:
        return ::galaxy::diagnosing(::galaxy::kDiagnoseSceneLoads) ? 1u : 0u;
    case NEBULA_OPTION_TOUCH_POINTER:
#if defined(NEBULA_HOST_RENDERER)
        return galaxy::host::touch_pointer_state();
#else
        return 0;
#endif
    default:
        return 0;
    }
}

static_assert(int{galaxy::saves::kDone} == NEBULA_SAVE_DONE && int{galaxy::saves::kNoFile} == NEBULA_SAVE_NO_FILE &&
                  int{galaxy::saves::kTooLarge} == NEBULA_SAVE_TOO_LARGE &&
                  int{galaxy::saves::kFailed} == NEBULA_SAVE_FAILED,
              "save results are the game's");

// A save file operation on the save worker (the game polls the status): galaxy/save_files.h in the game's save folder
// (gSaveDataPath under the save folder, or a mod's save folder).
std::atomic<int> gSavesPending{0};
// The facts file of the mods' store attached this run (cook_mod_files), read into the mod log at the mods' start.
fs::path gCookFacts;
// The exit waits here for the saves still on the worker.
std::mutex gSavesMutex;
std::condition_variable gSavesDone;

void svc_save_request(void*, const NebulaSaveRequest* request) {
    gSaveOps.fetch_add(1, std::memory_order_relaxed);
    *request->status = NEBULA_SAVE_PENDING;
    gSavesPending.fetch_add(1, std::memory_order_acq_rel);
    const NebulaSaveRequest r = *request;
    const std::string name = r.name != nullptr ? r.name : "";
    auto operation = [r, name] {
        fs::path folder;
        galaxy::saves::Result result = galaxy::saves::kFailed;
        if (save_folder_path(name.starts_with("GameData.bin") ? gOldSaveDataPath.c_str() : gSaveDataPath.c_str(), &folder)) {
            switch (r.op) {
            case NEBULA_SAVE_READ:
                result = galaxy::saves::read(folder, name, r.buffer, r.size, r.length);
                break;
            case NEBULA_SAVE_WRITE:
                result = galaxy::saves::write(folder, name, r.buffer, r.size);
                break;
            case NEBULA_SAVE_DELETE:
                result = galaxy::saves::remove(folder, name);
                break;
            }
        }
#if defined(__cpp_lib_atomic_ref)
        std::atomic_ref<int32_t>(*r.status).store(result, std::memory_order_release);
#else  // libc++ without atomic_ref (Android NDK r28): the compiler's builtin, the same release store.
        __atomic_store_n(r.status, result, __ATOMIC_RELEASE);
#endif
        if (gSavesPending.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            { const std::lock_guard<std::mutex> lock(gSavesMutex); }
            gSavesDone.notify_all();
        }
    };
    // Deterministic test mode: done before the call returns, as disc reads are, so the game's poll sees the same
    // status on the same frame every run.
    if (deterministic_mode()) {
        operation();
    } else {
        gFs.post(operation);
    }
}

// The settings the game asks the system for. The pointer mapping (native_input's virtual sensor bar) assumes the bar above
// the screen.
uint32_t svc_system_setting(void*, uint32_t item) {
    switch (item) {
    case NEBULA_SETTING_LANGUAGE:
#if defined(NEBULA_HOST_RENDERER)
        return static_cast<uint32_t>(galaxy::game_language_for_boot());  // the language setting
#else
        return 1;  // English
#endif
    case NEBULA_SETTING_SOUND_MODE:
#if defined(NEBULA_HOST_RENDERER)
        return static_cast<uint32_t>(galaxy::get_runtime_settings().audio.sound_output);
#else
        return 1;  // stereo
#endif
    case NEBULA_SETTING_WIDESCREEN:
        return 1;
    default:
        return 0;
    }
}

// Player 1: Nebula's controls (the bindings, keyboard and mouse, controllers; a real Wii Remote while it is in use, its
// sensors translated by wiimote::Motion), as a controller with a move stick, connected from the start. The game's read
// takes the sample. Headless, the controller lies still.
NebulaInputSample rest_input_sample() {
    NebulaInputSample sample{};
    sample.roll[0] = 1.0f;
    sample.distance = (0.2f / 0.383864f) / 0.3125f;
    sample.tilt[2] = 1.0f;
    sample.sub_tilt[2] = 1.0f;
    return sample;
}

#if defined(NEBULA_HOST_RENDERER)
static_assert(NEBULA_BUTTON_A == galaxy::input::kButtonA && NEBULA_BUTTON_B == galaxy::input::kButtonB &&
              NEBULA_BUTTON_1 == galaxy::input::kButton1 && NEBULA_BUTTON_2 == galaxy::input::kButton2 &&
              NEBULA_BUTTON_PLUS == galaxy::input::kButtonPlus && NEBULA_BUTTON_MINUS == galaxy::input::kButtonMinus &&
              NEBULA_BUTTON_HOME == galaxy::input::kButtonHome && NEBULA_BUTTON_UP == galaxy::input::kButtonUp &&
              NEBULA_BUTTON_DOWN == galaxy::input::kButtonDown && NEBULA_BUTTON_LEFT == galaxy::input::kButtonLeft &&
              NEBULA_BUTTON_RIGHT == galaxy::input::kButtonRight && NEBULA_BUTTON_C == galaxy::input::kButtonC &&
              NEBULA_BUTTON_Z == galaxy::input::kButtonZ,
              "the host's buttons are the game's");

NebulaInputSample input_sample(const galaxy::input::PlayerInput& input) {
    NebulaInputSample sample{};
    sample.buttons = galaxy::input::button_bits(input.buttons);
    sample.stick[0] = input.stick_x;
    sample.stick[1] = input.stick_y;
    sample.pointer_on = input.pointer_on ? 1u : 0u;
    sample.pointer[0] = input.pointer_x;
    sample.pointer[1] = input.pointer_y;
    sample.roll[0] = input.roll_x;
    sample.roll[1] = input.roll_y;
    sample.distance = input.distance;
    for (int i = 0; i < 3; ++i) {
        sample.tilt[i] = input.gravity[i];
        sample.sub_tilt[i] = input.nunchuk_gravity[i];
    }
    return sample;
}

// A device's tilt eased, and its spin held as presses: what a player does, one frame at a time.
class ControlsMotion {
public:
    void reset() {
        tilt_ = sub_tilt_ = {0.0f, 0.0f, 1.0f};
    }
    void apply(const galaxy::input::PlayerInput& input, NebulaInputSample& sample) {
        constexpr float kFollow = 0.15f;  // of the difference, each frame
        for (int i = 0; i < 3; ++i) {
            tilt_[i] += kFollow * (input.gravity[i] - tilt_[i]);
            sub_tilt_[i] += kFollow * (input.nunchuk_gravity[i] - sub_tilt_[i]);
            sample.tilt[i] = tilt_[i];
            sample.sub_tilt[i] = sub_tilt_[i];
        }
        sample.spin_presses = input.shake && !held_ ? 1u : 0u;
        sample.sub_spin_presses = input.nunchuk_shake && !sub_held_ ? 1u : 0u;
        held_ = input.shake;
        sub_held_ = input.nunchuk_shake;
    }

private:
    std::array<float, 3> tilt_{0.0f, 0.0f, 1.0f};
    std::array<float, 3> sub_tilt_{0.0f, 0.0f, 1.0f};
    bool held_ = false;
    bool sub_held_ = false;
};
ControlsMotion gControlsMotion;
galaxy::input::wiimote::Motion gRemoteMotion;
// One sample is taken at a time: on the game's thread when it reads, and on the input thread while a real remote is in use.
std::mutex gInputSampleLock;
std::atomic<bool> gRemoteConnected{false};  // a real remote is connected
std::atomic<bool> gInputRead{false};  // the game has read its input
std::atomic<bool> gControlsInput{true};  // player 1 is the controls (not a real remote)
std::atomic<uint8_t> gInputNunchuk{1};
std::atomic<uint8_t> gInputBattery{4};
std::atomic<uint8_t> gInputConnected{1};

// The chosen input mode has nothing to play with: the game shows its message (GameSystemErrorWatcher) until that changes.
// Looked at ten times a second.
bool input_device_missing(bool wii_remote) {
    static unsigned tick = 0;
    static bool missing = false;
    if (++tick % 10u != 0u) {
        return missing;
    }
    std::array<galaxy::input::ControllerInfo, galaxy::input::kMaxControllers> connected{};
    const bool controller = galaxy::host::connected_controllers(connected) != 0u;
#if defined(__ANDROID__)
    // Its screen is always in hand.
    const bool own_controls = galaxy::get_runtime_input_mode_state().mode == galaxy::RuntimeInputMode::Auto;
#else
    const bool own_controls = false;
#endif
    missing = !own_controls && galaxy::input_missing(galaxy::get_runtime_input_mode_state().mode, controller, wii_remote,
                                                     galaxy::host::keyboard_or_mouse_attached());
    return missing;
}

// The controls now (under gInputSampleLock). Each read renews the motor's state (a stale one stops).
galaxy::input::PlayerInput controls_input() {
    const int32_t mode = gStarPointerMode.load(std::memory_order_relaxed);
    galaxy::host::native_game_rumble(gMotor.load(std::memory_order_relaxed), mode);
    return galaxy::host::native_game_input(gGameFrames.load(std::memory_order_relaxed), mode,
                                           static_cast<uint8_t>(gTiltSection.load(std::memory_order_relaxed)));
}

// Whether the sample is someone playing: a button down, a stick pushed past its dead zone, a spin, or the pointer moved. Taken
// on the game's thread at each read, under gInputSampleLock; the time goes to the power policy (power_policy.h).
void note_input_activity(const NebulaInputSample& sample) {
    static float last_pointer[2] = {0.0f, 0.0f};
    constexpr float kStickPush = 0.25f;
    constexpr float kPointerMove = 0.01f;
    const bool pointer_moved = sample.pointer_on != 0u && (std::fabs(sample.pointer[0] - last_pointer[0]) > kPointerMove ||
                                                          std::fabs(sample.pointer[1] - last_pointer[1]) > kPointerMove);
    last_pointer[0] = sample.pointer[0];
    last_pointer[1] = sample.pointer[1];
    if (sample.buttons != 0u || std::fabs(sample.stick[0]) > kStickPush || std::fabs(sample.stick[1]) > kStickPush ||
        sample.spin_presses != 0u || sample.sub_spin_presses != 0u || pointer_moved) {
        gLastInputMs.store(steady_ms(), std::memory_order_relaxed);
    }
}
#endif

// The players svc_input_read serves: player 1's live controls, and with the renderer player 2 (co-star mode).
enum HostPlayer : uint32_t {
    kHostPlayerMain,
#if defined(NEBULA_HOST_RENDERER)
    kHostPlayerCoStar,
#endif
    kHostPlayers
};

uint32_t svc_input_players(void*) {
    return kHostPlayers;
}

uint32_t svc_input_read(void*, uint32_t player, NebulaInputDevice* device, NebulaInputSample* sample) {
    if (device != nullptr) {
        *device = {};
#if defined(NEBULA_HOST_RENDERER)
        // Player 2, when someone plays it (co-star mode): the pointer and its buttons, no move stick.
        if (player == kHostPlayerCoStar) {
            galaxy::input::PlayerInput co_star{};
            if (galaxy::host::native_game_co_star(&co_star)) {
                device->connected = 1;
                device->battery = 4;
                if (sample != nullptr) {
                    *sample = input_sample(co_star);
                    return 1;
                }
            }
            return 0;
        }
#endif
        if (player == kHostPlayerMain) {
            device->connected = 1;
#if defined(NEBULA_HOST_RENDERER)
            device->connected = gInputConnected.load(std::memory_order_relaxed);
            device->nunchuk = gInputNunchuk.load(std::memory_order_relaxed);
            device->battery = gInputBattery.load(std::memory_order_relaxed);
#else
            device->nunchuk = 1;
            device->battery = 4;
#endif
        }
    }
    if (player != 0 || sample == nullptr) {
        return 0;
    }
#if defined(NEBULA_HOST_RENDERER)
    gInputRead.store(true, std::memory_order_release);
    gInputRead.notify_one();
    if (gControlsInput.load(std::memory_order_acquire)) {
        const std::lock_guard<std::mutex> lock(gInputSampleLock);
        const galaxy::input::PlayerInput input = controls_input();
        *sample = input_sample(input);
        gControlsMotion.apply(input, *sample);
        const bool missing = input_device_missing(gRemoteConnected.load(std::memory_order_relaxed));
        galaxy::host::set_input_missing_shown(missing);
        gInputConnected.store(missing ? 0u : 1u, std::memory_order_relaxed);
    } else {
        galaxy::input::wiimote::MotionFrame frame;
        const galaxy::input::PlayerInput input = gRemoteMotion.read(frame);
        *sample = input_sample(input);
        for (int i = 0; i < 3; ++i) {
            sample->tilt[i] = frame.tilt[i];
            sample->sub_tilt[i] = frame.sub_tilt[i];
        }
        sample->spin_presses = frame.spin_presses;
        sample->sub_spin_presses = frame.sub_spin_presses;
#if defined(NEBULA_REAL_WIIMOTE)
        // Its motor is on or off: a strength is how often it is on.
        static uint32_t motor_share = 0;
        motor_share += gMotor.load(std::memory_order_relaxed);
        const bool motor_on = motor_share >= 255u;
        if (motor_on) {
            motor_share -= 255u;
        }
        galaxy::host::real_wiimote_set_rumble(motor_on);
#endif
    }
    note_input_activity(*sample);
    return 1;
#else
    *sample = rest_input_sample();
    return 1;
#endif
}

void svc_input_control(void*, uint32_t player, uint32_t what, uint32_t value) {
#if defined(NEBULA_HOST_RENDERER)
    if (player == 0 && what == NEBULA_INPUT_RESET) {
        const std::lock_guard<std::mutex> lock(gInputSampleLock);
        gControlsMotion.reset();
        gRemoteMotion.reset();
        return;
    }
#endif
    if (player != 0 || what != NEBULA_INPUT_RUMBLE) {
        return;  // player 1's controller is never let go; there are no others
    }
    gMotor.store(static_cast<uint8_t>(value), std::memory_order_relaxed);
#if defined(NEBULA_REAL_WIIMOTE)
    if (!galaxy::host::real_wiimote_in_use()) {
        galaxy::host::native_game_rumble(static_cast<uint8_t>(value), gStarPointerMode.load(std::memory_order_relaxed));
    }
#elif defined(NEBULA_HOST_RENDERER)
    galaxy::host::native_game_rumble(static_cast<uint8_t>(value), gStarPointerMode.load(std::memory_order_relaxed));
#endif
}

#if defined(NEBULA_HOST_RENDERER)
void input_thread() {
    if (deterministic_mode()) {
        return;  // the read takes the sample (svc_input_read)
    }
#if !defined(NEBULA_REAL_WIIMOTE)
    return;
#else
    name_thread(L"Nebula Host Input");
    gInputRead.wait(false, std::memory_order_acquire);
    const auto period = std::chrono::milliseconds(10);
    auto next = Clock::now() + period;
    galaxy::host::ensure_real_wiimote_started();
    // The remote's data reports, IR camera on: with its Nunchuk, buttons, accelerometer, IR and extension; without.
    galaxy::host::real_wiimote_set_report(0x37);
    galaxy::host::real_wiimote_set_ir(true);
    // A real Wii Remote: `remote` while one is connected (its calibration), `real` while the game plays with it.
    std::optional<galaxy::host::RealWiimoteConnection> remote;
    galaxy::input::wiimote::Reader reader;
    galaxy::input::wiimote::MotionPointer motion_pointer;
    bool real = false;
    std::optional<galaxy::input::DeviceKey> device_when_real;
    const auto use_remote = [&](bool on) {
        if (on == real || (on && !remote.has_value())) return;
        real = on;
        if (on) {
            galaxy::host::set_input_missing_shown(false);
            gInputConnected.store(1u, std::memory_order_relaxed);
        }
        galaxy::host::real_wiimote_set_active(on);
#if defined(NEBULA_HOST_SDL)
        galaxy::host::real_wiimote_taken(on);
#endif
        gControlsInput.store(!on, std::memory_order_release);
        gInputNunchuk.store(on ? (remote->nunchuk ? 1u : 0u) : 1u, std::memory_order_relaxed);
        gInputBattery.store(on ? remote->battery : 4u, std::memory_order_relaxed);
        device_when_real = galaxy::host::last_input_device();
        next = Clock::now() + period;
        log_line("[wiimote]", on ? "in use" : "keyboard, mouse and controllers in use");
    };
    for (;;) {
        // A real Wii Remote's reports become samples as they arrive: in the Wii Remote input mode while it is
        // connected, in Auto from its first button press until a key, a mouse button or a controller is used. The
        // keyboard, mouse and controllers are read every 10 ms meanwhile (for that, and the settings menu).
        std::array<uint8_t, 22> real_report{};
        std::size_t real_size = 0;
        galaxy::host::RealWiimoteConnection real_connection{};
        const auto until_tick =
            std::chrono::duration_cast<std::chrono::milliseconds>(next - Clock::now()).count();
        // Not playing with it: nothing to do until a report or a connection arrives.
        const galaxy::host::RealWiimoteEvent event = galaxy::host::real_wiimote_wait(
            real ? static_cast<uint32_t>(std::clamp<long long>(until_tick, 0, 10)) : 3600000u, real_report, real_size,
            real_connection);
        const bool wii_remote_mode = galaxy::is_wii_remote_mode(galaxy::get_runtime_input_mode_state().mode);
        if (event == galaxy::host::RealWiimoteEvent::Connected) {
            remote = real_connection;
            gRemoteConnected.store(true, std::memory_order_relaxed);
            reader.connect(real_connection.eeprom, real_connection.extension_calibration, real_connection.nunchuk,
                           real_connection.motion_plus
                               ? std::span<const uint8_t>(real_connection.motion_plus_calibration)
                               : std::span<const uint8_t>());
            motion_pointer.reset();
            galaxy::host::real_wiimote_set_report(real_connection.nunchuk ? 0x37 : 0x33);
            if (real) {
                gInputNunchuk.store(real_connection.nunchuk ? 1u : 0u, std::memory_order_relaxed);
                gInputBattery.store(real_connection.battery, std::memory_order_relaxed);
            } else if (wii_remote_mode) {
                use_remote(true);
            }
            continue;
        }
        if (event == galaxy::host::RealWiimoteEvent::Disconnected) {
            use_remote(false);
            remote.reset();
            gRemoteConnected.store(false, std::memory_order_relaxed);
            continue;
        }
        if (event == galaxy::host::RealWiimoteEvent::Battery) {
            if (remote.has_value()) remote->battery = real_connection.battery;
            if (real) gInputBattery.store(real_connection.battery, std::memory_order_relaxed);
            continue;
        }
        if (event == galaxy::host::RealWiimoteEvent::Report) {
            const std::span<const uint8_t> bytes(real_report.data(), real_size);
            if (!real && (wii_remote_mode || galaxy::host::real_wiimote_buttons_held(bytes))) use_remote(true);
            galaxy::input::PlayerInput input{};
            galaxy::input::wiimote::MotionRates rates;
            if (real && reader.read(bytes, input, &rates)) {
                // The MotionPlus on (the motion pointer mode): the pointer is its, 2 recenters it.
                if (remote.has_value() && remote->motion_plus) {
                    const auto now_us = static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch()).count());
                    motion_pointer.apply(input, rates, now_us);
                }
                gRemoteMotion.add(input);
            }
            continue;
        }
        if (!real || Clock::now() < next) {
            continue;
        }
        next += period;
        const std::lock_guard<std::mutex> lock(gInputSampleLock);
        (void)controls_input();  // for the device last used, and the settings menu
        // Another device used (in Auto): back to the keyboard, mouse and controllers, with this sample.
        if (!galaxy::is_wii_remote_mode(galaxy::get_runtime_input_mode_state().mode) &&
            galaxy::host::last_input_device() != device_when_real) {
            use_remote(false);
        }
    }
#endif
}
#endif

#if defined(NEBULA_HOST_RENDERER)
#include "galaxy/wiimote_speaker.h"
#endif

// The remote speaker's sound, player 1's (6 kHz blocks): the audio output mixes it at the speaker gain (I-06).
void svc_speaker_samples(void*, uint32_t player, const int16_t* samples, uint32_t count) {
#if defined(NEBULA_HOST_RENDERER)
    if (player == 0u && samples != nullptr) {
        galaxy::input::speaker::game_speaker().push(std::span<const std::int16_t>(samples, count), 6000u);
    }
#else
    (void)player, (void)samples, (void)count;
#endif
}

// The game's sound (native_audio.h), on its audio thread. Headless, nothing is mixed.
void svc_audio_setup(void*, NebulaVoice* voices, uint32_t count, const NebulaEffectLine* effects) {
#if defined(NEBULA_HOST_RENDERER)
    gAudio->setup(voices, count, effects, !deterministic_mode());
#else
    (void)voices, (void)count, (void)effects;
#endif
}

void svc_audio_mix(void*, float gain, uint32_t used) {
#if defined(NEBULA_HOST_RENDERER)
    gAudio->mix(gain, used);
#else
    (void)gain;
    (void)used;
#endif
    gSoundSubframes.fetch_add(1, std::memory_order_relaxed);
}

void svc_audio_frame(void*) {
    if (::galaxy::diagnosing(::galaxy::kDiagnoseFileReads)) {
        std::fprintf(stderr, "[read] sound frame %llu at frame %llu\n", static_cast<unsigned long long>(gSoundBlocks.load(std::memory_order_relaxed)),
                     static_cast<unsigned long long>(gDeterministicFrames.load(std::memory_order_relaxed)));
    }
#if defined(NEBULA_HOST_RENDERER)
    gAudio->frame();
#endif
}

// The movie's sound: headless, every frame is taken and dropped.
int svc_movie_audio(void*, const void* record, uint32_t size) {
#if defined(NEBULA_HOST_RENDERER)
    gLastMovieMs.store(steady_ms(), std::memory_order_relaxed);  // a cutscene is playing (power_policy.h)
    return gAudio->movie_audio(record, size);
#else
    (void)record, (void)size;
    return 1;
#endif
}

double svc_movie_audio_position(void*) {
#if defined(NEBULA_HOST_RENDERER)
    return gAudio->movie_audio_position();
#else
    return 0.0;
#endif
}

void svc_movie_audio_control(void*, uint32_t what, int32_t value, int32_t milliseconds) {
#if defined(NEBULA_HOST_RENDERER)
    gAudio->movie_audio_control(what, value, milliseconds);
#else
    (void)what, (void)value, (void)milliseconds;
#endif
}

void svc_audio_control(void*, uint32_t what, uint32_t value) {
    if (::galaxy::diagnosing(::galaxy::kDiagnoseFileReads)) {
        std::fprintf(stderr, "[read] sound clock %u at frame %llu\n", what, static_cast<unsigned long long>(gDeterministicFrames.load()));
    }
    {
        std::lock_guard<std::mutex> lock(gSoundClockLock);
        if (what == NEBULA_AUDIO_START) {
            gSoundClockRunning = true;
            gSoundTicksPerSecond = value;
        } else if (what == NEBULA_AUDIO_STOP) {
            gSoundClockRunning = false;
        }
        gSoundClockWake.notify_one();
    }
#if defined(NEBULA_HOST_RENDERER)
    if (what == NEBULA_AUDIO_START) {
        gAudio->start(audio_frame_tick);
    } else if (what == NEBULA_AUDIO_STOP) {
        gAudio->stop();
    }
#endif
}

int svc_home_menu(void*, uint32_t what, uint32_t value) {
#if defined(NEBULA_HOST_RENDERER)
    switch (what) {
    case NEBULA_HOME_MENU_OPEN:
        galaxy::render::home_menu_open();
        return 0;
    case NEBULA_HOME_MENU_POLL:
        return galaxy::render::home_menu_poll();
    case NEBULA_HOME_MENU_CLOSE:
        galaxy::render::home_menu_close();
        return 0;
    case NEBULA_HOME_MENU_VIEW:
        return galaxy::render::home_menu_view();
    case NEBULA_HOME_MENU_POINT:
        galaxy::render::home_menu_point(value);
        return 0;
    case NEBULA_HOME_MENU_CHOOSE:
        galaxy::render::home_menu_choose(value);
        return 0;
    default:
        return 0;
    }
#else
    (void)value;
    return what == NEBULA_HOME_MENU_VIEW ? -1 : 0;  // headless: the menu closes at once
#endif
}

// Test hooks for hidden runs; the game performs them with its own calls each scene frame.
//   GALAXY_TEST_GALAXY_TOUR=<file of "Stage scenario" lines>: each stop in turn, GALAXY_TEST_GALAXY_TOUR_FRAMES scene
//     frames apart (default 1200), through MR::requestChangeStageInGameMoving.
//   GALAXY_TEST_TELEPORT=x,y,z,frame[/x,y,z,frame...]: MR::setPlayerPos at those scene frames, held 30 frames (the
//     player settles there); counted from the latest tour stop when touring.
//   GALAXY_TEST_EVENTS=kind@frame[/kind@frame...]: once at that scene frame (from the latest stop when touring): kill (a
//     miss: MR::forceKillPlayerByAbyss), movieN (MR::startMovie(N), in a stage with a movie sequence), wait (nothing:
//     keeps a tour's last stop running until then), star / grandstar (the stop's star is collected: its result, the get
//     demo, the save and what follows, the ending after KoopaBattleVs3Galaxy).
//   GALAXY_TEST_GRAND_STARS=n: the save holds Grand Stars 1-n from the second scene frame, and the stage starts again
//     with them (the Observatory's domes restored, lit and furnished as the game shows them for that many).
//   GALAXY_TEST_EVENT_FLAGS=name[,name=value...]: the save's stored event flags on and event values set from the second
//     scene frame, before the Grand Stars (with GALAXY_TEST_GRAND_STARS>=3, LibraryOpenNewStarCount=0 opens the Library
//     and its first storybook chapter; PictureBookAlreadyRead=n: chapters 1-n read, which the Library's book lists;
//     SpinAndStarPointer: the spin and pointer the Gateway's Luma grants; Achievement:<id>: that achievement's pop-up; AchievementBoard: the
//     Observatory's achievement board).
//   GALAXY_TEST_EVENTS actors@frame: the live actors with a model, by model name and position (where to teleport),
//     with the animation they play, and whether they are shown, clipped, or drawn while clipped as shadow casters.
//   GALAXY_TEST_EVENTS shake@frame: the screen shakes from that frame until the scene ends.
//   GALAXY_TEST_EVENTS audio@frame: the sound mixer's counters then (the "host: jaudio" line, which otherwise comes in
//     the first second of every 30 game seconds).
//   GALAXY_TEST_EVENTS defeat:<model>@frame: the live actors with that model (as actors@ names it) are killed, as their
//     defeat ends: what they leave appears (KuriboChief, the Gateway's big Goomba: its key).
//   GALAXY_TEST_SCRIPT_CLOCK=frames: the scripted input (GALAXY_INPUT_*) counts scene frames instead of frames, so a
//     run's game frames take the same input whatever its renderer's speed (builds compared frame for frame).
//   GALAXY_TEST_SCRIPT_CLOCK=stop: the scripted input counts scene frames since the tour's current stop began (the clock
//     of GALAXY_TEST_EVENTS and GALAXY_TEST_TELEPORT) and gives nothing before the first stop; without a tour it is the
//     scene frames from boot, as frames is.
std::atomic<bool> gTourDone{false};
#if defined(_WIN32) && defined(NEBULA_HOST_RENDERER) && !defined(NEBULA_HOST_SDL)
HANDLE gHostWake = nullptr;
#endif

// What the host gives its extensions (nebula_host_extension.h).
void extension_log(void*, const char* line) {
    log_line("extension:", line);
}
void extension_quit(void*) {
    gTourDone.store(true, std::memory_order_relaxed);
#if defined(_WIN32) && defined(NEBULA_HOST_RENDERER) && !defined(NEBULA_HOST_SDL)
    if (gHostWake != nullptr) {
        SetEvent(gHostWake);
    }
#endif
}
#if defined(NEBULA_HOST_RENDERER)
void extension_menu_key(void*, uint32_t key) {
    static constexpr int kKeys[] = {0,         VK_UP,  VK_DOWN, VK_LEFT, VK_RIGHT, VK_RETURN,
                                    VK_ESCAPE, VK_TAB, VK_BACK, VK_SPACE, VK_F1};
    if (key != 0u && key < std::size(kKeys)) {
        galaxy::render::native_settings_post_key(static_cast<std::uintptr_t>(kKeys[key]));
    }
}
uint32_t extension_menu_open(void*) {
    if (galaxy::render::home_menu_visible()) {
        return NEBULA_EXT_MENU_HOME;
    }
    return galaxy::render::native_settings_overlay_visible() ? NEBULA_EXT_MENU_SETTINGS : NEBULA_EXT_MENU_NONE;
}
void extension_open_settings(void*, uint32_t page) {
    galaxy::render::host_window::open_native_settings(page);
}
void extension_resize_window(void*, uint32_t width, uint32_t height) {
    galaxy::render::host_window::resize_window_client(width, height);
}
#endif

void extension_take_out_mod(void*, const char* mod_id) {
    if (mod_id != nullptr) {
        galaxy::mods::take_out(mod_id);
    }
}

void attach_extensions() {
    const std::vector<fs::path>& files = ::galaxy::launch_args().extensions;
    if (files.empty()) {
        return;
    }
    static NebulaHostForExtension host{};
    host.version = NEBULA_HOST_EXTENSION_VERSION;
    host.size = sizeof(host);
    host.log = extension_log;
    host.quit = extension_quit;
    host.take_out_mod = extension_take_out_mod;
#if defined(NEBULA_HOST_RENDERER)
    host.menu_key = extension_menu_key;
    host.menu_open = extension_menu_open;
    host.open_settings = extension_open_settings;
    host.resize_window = extension_resize_window;
#endif
    std::string error;
    if (!galaxy::extensions::attach(files, host, error)) {
        die("--extension %s", error.c_str());
    }
    log_line("host:", (std::to_string(files.size()) + " extension(s) attached").c_str());
}

// The settings' galaxy select (its ADVANCED page): a travel the player asked for, through the game's own stage change,
// and the Grand Stars with the Observatory they power.
int svc_galaxy_travel(void*, char* stage, uint32_t capacity, int32_t* star) {
#if defined(NEBULA_HOST_RENDERER)
    std::size_t index = 0u;
    std::uint32_t chosen = 0u;
    if (galaxy::render::take_galaxy_travel(index, chosen)) {
        std::snprintf(stage, capacity, "%s", galaxy::render::kGalaxyStages[index].id);
        *star = static_cast<int32_t>(chosen);
        return NEBULA_GALAXY_TRAVEL_STAGE;
    }
    if (galaxy::render::take_grand_stars()) {
        std::snprintf(stage, capacity, "%s", galaxy::render::kObservatoryId);
        *star = 6;
        return NEBULA_GALAXY_TRAVEL_GRAND_STARS;
    }
#else
    (void)stage;
    (void)capacity;
    (void)star;
#endif
    return NEBULA_GALAXY_TRAVEL_NONE;
}

// A restart starts this executable again with the same arguments (the game's boot); the
// other power events close the game.
// Every way out of the game: pending saves written (bounded: a dead drive does not hold the game), counters flushed.
#if defined(_WIN32) && defined(NEBULA_HOST_RENDERER)
// One game per settings folder, so two never write the same saves. A second start brings the first's window forward and
// ends. Test runs have their own folders, so they run side by side.
HANDLE gOneGame = nullptr;

bool one_game_at_a_time() {
    std::wstring name = L"Local\\NebulaGame-";
    for (const wchar_t c : galaxy::runtime_settings_folder().wstring()) {
        name += c == L'\\' || c == L':' ? L'_' : c;
    }
    gOneGame = CreateMutexW(nullptr, TRUE, name.c_str());
    // The folder's key: the first game's window carries it, and a second start finds that window by it.
    std::uint64_t key = 1469598103934665603ull;
    for (const wchar_t c : name) {
        key = (key ^ static_cast<std::uint64_t>(c)) * 1099511628211ull;
    }
    galaxy::render::host_window::set_instance_key(key != 0u ? key : 1u);
    if (gOneGame == nullptr || GetLastError() != ERROR_ALREADY_EXISTS) {
        return false;
    }
    log_line("host:", "the game is already running");
    // Up to 3 s: the first start may be opening its window still.
    // Files it was started to import (Open with, --import) go to the running game, which imports them.
    if (!galaxy::render::host_window::show_existing_instance(key != 0u ? key : 1u, 3000u, galaxy::launch_args().imports)) {
        log_line("host:", "the running game has no window to bring forward");
    }
    return true;
}

// The launcher's sign that the game runs (Launcher.RuntimeLeaseHeld): a named mutex for the release, held by the main
// thread until the process ends.
HANDLE gRuntimeLease = nullptr;

void hold_runtime_lease() {
    std::wstring name = L"Local\\Nebula.";
    for (const char* c = gPak.id; *c != '\0'; ++c) {
        name += static_cast<wchar_t>(*c);
    }
    name += L".NativeRuntime";
    gRuntimeLease = CreateMutexW(nullptr, TRUE, name.c_str());
}
#endif

[[noreturn]] void finish_and_exit(bool restart) {
#if defined(_WIN32)
    game_profile::finish();
#endif
#if defined(NEBULA_HOST_RENDERER)
    // --diagnostics pipelines: the pipelines this run created (tools/dev/site_pipelines.py merges them).
    if (::galaxy::diagnosing(::galaxy::kDiagnosePipelines)) {
        if (const fs::path folder = galaxy::nebula_diagnostics_directory(); !folder.empty()) {
            galaxy::render::registries().write_pipeline_list(
                folder / ("pipelines-" + std::to_string(galaxy::current_process_id()) + ".bin"));
        }
    }
#endif
    log_line("host:", restart ? "restart" : "the game ended");
    {
        std::unique_lock<std::mutex> lock(gSavesMutex);
        gSavesDone.wait_for(lock, std::chrono::seconds(5), [] { return gSavesPending.load(std::memory_order_acquire) == 0; });
    }
    if (gSavesPending.load(std::memory_order_acquire) != 0) {
        log_line("host:", "a save still pending at exit");
    }
#if defined(NEBULA_HOST_RENDERER)
    galaxy::achievements::flush();
    galaxy::stats::flush();
#endif
    std::fflush(stdout);
#if defined(_WIN32)
    if (restart) {
#if defined(NEBULA_HOST_RENDERER)
        if (gOneGame != nullptr) {
            CloseHandle(gOneGame);  // the new process takes it
        }
        if (gRuntimeLease != nullptr) {
            CloseHandle(gRuntimeLease);
        }
#endif
        const std::wstring path = ::galaxy::windows::executable_path();
        if (!path.empty()) {
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            PROCESS_INFORMATION process{};
            std::wstring command = GetCommandLineW();
            command += gRestartArguments;
            if (CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
                CloseHandle(process.hThread);
                CloseHandle(process.hProcess);
            }
        }
    }
#elif defined(__ANDROID__) && defined(NEBULA_HOST_RENDERER)
    if (restart && galaxy::render::host_window::restart_game(false)) {
        // The activity ends this process once the new one is on its way.
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
#else
    (void)restart;
#endif
#if defined(_WIN32)
#if defined(NEBULA_HOST_RENDERER)
    galaxy::host::stop_all_rumble();
#endif
    // Saves and logs are out. No library's detach runs: Streamline's faults on state from before a lost card.
    TerminateProcess(GetCurrentProcess(), 0);
#endif
    std::_Exit(0);
}

void svc_app_exit(void*) {
    finish_and_exit(false);
}

void svc_mods_start(void*) {
    if (const fs::path& dump = ::galaxy::launch_args().dump_tunables; !dump.empty()) {
        const auto game_api = reinterpret_cast<const NebulaModApi* (*)()>(find_symbol(gModule.module, "nebula_game_mod_api"));
        if (game_api == nullptr || !galaxy::mods::dump_tunables(game_api(), dump)) {
            log_line("mods:", ("the tunables could not be written to " + galaxy::path_text(dump)).c_str());
        }
    }
    const fs::path crashed = galaxy::mods::mods_folder() / ".code_crashed";
    std::error_code crash_error;
    if (fs::exists(crashed, crash_error)) {
        fs::remove(crashed, crash_error);
        if (!gPlugins.empty()) {
            log_line("mods:", "the last run crashed with mods' code on: it stays off for this run");
            gPlugins.clear();
            galaxy::mods::set_code_off_after_crash();
        }
    }
    // The mod log (contract mod-log): how many mods are on, and the harness's facts in a harness run. With none it is
    // never made.
    galaxy::modlog::begin(
        [](const char* name) -> void* {
#if defined(_WIN32)
            return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(gModule.module), name));
#else
            return dlsym(gModule.module, name);
#endif
        },
        gModsOn);
    // Any mod at all tells the game: a stage then holds more than its level asks for.
    if (!gModsChangeGame) {
        return;
    }
    // A crash from here with code on leaves the code off at the next start; without, every mod (set at the scan).
    if (!gPlugins.empty()) {
        gCodeCrashFile = crashed;
    }
    const auto game_api = reinterpret_cast<const NebulaModApi* (*)()>(find_symbol(gModule.module, "nebula_game_mod_api"));
#if defined(NEBULA_HOST_RENDERER)
    galaxy::mods::set_achievement_store(galaxy::achievements::unlock_mod);
#endif
#if defined(NEBULA_HOST_RENDERER)
    galaxy::mods::set_message_language(galaxy::mods::language_folder(
        std::string(gPak.store.game_id()),
        static_cast<unsigned>(galaxy::game_language_resolved(galaxy::get_runtime_settings().language))));
#endif  // a headless host has no language setting: refusals stay in English, the default
    galaxy::mods::start_plugins(gPlugins, gModRows, game_api(),
                                [](const char* name) -> void* {
#if defined(_WIN32)
                                    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(gModule.module), name));
#else
                                    return dlsym(gModule.module, name);
#endif
                                },
                                [](const char* text) { log_line("mods:", text); });
    // Each mod whose code did not start, in the mod log as the Mods page says it.
    galaxy::modlog::add_not_started();
    // Each placed package through the validator, its findings in the mod log (kept: nothing runs when nothing changed).
    galaxy::modlog::add_start_findings();
    // The cooker's facts for the store attached this run: its refusals and notes, and in a harness run what it cooked.
    galaxy::modlog::add_cook_facts(gCookFacts);
}

void svc_app_restart(void*) {
    finish_and_exit(true);
}


}  // namespace


#if defined(NEBULA_HOST_RENDERER)
#include "galaxy/mod_import.h"

#if defined(__APPLE__)
// The importer, linked into the program on Apple (platform/apple/metal.cmake; crates/nebula-import/src/jni.rs).
extern "C" int nebula_cook_overlay(const char* plan, const char* output);
#endif

namespace {

std::string json_text(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
            out.push_back(c);
        } else if (static_cast<unsigned char>(c) < 0x20u) {
            char escaped[8];
            std::snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
            out += escaped;
        } else {
            out.push_back(c);
        }
    }
    return out + "\"";
}


// The game's language folders: the disc's, and the language packs' among the mods on at the start.
std::vector<std::string> gGameLanguages;

// The mods' disc files, cooked once into a store the game reads over its own (cook/overlay.rs): by the cooker beside
// this program, again when what they change has changed. A failure leaves the game as it is and says why in the log.
void cook_mod_files(const galaxy::mods::ModPlan& plan, const fs::path& settings_folder, const std::string& game_id);
// `mods`: the start's scan (scan_mods(game_id) with the played language set).
void prepare_mod_files(const fs::path& settings_folder, const std::string& game_id, std::vector<galaxy::mods::ModInfo> mods) {
    const std::string language = galaxy::mods::language_folder(
        game_id, static_cast<unsigned>(galaxy::game_language_resolved(galaxy::get_runtime_settings().language)));
    gStartPackages.clear();
    for (const galaxy::mods::ModInfo& mod : mods) {
        if (mod.package) gStartPackages.push_back({mod.id, mod.path, mod.enabled});
    }
    // The validator's errors refuse a package at each start as at import (one reading, validator 2); its findings go
    // to the mod log once the log exists (add_start_findings).
    galaxy::modlog::check_packages(gStartPackages, game_id, language);
    for (galaxy::mods::ModInfo& mod : mods) {
        if (mod.package && mod.problem.empty() && galaxy::modlog::package_refused(mod.id)) {
            mod.problem = galaxy::modlog::refused_sentence();
        }
    }
    gModsOn = static_cast<std::uint32_t>(
        std::count_if(mods.begin(), mods.end(), [](const galaxy::mods::ModInfo& mod) { return mod.enabled; }));
    const galaxy::mods::ModPlan plan = galaxy::mods::plan_mod_changes(gPak.store, game_id, language, std::move(mods));
    gPlugins = plan.plugins;
    gModRows = plan.rows;
    gModsChangeGame = !plan.files.empty() || !plan.merges.empty() || !plan.materials.empty() || !plan.plugins.empty() || !plan.rows.empty();
    if (!plan.save_folder.empty() && !gSaveDataPath.empty()) {
        // A clone starts from the player's save the first time, the rest starts new; the player's stays as it is.
        const fs::path mine = gSaveRoot / gSaveDataPath.substr(1);
        const fs::path theirs = plan.save_folder / gSaveDataPath.substr(1);
        const fs::path old_mine = gSaveRoot / gOldSaveDataPath.substr(1);
        const fs::path old_theirs = plan.save_folder / gOldSaveDataPath.substr(1);
        std::error_code save_error;
        if (!fs::exists(theirs, save_error)) {
            fs::create_directories(theirs, save_error);
            if (!plan.save_start.empty()) {
                fs::copy(plan.save_start, plan.save_folder, fs::copy_options::recursive | fs::copy_options::skip_existing, save_error);
            } else if (plan.save_clone) {
                fs::copy(mine, theirs, fs::copy_options::recursive | fs::copy_options::skip_existing, save_error);
                if (fs::exists(old_mine, save_error)) {
                    fs::create_directories(old_theirs, save_error);
                    fs::copy(old_mine, old_theirs, fs::copy_options::recursive | fs::copy_options::skip_existing, save_error);
                }
            }
        }
        gModSaveRoot = plan.save_folder;
        log_line("mods:", "saves go to the mod's save folder");
    }
    for (const std::string& note : plan.notes) {
        log_line("mods:", note.c_str());
    }
    cook_mod_files(plan, settings_folder, game_id);
    // What the .luma mods' materials need: what they brought, then what the game built for them.
    {
        std::error_code built_error;
        const fs::path built = settings_folder / "mods" / ".cooked" / "built.shaders";
        if (fs::is_regular_file(built, built_error)) {
            galaxy::render::load_mod_shaders(built);
        }
    }
    for (const fs::path& folder : plan.luma_folders) {
        std::error_code shaders_error;
        const fs::path shaders = folder / "shaders" / galaxy::render::program_file_name();
        if (fs::is_regular_file(shaders, shaders_error)) {
            const galaxy::render::ModShadersResult loaded = galaxy::render::load_mod_shaders(shaders);
            // Such a file adds nothing to what the cook found present: the shaders its models need are built here
            // (build_missing_mod_shaders, "shaders built here").
            if (loaded == galaxy::render::ModShadersResult::OtherInterface) {
                log_line("mods:", "its shaders are for another build of Nebula: the ones it needs are built here");
            } else if (loaded == galaxy::render::ModShadersResult::Unreadable) {
                log_line("mods:", "its shaders could not be read: the ones it needs are built here");
            }
        }
    }
}

// The cooker failed as a whole: why is in `<store>.why` (cook/overlay.rs), as it has no console to say it in.
void cook_failed(const fs::path& overlay) {
    fs::path why_file = overlay;
    why_file.replace_extension("why");
    std::ifstream in(why_file, std::ios::binary);
    std::string why((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    while (!why.empty() && (why.back() == '\n' || why.back() == '\r')) why.pop_back();
    log_line("mods:", why.empty() ? "the mods' files could not be cooked: the game runs without them"
                                  : ("the mods' files could not be cooked: the game runs without them: " + why).c_str());
}

void cook_mod_files(const galaxy::mods::ModPlan& plan, const fs::path& settings_folder, const std::string& game_id) {
    if (plan.files.empty() && plan.merges.empty()) {
        return;
    }
    const fs::path folder = settings_folder / "mods" / ".cooked";
    const fs::path overlay = folder / "mods.pak";
    const fs::path signature_file = folder / "mods.signature";
    // Another build's shader interface or shader compiler means their shaders are built again.
    const std::string signature = galaxy::mods::plan_signature(plan, game_id) + galaxy::render::program_interface() + " " +
                                  std::to_string(galaxy::render::mod_shader_compiler_revision()) + "\n";
    std::error_code error;
    fs::create_directories(folder, error);
    std::string kept;
    {
        std::ifstream in(signature_file, std::ios::binary);
        kept.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    if (kept != signature || !fs::is_regular_file(overlay, error)) {
        const auto cook_now = [plan, folder, overlay, signature_file, signature, game_id]() -> bool {
        const fs::path plan_file = folder / "mods.plan";
        if (!galaxy::io::write_file_atomically(plan_file, galaxy::mods::plan_text(plan, game_id))) {
            log_line("mods:", "the plan could not be written");
            return false;
        }
        std::error_code cook_error;
        fs::remove(signature_file, cook_error);
#if defined(_WIN32)
        const fs::path cooker = fs::path(::galaxy::windows::executable_path()).parent_path() / "nebula-import.exe";
        std::wstring command = L"\"" + cooker.wstring() + L"\" overlay \"" + plan_file.wstring() + L"\" --output \"" +
                               overlay.wstring() + L"\"";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        DWORD exit_code = 1;
        if (CreateProcessW(cooker.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                           &startup, &process)) {
            WaitForSingleObject(process.hProcess, INFINITE);
            GetExitCodeProcess(process.hProcess, &exit_code);
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
        } else {
            log_line("mods:", "the cooker (nebula-import) could not start");
        }
        if (exit_code != 0u) {
            cook_failed(overlay);
            return false;
        }
#elif defined(__APPLE__)
        // The importer is linked into the program (platform/apple/metal.cmake).
        if (nebula_cook_overlay(utf8_of(plan_file).c_str(), utf8_of(overlay).c_str()) != 0) {
            cook_failed(overlay);
            return false;
        }
#else
        // The importer's library, as the launcher loads it.
        using CookOverlay = int (*)(const char*, const char*);
        void* const library = dlopen("libnebula_import.so", RTLD_NOW | RTLD_LOCAL);
        const auto cook = library != nullptr ? reinterpret_cast<CookOverlay>(dlsym(library, "nebula_cook_overlay")) : nullptr;
        if (cook == nullptr || cook(utf8_of(plan_file).c_str(), utf8_of(overlay).c_str()) != 0) {
            cook_failed(overlay);
            return false;
        }
#endif
        // What a mod holds that couldn't be used: the rest of it goes ahead.
        {
            fs::path left_out = overlay;
            left_out.replace_extension("left_out");
            std::ifstream lines(left_out);
            std::string line;
            while (std::getline(lines, line)) {
                log_line("mods: left out:", line.c_str());
            }
        }
        {
            // What their materials need and they didn't bring.
            std::vector<fs::path> brought;
            for (const fs::path& mod : plan.luma_folders) {
                brought.push_back(mod / "shaders" / galaxy::render::program_file_name());
            }
            fs::path sources = overlay;
            sources.replace_extension("sources");
            fs::path progress = overlay;
            progress.replace_extension("progress");
            const galaxy::render::ModShaderBuild built = galaxy::render::build_missing_mod_shaders(
                sources, brought, folder / "built.shaders", [progress](unsigned done, unsigned total) {
                    std::ofstream out(progress);
                    out << done << ' ' << total << '\n';
                });
            std::error_code progress_error;
            fs::remove(progress, progress_error);
            if (built.wanted != 0u) {
                char text[96];
                std::snprintf(text, sizeof(text), "%u of %u shaders built here", built.built, built.wanted);
                log_line("mods:", text);
                if (!built.problem.empty()) {
                    log_line("mods:", built.problem.c_str());
                }
            }
        }
        if (!galaxy::io::write_file_atomically(signature_file, signature)) {
            log_line("mods:", "the signature could not be written");
        }
            return true;
        };
#if defined(NEBULA_HOST_RENDERER)
        // The game's own screen while they cook; then it starts again with them, so that they are there from its first file.
        // A hidden test run cooks first, as its measurements start from the game with its mods.
        if (!::galaxy::launch_args().started_hidden || (galaxy::extensions::flags() & NEBULA_EXT_MODS_SCREEN) != 0u) {
            gModsProgressFile = overlay;
            gModsProgressFile.replace_extension("progress");
            gModsPreparing.store(2u, std::memory_order_release);
            log_line("mods:", "preparing their files");
            std::thread([cook_now]() {
                if (cook_now()) {
                    finish_and_exit(true);
                }
                gModsPreparing.store(0u, std::memory_order_release);
            }).detach();
            return;
        }
#endif
        if (!cook_now()) {
            return;
        }
    }
    if (gPak.store.attach_overlay(overlay)) {
        gCookFacts = overlay;
        gCookFacts.replace_extension("facts");
        char text[96];
        std::snprintf(text, sizeof(text), "%zu files and %zu table or file merges applied", plan.files.size(), plan.merges.size());
        log_line("mods:", text);
    } else {
        log_line("mods:", "the mods' files could not be read: the game runs without them");
    }
}

// The game id from Setup's game.pak, read without mapping it; false (and nothing changed) for a file that is not one:
// the tools then work without it.
bool read_game_id(const fs::path& path, std::string& id) {
    std::ifstream file(path, std::ios::binary);
    char header[64];
    if (!file.read(header, sizeof(header)) || std::memcmp(header, galaxy::assets::kStoreMagic, 8) != 0) return false;
    id.assign(header + 48, 6);
    return true;
}

// The launcher's mod tools: NebulaNative --import-mod <folder or archive>..., --remove-mod <id> or --list-mods
// [--game <id>] [--pak <game.pak>], on the mods folder beside the settings (as the game uses it). One JSON line on
// stdout; exit code 0 when everything succeeded.
int run_mods_tool() {
    std::vector<fs::path> args;
#if defined(_WIN32)
    int count = 0;
    if (LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count)) {
        for (int i = 1; i < count; ++i) args.emplace_back(wide[i]);
        LocalFree(wide);
    }
#endif
    std::string game_id = "RMGE01";
    std::vector<fs::path> sources;
    const bool list = !args.empty() && args[0] == "--list-mods";
    const bool remove = !args.empty() && args[0] == "--remove-mod";
    for (std::size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--game" && i + 1u < args.size()) {
            game_id = utf8_of(args[++i]);
        } else if (args[i] == "--pak" && i + 1u < args.size()) {
            std::string id;
            if (read_game_id(args[++i], id)) {
                game_id = id;
            }
        } else {
            sources.push_back(args[i]);
        }
    }
    const fs::path settings_folder = galaxy::runtime_settings_folder();
    if (settings_folder.empty()) {
        std::printf("{\"ok\":false,\"message\":\"no settings folder\"}\n");
        return 2;
    }
    std::error_code error;
    fs::create_directories(settings_folder / "mods", error);
    galaxy::mods::set_mods_folder(settings_folder / "mods", settings_folder / "mods.ini", game_id);
    if (remove) {
        std::string message;
        const bool ok = !sources.empty() &&
                        galaxy::mods::remove_mod(settings_folder / "mods", utf8_of(sources.front()), &message);
        std::printf("{\"ok\":%s,\"message\":%s}\n", ok ? "true" : "false", json_text(message).c_str());
        return ok ? 0 : 1;
    }
    if (list) {
        std::string out = "{\"ok\":true,\"folder\":" + json_text(utf8_of(settings_folder / "mods")) + ",\"mods\":[";
        bool first = true;
        for (const galaxy::mods::ModInfo& mod : galaxy::mods::scan_mods(game_id)) {
            out += first ? "{" : ",{";
            first = false;
            out += "\"id\":" + json_text(mod.id) + ",\"enabled\":" + (mod.enabled ? "true" : "false") +
                   ",\"order\":" + std::to_string(mod.order) + ",\"riivolution\":" + (mod.riivolution ? "true" : "false") +
                   ",\"files\":" + ((mod.files || mod.loose) ? "true" : "false") + ",\"textures\":" +
                   (mod.textures ? "true" : "false") + ",\"movies\":" + (mod.movies ? "true" : "false") +
                   ",\"code_patches\":" + std::to_string(mod.code_patches) + ",\"problem\":" + json_text(mod.problem) +
                   ",\"languages\":[";
            for (std::size_t l = 0; l < mod.languages.size(); ++l) out += (l ? "," : "") + json_text(mod.languages[l]);
            out += "],\"options\":[";
            for (std::size_t o = 0; o < mod.options.size(); ++o) {
                const galaxy::mods::ModOption& option = mod.options[o];
                out += std::string(o ? "," : "") + "{\"key\":" + json_text(option.key) + ",\"section\":" +
                       json_text(option.section) + ",\"name\":" + json_text(option.name) +
                       ",\"selected\":" + std::to_string(option.selected) + ",\"choices\":[";
                for (std::size_t c = 0; c < option.choices.size(); ++c) {
                    const bool other = c < option.other_region.size() && option.other_region[c];
                    out += std::string(c ? "," : "") + "{\"name\":" + json_text(option.choices[c]) +
                           ",\"other_region\":" + (other ? "true" : "false") + "}";
                }
                out += "]}";
            }
            out += "],\"overlaps\":[";
            for (std::size_t o = 0; o < mod.overlaps.size(); ++o) {
                const galaxy::mods::ModOverlap& overlap = mod.overlaps[o];
                out += std::string(o ? "," : "") + "{\"other\":" + json_text(overlap.other) + ",\"above\":" +
                       (overlap.other_above ? "true" : "false") + ",\"files\":" + std::to_string(overlap.files) +
                       ",\"textures\":" + std::to_string(overlap.textures) +
                       ",\"settings\":" + std::to_string(overlap.settings) + "}";
            }
            out += "]}";
        }
        out += "]}";
        std::printf("%s\n", out.c_str());
        return 0;
    }
    int failed = 0;
    std::string out = "{\"results\":[";
    // The same check as the game's own imports (task 6.1, validator 2 "at import"): against the packages placed now.
    const galaxy::mods::ImportCheck check = [](const fs::path& staged, const std::string& id, galaxy::mods::ImportResult& result) {
        std::vector<galaxy::modlog::StartPackage> others;
        for (const galaxy::mods::ModInfo& mod : galaxy::mods::scan_mods()) {
            if (mod.package && mod.id != id) others.push_back({mod.id, mod.path, mod.enabled});
        }
        const galaxy::modlog::ImportCheck found = galaxy::modlog::check_import(staged, others);
        result.checked = found.ran;
        result.refused = found.refused;
        for (const galaxy::modlog::ImportFinding& f : found.findings) {
            result.findings.push_back({f.error, f.code, f.file, f.text, f.line, f.column});
        }
        return !found.refused;
    };
    for (std::size_t i = 0; i < sources.size(); ++i) {
        const galaxy::mods::ImportResult result = galaxy::mods::import_mod(sources[i], settings_folder / "mods", {}, nullptr, check);
        failed += result.ok ? 0 : 1;
        std::string findings;
        for (const galaxy::mods::ImportResult::Finding& f : result.findings) {
            findings += std::string(findings.empty() ? "" : ",") + "{\"code\":" + std::to_string(f.code) + ",\"error\":" +
                        (f.error ? "true" : "false") + ",\"file\":" + json_text(f.file) + ",\"text\":" + json_text(f.text) + "}";
        }
        out += std::string(i ? "," : "") + "{\"ok\":" + (result.ok ? "true" : "false") + ",\"id\":" + json_text(result.id) +
               ",\"replaced\":" + (result.replaced ? "true" : "false") +
               ",\"already\":" + (result.already ? "true" : "false") + ",\"title\":" + json_text(result.title) + ",\"files\":" + std::to_string(result.files) +
               ",\"bytes\":" + std::to_string(result.bytes) + ",\"kinds\":" + json_text(result.kinds) +
               ",\"message\":" + json_text(result.message) + ",\"checked\":" + (result.checked ? "true" : "false") +
               ",\"refused\":" + (result.refused ? "true" : "false") + ",\"findings\":[" + findings + "]}";
    }
    out += "],\"ok\":" + std::string(failed == 0 && !sources.empty() ? "true" : "false") + "}";
    std::printf("%s\n", out.c_str());
    return failed == 0 && !sources.empty() ? 0 : 1;
}

}  // namespace
#endif

namespace {

// The command line's launch arguments (launch_args.h, docs/COMMAND_LINE.md), before anything reads them. Paths come
// from the UTF-16 command line on Windows: argv is in the system's code page, which cannot hold every folder name.
void read_launch_args(int argc, char** argv) {
    galaxy::LaunchArgs& args = galaxy::g_launch_args;
#if defined(_WIN32)
    int wide_count = 0;
    LPWSTR* const wide = CommandLineToArgvW(GetCommandLineW(), &wide_count);
#endif
    const auto path_at = [&](int index) {
#if defined(_WIN32)
        if (wide != nullptr && index < wide_count) {
            return fs::path(wide[index]);
        }
#endif
        return fs::path(argv[index]);
    };
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string_view key = argv[i];
        if (key == "--data") {
            args.data = path_at(i + 1);
        } else if (key == "--settings") {
            args.settings = path_at(i + 1);
        } else if (key == "--crash-dumps") {
            args.crash_dumps = path_at(i + 1);
        } else if (key == "--gpu-validation") {
            args.gpu_validation = static_cast<unsigned>(std::strtoul(argv[i + 1], nullptr, 10));
        } else if (key == "--console-log") {
            args.console_log = argv[i + 1][0] == '1';
        } else if (key == "--gpu-start-retry") {
            args.gpu_start_retry = static_cast<unsigned>(std::strtoul(argv[i + 1], nullptr, 10));
        } else if (key == "--timeline") {
            args.timeline = path_at(i + 1);
        } else if (key == "--dump-tunables") {
            args.dump_tunables = path_at(i + 1);
        } else if (key == "--extension") {
            args.extensions.push_back(path_at(i + 1));
        } else if (key == "--import") {
            args.imports.push_back(path_at(i + 1));
        } else if (key == "--safe-start") {
            args.safe_start = argv[i + 1][0] == '1';
        } else if (key == "--diagnostics") {
            std::string_view unknown;
            unsigned what = 0u;
            if (!galaxy::diagnostics_from_text(argv[i + 1], what, unknown)) {
                die("--diagnostics: no diagnostic is named %.*s (docs/DIAGNOSTICS.md)", static_cast<int>(unknown.size()), unknown.data());
            }
            args.diagnostics |= what;
        }
    }
#if defined(_WIN32)
    if (wide != nullptr) {
        LocalFree(wide);
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    GetStartupInfoW(&startup);
    args.started_hidden = (startup.dwFlags & STARTF_USESHOWWINDOW) != 0u && startup.wShowWindow == SW_HIDE;
#endif
}

}  // namespace

int main(int argc, char** argv) {
#if defined(NEBULA_HOST_APPLE)
    // An app has no command line: the app's own layout names the game, the saves and the module.
    galaxy::apple::launch_arguments(argc, argv);
#endif
#if defined(NEBULA_HOST_NATIVE_APPLE)
    // The first start: the player's disc image becomes the game's data, or the player leaves.
    if (!galaxy::apple::ensure_game_data(argc, argv)) {
        return 0;
    }
#endif
    read_launch_args(argc, argv);
#if defined(NEBULA_HOST_RENDERER) && defined(_WIN32)
    // A tool's hidden start keeps off the player's data (hidden_start.h), before anything is written.
    {
        int count = 0;
        LPWSTR* const wide = CommandLineToArgvW(GetCommandLineW(), &count);
        const char* const error =
            wide != nullptr ? galaxy::host::hidden_start_error(::galaxy::launch_args().started_hidden, count, wide)
                            : (::galaxy::launch_args().started_hidden ? "cannot read the command line" : nullptr);
        if (wide != nullptr) {
            LocalFree(wide);
        }
        if (error != nullptr) {
            std::fprintf(stderr, "[hidden-start] %s\n", error);
            return 2;
        }
    }
#endif
#if defined(NEBULA_HOST_RENDERER) && defined(_WIN32)
    if (argc >= 3 && std::strcmp(argv[1], "--crash-report") == 0) {
        return show_crash_report_dialog();
    }
    if (one_game_at_a_time()) {
        return 0;
    }
#endif
#if defined(NEBULA_HOST_SDL)
    if (!galaxy::sdl::start()) {
        return 1;
    }
#endif
#if defined(NEBULA_HOST_RENDERER) && defined(_WIN32)
    // A player's start has nowhere to print (a launcher with no terminal, or none): the log goes to logs/latest.log beside the
    // settings, the last start's to previous.log. A test or a shell capturing the output to a pipe or a file keeps it.
    {
        const DWORD output = GetFileType(GetStdHandle(STD_OUTPUT_HANDLE));
        if (output != FILE_TYPE_PIPE && output != FILE_TYPE_DISK && !::galaxy::launch_args().console_log) {
            const fs::path settings = galaxy::runtime_settings_folder();
            std::error_code error;
            if (!settings.empty() && fs::create_directories(settings / "logs", error) | fs::is_directory(settings / "logs", error)) {
                fs::rename(settings / "logs" / "latest.log", settings / "logs" / "previous.log", error);
                FILE* log = nullptr;
                if (_wfreopen_s(&log, (settings / "logs" / "latest.log").c_str(), L"w", stdout) == 0) {
                    _dup2(_fileno(stdout), _fileno(stderr));
                }
            }
        }
    }
#endif
#if defined(NEBULA_HOST_RENDERER) && defined(_WIN32)
    // --diagnostics timeline: the frame pipeline's events (frame_timeline.h), written at exit.
    galaxy::timeline::initialize();
#endif
#if defined(NEBULA_HOST_RENDERER) && defined(_WIN32)
    // Physical-pixel DPI awareness before any window (sizes and mouse coordinates in pixels), and CRT assertion and
    // error reports to stderr instead of dialogs.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX | SEM_NOGPFAULTERRORBOX);
#endif
#if defined(_WIN32)
    install_crash_reporter();
#endif
#if defined(NEBULA_HOST_RENDERER)
    if (argc >= 2 && (std::strcmp(argv[1], "--import-mod") == 0 || std::strcmp(argv[1], "--list-mods") == 0 ||
                      std::strcmp(argv[1], "--remove-mod") == 0)) {
        return run_mods_tool();
    }
#endif
    if (::galaxy::launch_args().started_hidden) {
        log_line("host:", "started hidden");
    }
    fs::path pak, saves, module;
    // A path in the command line: argv is in the system's code page, which cannot hold every folder name.
    const auto arg_path = [&](int index) {
#if defined(_WIN32)
        int count = 0;
        if (LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count)) {
            fs::path path = index < count ? fs::path(wide[index]) : fs::path(argv[index]);
            LocalFree(wide);
            return path;
        }
#endif
        return fs::path(argv[index]);
    };
    const auto default_module = [&] {
#if defined(_WIN32)
        return arg_path(0).parent_path() / "smg_game.dll";
#elif defined(__APPLE__)
        return fs::path(argv[0]).parent_path() / "libsmg_game.dylib";
#else
        return fs::path(argv[0]).parent_path() / "libsmg_game.so";
#endif
    };
    // --luma-describe <file> [--module <path>]: the module's type rows as JSON (contract types), compared by the build
    // with the generator's (tools/lumacore/compare_types.py). No disc; the module is attached with its heap, log and fatal
    // errors (to stderr) only, as its malloc is the host's and its constructors run at attach.
    if (argc >= 2 && std::strcmp(argv[1], "--luma-describe") == 0) {
        // Exactly `<file>` or `<file> --module <path>`: anything else is said, never ignored (A-LD1).
        if (!(argc == 3 || (argc == 5 && std::strcmp(argv[3], "--module") == 0))) {
            die("usage: --luma-describe <file> [--module <path>] (docs/COMMAND_LINE.md)");
        }
        const fs::path out = arg_path(2);
        const fs::path from = argc == 5 ? arg_path(4) : default_module();
        load_module(from);
        static NebulaHostServices bare{};
        bare.version = NEBULA_GAME_API_VERSION;
        bare.size = sizeof(bare);
        bare.log = [](void*, const char* text) { std::fprintf(stderr, "%s\n", text); };
        bare.fatal = [](void*, const char* text) {
            std::fprintf(stderr, "luma-describe: the game module stopped: %s\n", text);
            std::fflush(stderr);
            std::_Exit(3);
        };
        bare.heap_alloc = [](void*, size_t size) -> void* { return std::malloc(size); };
        bare.heap_realloc = [](void*, void* data, size_t size) -> void* { return std::realloc(data, size); };
        bare.heap_free = [](void*, void* data) { std::free(data); };
        if (gModule.attach(&bare) != NEBULA_GAME_API_VERSION) {
            die("the game module's API version differs from the host's (%u)", NEBULA_GAME_API_VERSION);
        }
        const auto describe =
            reinterpret_cast<decltype(&nebula_game_luma_describe)>(find_symbol(gModule.module, "nebula_game_luma_describe"));
        std::ofstream file(out, std::ios::binary | std::ios::trunc);
        describe([](void* user, const char* text, uint32_t length) { static_cast<std::ofstream*>(user)->write(text, length); },
                 &file);
        file.close();
        if (!file) {
            std::fprintf(stderr, "luma-describe: %s could not be written\n", utf8_of(out).c_str());
            return 2;
        }
        return 0;
    }
    attach_extensions();
    boot_mark("main");
    double seconds = 0;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string key = argv[i];
        if (key == "--pak") {
            pak = arg_path(i + 1);
        } else if (key == "--saves") {
            saves = arg_path(i + 1);
        } else if (key == "--module") {
            module = arg_path(i + 1);
        } else if (key == "--diagnostics" || key == "--timeline" || key == "--data" || key == "--settings" ||
                   key == "--crash-dumps" || key == "--gpu-validation" || key == "--console-log" ||
                   key == "--gpu-start-retry" || key == "--dump-tunables" || key == "--extension" || key == "--import" ||
                   key == "--safe-start") {
            // Read above, before anything uses them.
        } else if (key == "--seconds") {
            seconds = std::atof(argv[i + 1]);
        } else {
            die("unknown option %s", key.c_str());
        }
    }
    if (pak.empty() || saves.empty()) {
        die("usage: nebula_native_host --pak <game.pak> --saves <dir> [--module <smg_game.dll>] [--seconds N] (docs/COMMAND_LINE.md)");
    }
    if (module.empty()) {
        module = default_module();
    }
    gSaveRoot = saves;
    std::error_code error;
    if (!open_pak(pak)) return 2;
    boot_mark("disc opened");
    load_module(module);
    boot_mark("game module loaded");
#if defined(NEBULA_HOST_RENDERER)
    galaxy::render::set_texture_pack_game_id(gPak.id);  // packs' and dumps' game folders: the disc's release
#if defined(_WIN32)
    hold_runtime_lease();
#endif
    {
        // Mii import: the files in the import folder (next to the settings: any Mii files, mii_formats.h)
        // join the library before the game reads it; the settings import more while it runs.
        const fs::path settings_folder = galaxy::runtime_settings_folder();
        const fs::path import_folder = settings_folder.empty() ? fs::path{} : settings_folder / "miis";
        if (!import_folder.empty()) {
            fs::create_directories(import_folder, error);
        }
        galaxy::host::set_mii_library(mii_library_path(), console_mii_database_path(), import_folder,
                                      [](galaxy::host::MiiRecord& mii) {
                                          galaxy::host::fix_wii_mii(mii);
                                          const NebulaMii fields = nebula_mii(galaxy::host::decode_wii_mii(mii));
                                          return gModule.mii_valid(&fields) != 0u;
                                      });
        std::string message;
        (void)galaxy::host::import_mii_folder(&message);
        std::fprintf(stderr, "[mii] import folder %s: %s (%u in the library)\n", utf8_of(import_folder.filename()).c_str(),
                     message.c_str(), galaxy::host::mii_library_count());
        (void)galaxy::host::take_mii_installs();  // in the file the game reads at boot
    }
#if defined(NEBULA_HOST_RENDERER)
    // The player's settings (in --settings' folder when given) before the mods: the language they choose
    // picks the mods' text (and is the game's language from then on).
    try {
        galaxy::load_runtime_settings_from_file();
    } catch (const std::exception& failure) {
        die("the settings did not load: %s", failure.what());
    }
#endif
    {
        // Mods: the mods folder next to the settings (galaxy::mods), applied before the game reads its disc.
        const fs::path settings_folder = galaxy::runtime_settings_folder();
        const std::string game_id(gPak.store.game_id());
        const galaxy::saves::SaveFolders save_folders = galaxy::saves::save_folders(std::string_view(gPak.id, 4));
        gOldSaveDataPath = save_folders.earlier;
        gSaveDataPath = save_folders.current;
        fs::create_directories(gSaveRoot / gSaveDataPath.substr(1), error);  // the release's own save folder
        if (!settings_folder.empty()) {
            fs::create_directories(settings_folder / "mods", error);
            // Updates imported while their mod's folder was in use take its place before anything reads the mods.
            galaxy::mods::apply_pending_updates(settings_folder / "mods");
            galaxy::mods::set_mods_folder(settings_folder / "mods", settings_folder / "mods.ini", game_id);
            galaxy::mods::set_mods_store_file(gPak.store_file);
            // One scan for the start, in the language played (a .luma's layers and text follow it): the mods
            // mods.ini has on give their packs and movies from the first frame, and the same list is planned. The
            // language played is one the disc or a language pack on has (a Japanese disc plays Japanese unless a
            // pack brings the chosen one): the disc's are known first, a pack's after the scan finds it.
            std::vector<std::string> game_languages = gPak.store.language_folders();
#if defined(NEBULA_HOST_RENDERER)
            galaxy::set_game_languages(game_languages);
#endif
            const auto played = [] {
                return static_cast<unsigned>(galaxy::game_language_resolved(galaxy::get_runtime_settings().language));
            };
            const unsigned scanned_in = played();
            galaxy::mods::set_played_language(scanned_in);
            std::vector<galaxy::mods::ModInfo> mods = galaxy::mods::scan_mods(game_id);
            // Safe start (task 6.6): every mod off for this start only (the copy planned; mods.ini stays the player's).
            bool safe_asked = ::galaxy::launch_args().safe_start;
#if defined(_WIN32)
            safe_asked = safe_asked || (!::galaxy::launch_args().started_hidden && (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0);
#endif
            if (const galaxy::mods::SafeStart safe = galaxy::mods::take_safe_start(safe_asked);
                safe != galaxy::mods::SafeStart::kNo) {
                log_line("mods:", safe == galaxy::mods::SafeStart::kAsked
                                      ? "safe start: every mod is off for this start"
                                      : "safe start: the last run crashed with mods on; every mod is off for this start");
                // The harness's own game half (luma-test, placed by tools/lumacore/luma_test.py in a harness run) is the
                // test's instrument, not a player's mod: it stays on, or no safe start could be run as a test.
#if defined(_WIN32)
                const bool harness_run = GetEnvironmentVariableA("LUMA_HARNESS_PLAN", nullptr, 0) != 0;
#else
                const bool harness_run = std::getenv("LUMA_HARNESS_PLAN") != nullptr;
#endif
                for (galaxy::mods::ModInfo& mod : mods) {
                    if (!(harness_run && mod.id == "luma-test")) mod.enabled = false;
                }
            } else if (std::any_of(mods.begin(), mods.end(), [](const galaxy::mods::ModInfo& mod) { return mod.enabled; })) {
                // A crash from here on (the cook, the packs, the game) makes the next start safe; svc_mods_start
                // narrows it to the code when code is on.
                gCodeCrashFile = galaxy::mods::safe_start_file();
            }
            bool added_language = false;
            for (const galaxy::mods::ModInfo& mod : mods) {
                if (!mod.enabled) continue;
                for (const std::string& language : mod.languages) {
                    if (std::find(game_languages.begin(), game_languages.end(), language) == game_languages.end()) {
                        game_languages.push_back(language);
                        added_language = true;
                    }
                }
            }
#if defined(NEBULA_HOST_RENDERER)
            if (added_language) {
                galaxy::set_game_languages(game_languages);
                if (played() != scanned_in) {
                    galaxy::mods::set_played_language(played());
                    mods = galaxy::mods::scan_mods(game_id);
                }
            }
#else
            (void)added_language;
#endif
            gGameLanguages = game_languages;
            galaxy::render::set_texture_pack_mod_folders(galaxy::mods::texture_folders(mods),
                                                         galaxy::mods::texture_folder_categories_off(mods));
            galaxy::render::set_movie_mod_folders(galaxy::mods::movie_folders(mods));
            prepare_mod_files(settings_folder, game_id, std::move(mods));
        }
        {
            // The session's mods, for crash reports: one line each, and whether one changes the game itself.
            std::string lines;
            bool change_game = false;
            for (const galaxy::mods::ModInfo& mod : galaxy::mods::session_mods().started_with) {
                if (!mod.enabled) continue;
                std::string kinds;
                for (const auto& [on, name] : {std::pair{mod.riivolution, "Riivolution"},
                                               std::pair{mod.files || mod.loose, "game files"},
                                               std::pair{mod.textures, "textures"}, std::pair{mod.movies, "movies"}}) {
                    if (on) kinds += kinds.empty() ? name : std::string(", ") + name;
                }
                lines += "  " + mod.id + (kinds.empty() ? "" : " (" + kinds + ")") + "\n";
                change_game = change_game || mod.riivolution || mod.files || mod.loose;
            }
            galaxy::crash::set_session_mods(lines, change_game);
        }
        {
            // The game's version and its languages (the disc's and the language packs' on), which the language
            // setting offers.
            const std::vector<std::string> languages = gGameLanguages.empty() ? gPak.store.language_folders() : gGameLanguages;
            std::string list;
            for (const std::string& language : languages) list += (list.empty() ? "" : ", ") + language;
            const std::string text = game_id + " revision " + std::to_string(gPak.store.revision()) + ": " +
                                     (list.empty() ? std::string("no language folders") : list);
            log_line("disc:", text.c_str());
#if defined(NEBULA_HOST_RENDERER)
            galaxy::set_game_languages(languages);
#endif
        }
    }
#endif

    boot_mark("settings and mods");
    static NebulaHostServices services{};
    services.version = NEBULA_GAME_API_VERSION;
    services.size = sizeof(services);
    services.log = svc_log;
    services.fatal = svc_fatal;
    services.heap_alloc = [](void*, size_t size) -> void* { return std::malloc(size); };
    services.heap_realloc = [](void*, void* data, size_t size) -> void* { return std::realloc(data, size); };
    services.heap_free = [](void*, void* data) { std::free(data); };
    services.movie_frame = svc_movie_frame;
    services.presence = svc_presence;
    services.achievement = svc_achievement;
    services.achievement_date = svc_achievement_date;
    services.mod_achievement_count = svc_mod_achievement_count;
    services.mod_achievement = svc_mod_achievement;
    services.mod_achievement_unlocked = svc_mod_achievement_unlocked;
    services.stat_add = svc_stat_add;
    services.stat_get = svc_stat_get;
    services.settings_view = svc_settings_view;
    services.settings_input = svc_settings_input;
    services.sharp_2d = svc_sharp_2d;
    services.button_icons = svc_button_icons;
    services.layer_2d_lines = svc_layer_2d_lines;
    services.pointer_wheel = svc_pointer_wheel;
    services.galaxy_select_stage = svc_galaxy_select_stage;
    services.galaxy_select_names_wanted = svc_galaxy_select_names_wanted;
    services.galaxy_select_name = svc_galaxy_select_name;
    services.take_pause_request = svc_take_pause_request;
    // A mod's request screen pauses the game as Start does (services-mods 9, services-save 8).
    galaxy::mods::set_screen_pause([] { gPauseRequest.store(true, std::memory_order_release); });
    services.test_frame_step = deterministic_mode() ? svc_test_frame_step : nullptr;
    services.texture_hd = svc_texture_hd;
    services.model_register = svc_model_register;
    services.model_free = svc_model_free;
    services.mesh_register = svc_mesh_register;
    services.mesh_free = svc_mesh_free;
    services.texture_register = svc_texture_register;
    services.texture_update = svc_texture_update;
    services.texture_free = svc_texture_free;
    services.texture_target = svc_texture_target;
    services.texture_table = svc_texture_table;
    services.material_register = svc_material_register;
    services.pipeline_register = svc_pipeline_register;
    services.submit_frame = svc_submit_frame;
    services.probe_results = svc_probe_results;
    services.mii_count = svc_mii_count;
    services.mii_record = svc_mii_record;
    services.clock_ns = svc_clock_ns;
    services.calendar_ns = svc_calendar_ns;
    services.asset_find = svc_asset_find;
    services.asset_find_game = svc_asset_find_game;
    services.asset_entry = svc_asset_entry;
    services.asset_child = svc_asset_child;
    services.asset_added_child = svc_asset_added_child;
    services.asset_read = svc_asset_read;
    services.memory_reserve = svc_memory_reserve;
    services.memory_commit = svc_memory_commit;
    services.memory_decommit = svc_memory_decommit;
    services.memory_release = svc_memory_release;
    services.asset_map = svc_asset_map;
    services.asset_map_private = svc_asset_map_private;
    services.asset_unmap = svc_asset_unmap;
    services.game_id = svc_game_id;
    services.frame_wait = svc_frame_wait;
    services.frame_begun = svc_frame_begun;
    services.save_request = svc_save_request;
    services.system_setting = svc_system_setting;
    services.input_read = svc_input_read;
    services.input_control = svc_input_control;
    services.speaker_samples = svc_speaker_samples;
    services.input_players = svc_input_players;
    services.voices_setup = svc_audio_setup;
    services.voices_mix = svc_audio_mix;
    services.voices_frame = svc_audio_frame;
    services.movie_audio = svc_movie_audio;
    services.movie_audio_position = svc_movie_audio_position;
    services.movie_audio_control = svc_movie_audio_control;
    services.sound_clock = svc_audio_control;
    services.home_menu = svc_home_menu;
    services.game_context = svc_game_context;
    services.app_exit = svc_app_exit;
    services.app_restart = svc_app_restart;
    services.mods_start = svc_mods_start;
    services.galaxy_travel = svc_galaxy_travel;
    services.display_extent = svc_display_extent;
    services.option = svc_option;
    services.game_stage = svc_game_stage;
#if defined(NEBULA_HOST_RENDERER)
    // The display configuration (in --settings' folder when given), loaded before the window; the settings overlay
    // reads its frame art from the game files.
    try {
        galaxy::render::load_render_config_from_file();
    } catch (const std::exception& failure) {
        die("the settings did not load: %s", failure.what());
    }
    // Discord: never from a hidden start (a test), which must leave the player's status alone.
    if (!::galaxy::launch_args().started_hidden) {
        galaxy::discord::set_enabled_provider([] { return galaxy::get_runtime_settings().discord_status; });
        galaxy::discord::start(kDiscordClientId);
    }
    const galaxy::render::RenderConfig render_config = galaxy::render::get_render_config();
    if (!galaxy::render::initialize(static_cast<int>(render_config.window_width),
                                    static_cast<int>(render_config.window_height))) {
        const std::string reason = galaxy::render::startup_failure();
        if (retry_start_for_gpu_memory(reason)) {
            finish_and_exit(true);
        }
        die("the renderer did not start%s%s", reason.empty() ? "" : ": ", reason.c_str());
    }
    boot_mark("renderer started");
    // While the start runs ahead, the frames' presents alone reach the screen (svc_frame_started).
    galaxy::render::set_frame_end_presents(!gBootFast.load(std::memory_order_relaxed));
    gAudio = new galaxy::host::NativeGameAudio();  // lives as long as the process (the audio clock is detached)
    // The live controls' links to the window: the mouse as the pointer, keyboard capture and controller navigation
    // while the settings overlay or the HOME Menu is open.
    galaxy::host::install_host_pointer_provider([]() noexcept {
        galaxy::host::HostPointerState state{};
        const galaxy::render::HostPointerState pointer = galaxy::render::poll_host_pointer_state();
        state.window_focused = pointer.window_focused;
        state.inside_client = pointer.inside_client;
        state.absolute_valid = pointer.absolute_valid && pointer.client_width > 1 && pointer.client_height > 1;
        state.left_button = pointer.left_button;
        state.right_button = pointer.right_button;
        state.client_x = pointer.client_x;
        state.client_y = pointer.client_y;
        state.client_width = pointer.client_width;
        state.client_height = pointer.client_height;
        state.absolute_sequence = pointer.absolute_sequence;
        state.debug_window = pointer.debug_window;
        return state;
    });
    galaxy::host::install_host_pointer_transition_provider(
        [](std::uint64_t after) noexcept { return galaxy::render::poll_host_pointer_transition(after); });
    galaxy::host::install_native_keyboard_capture_provider([]() noexcept { return galaxy::render::native_window_input_captured(); });
    galaxy::host::install_native_settings_pad_hooks(
        []() noexcept { return galaxy::render::native_settings_overlay_visible() || galaxy::render::home_menu_visible(); },
        [](std::uintptr_t key) noexcept { galaxy::render::native_settings_post_key(key); });
#endif
    if (gModule.attach(&services) != NEBULA_GAME_API_VERSION) {
        die("the game module's API version differs from the host's (%u)", NEBULA_GAME_API_VERSION);
    }

    name_thread(L"Nebula Host Main");
    gFs.start();
#if !defined(NEBULA_HOST_RENDERER)
    std::thread(sound_clock_thread).detach();
#endif
#if defined(NEBULA_HOST_RENDERER)
    std::thread(input_thread).detach();
#endif
#if defined(NEBULA_HOST_SDL)
    galaxy::sdl::start_thread_sampler();
#endif
    std::thread game([] {
        name_thread(L"Nebula Game");
        galaxy::host::perf_hint_add_current_thread();
        gModule.run();
    });
#if defined(_WIN32)
    if (::galaxy::diagnosing(::galaxy::kDiagnoseProfile)) {
        game_profile::start(GetThreadId(static_cast<HANDLE>(game.native_handle())));
    }
    // The simulation thread's CPU time, reported each second (the game and everything it calls on that thread).
    HANDLE game_thread = nullptr;  // its own handle: detach() closes the thread object's
    DuplicateHandle(GetCurrentProcess(), static_cast<HANDLE>(game.native_handle()), GetCurrentProcess(), &game_thread,
                    THREAD_QUERY_LIMITED_INFORMATION, FALSE, 0);
    uint64_t game_cycles_last = 0;
    const bool trace_thread_cpu = ::galaxy::diagnosing(::galaxy::kDiagnoseThreadCpu);
    // Thread cycles count at the time-stamp counter's rate, measured here against the performance counter.
    const double tsc_hz = !trace_thread_cpu ? 0.0 : [] {
        LARGE_INTEGER f, a, b;
        QueryPerformanceFrequency(&f);
        QueryPerformanceCounter(&a);
        const uint64_t t0 = __rdtsc();
        do {
            QueryPerformanceCounter(&b);
        } while (b.QuadPart - a.QuadPart < f.QuadPart / 20);
        return (double)(__rdtsc() - t0) * (double)f.QuadPart / (double)(b.QuadPart - a.QuadPart);
    }();
#endif
    game.detach();
    boot_mark("game started");

    const auto end = Clock::now() + std::chrono::duration<double>(seconds > 0 ? seconds : 1e9);
    bool first_frame = false;
    uint32_t reported = 0;
    auto next_report = Clock::now() + std::chrono::seconds(1);
#if defined(_WIN32) && defined(NEBULA_HOST_RENDERER) && !defined(NEBULA_HOST_SDL)
    gHostWake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    galaxy::render::host_window::set_state_wake(gHostWake);
#endif
    while (Clock::now() < end && !gTourDone.load(std::memory_order_relaxed)) {
#if defined(NEBULA_HOST_RENDERER)
        // The window's messages, on the thread that created it.
#if defined(NEBULA_HOST_SDL)
        galaxy::sdl::pump_events(4);
        if (const bool away = galaxy::sdl::in_background(); away != gSuspended) {
            {
                std::lock_guard<std::mutex> lock(gSuspendLock);
                gSuspended = away;
            }
            gSuspendWake.notify_all();
            gAudio->set_paused(away);
            log_line("host:", away ? "suspended (out of sight)" : "resumed");
        }
#else
        // Until a message, a change of the window or the game's end (gHostWake), or the once-a-second report.
        {
            const auto until = std::chrono::duration_cast<std::chrono::milliseconds>(next_report - Clock::now()).count();
            const DWORD wait = first_frame ? static_cast<DWORD>(std::clamp<long long>(until + 1, 0, 1000)) : 5u;
            MsgWaitForMultipleObjects(1, &gHostWake, FALSE, wait, QS_ALLINPUT);
        }
        // Minimised: the game and its sound stop, as on the platforms that suspend, and the renderer presents nothing of its
        // own. The game's clock starts again from the return (svc_frame_wait: no step is owed), and the device is kept, so
        // showing the window again rebuilds nothing. A covered window is not this: the game and its sound go on, and the
        // renderer alone stops presenting (RenderCore, which looks at the window itself every 100 ms).
        // A window that is not in front, by the player's choice (ON LOSING FOCUS): PAUSE has the game open its pause menu (the
        // pause button's path, so its timers stop), MUTE silences the sound (the mixer reads background_muted) and the game
        // goes on; KEEP RUNNING does nothing. Minimising holds the game whatever is chosen.
        {
            static bool was_away = false;
            static bool away_minimised = false;
            static Clock::time_point lapse{};
            const galaxy::FocusLoss focus = galaxy::get_runtime_settings().focus_loss;
            const bool minimised_now = galaxy::render::host_window::is_minimized();
            const bool away = galaxy::render::host_window::is_background() || minimised_now;
            if (away && !was_away && focus == galaxy::FocusLoss::Pause) {
                gPauseRequest.store(true, std::memory_order_release);
            }
            if (away) {
                away_minimised = away_minimised || minimised_now;
            }
            if (!away && was_away) {
                if (!away_minimised) {
                    gPauseRequest.store(false, std::memory_order_release);
                } else {
                    // Held while minimised: the game takes the request at its first frame back; a game that cannot pause then
                    // (a cutscene) lets it lapse rather than pause long after the player returned.
                    lapse = Clock::now() + std::chrono::seconds(3);
                }
                away_minimised = false;
            }
            if (focus != galaxy::FocusLoss::Pause && !away_minimised) {
                gPauseRequest.store(false, std::memory_order_release);
            }
            if (!away && lapse != Clock::time_point{} && Clock::now() >= lapse) {
                gPauseRequest.store(false, std::memory_order_release);
                lapse = Clock::time_point{};
            }
            if (away != was_away) {
                log_line("host:", away ? "window not in front" : "window in front");
            }
            was_away = away;
            galaxy::set_background_muted(away && focus == galaxy::FocusLoss::Mute);
            // Behind another window (not minimised, which holds the game): the renderer shows the game's own frames only.
            galaxy::render::set_window_background(away && !minimised_now);
            apply_power_wish(away, minimised_now);
        }
        const bool minimised = galaxy::render::host_window::is_minimized();
        if (minimised != gSuspended) {
            galaxy::render::set_window_hidden(minimised);
            {
                std::lock_guard<std::mutex> lock(gSuspendLock);
                gSuspended = minimised;
            }
            gSuspendWake.notify_all();
            gAudio->set_paused(minimised);
            log_line("host:", minimised ? "suspended (minimised)" : "resumed");
        }
#endif
        galaxy::render::pump_messages();
        if (galaxy::render::quit_requested()) {
            break;
        }
        if (!first_frame && gSubmissions.load(std::memory_order_relaxed) != 0u) {
            first_frame = true;
            boot_mark("first game frame");
        }
        if (Clock::now() < next_report) {
            continue;
        }
#else
        std::this_thread::sleep_until(next_report);
#endif
        next_report += std::chrono::seconds(1);
        const uint32_t frames = gGameFrames.load(std::memory_order_relaxed);
        if (frames != reported) {
            char text[384];
            std::snprintf(text, sizeof(text), "frame %u, file reads %u, save ops %u, sound blocks %u, sound subframes %u",
                          frames,
                          gFileReads.load(std::memory_order_relaxed), gSaveOps.load(std::memory_order_relaxed),
                          gSoundBlocks.load(std::memory_order_relaxed), gSoundSubframes.load(std::memory_order_relaxed));
#if defined(NEBULA_HOST_RENDERER)
            if (frames / galaxy::kGameStepsPerSecond % 30 == 0) {
                log_line("host:", ("jaudio " + gAudio->stats_line()).c_str());
            }
            const size_t length = std::strlen(text);
            std::snprintf(text + length, sizeof(text) - length, ", renderer frames %llu",
                          (unsigned long long)gSubmissions.load(std::memory_order_relaxed));
#endif
#if defined(_WIN32)
            if (trace_thread_cpu) {
                report_thread_cpu(tsc_hz);
            }
            ULONG64 cycles = 0;
            if (trace_thread_cpu && QueryThreadCycleTime(game_thread, &cycles)) {
                const size_t used = std::strlen(text);
                std::snprintf(text + used, sizeof(text) - used, ", game cpu %.1f ms/s",
                              (double)(cycles - game_cycles_last) * 1000.0 / tsc_hz);
                game_cycles_last = cycles;
            }
#endif
            log_line("host:", text);
            reported = frames;
        }
    }
    log_line("host:", Clock::now() < end ? "the window was closed" : "time limit reached");
    log_line("host:", ("3D scenes " + std::to_string(gSceneBegins.load(std::memory_order_relaxed))).c_str());
#if defined(NEBULA_HOST_RENDERER)
    galaxy::loss::report();
#endif
#if defined(NEBULA_HOST_RENDERER) && defined(_WIN32)
    galaxy::timeline::dump();
#endif
    finish_and_exit(false);
}
