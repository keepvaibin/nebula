// The native game's host (docs/NATIVE_PORT.md, "Module layout"): loads the game module (smg_game.dll), gives it
// Nebula's services (nebula_game_api.h) and runs it. Built three ways:
//   - nebula_native_host (native/CMakeLists.txt): headless. The GX stream is released as it arrives and PE events
//     answered at once; audio is paced but not played; no remotes connect.
//   - NebulaNative (the top-level build, NEBULA_HOST_RENDERER): the stream goes to Nebula's D3D12 renderer, which
//     raises the PE events and presents the frame buffer the game displays.
//   - On SDL platforms (platform/sdl, NEBULA_HOST_SDL): the same with the Vulkan renderer, SDL's window, sound and
//     input; on Android SDL's activity calls SDL_main.
//
//   <host> --pak <game.pak> --nand <dir> [--module <smg_game.dll>] [--seconds N]
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif
#include <nebula_game_api.h>

#include "galaxy/atomic_file.h"
#include "galaxy/disc_files.h"
#include "galaxy/save_files.h"
#include "galaxy/crash_report.h"
#include "galaxy/discord_presence.h"
#include "galaxy/mii_formats.h"
#include "galaxy/mii_import.h"
#include "galaxy/mods.h"
#if defined(NEBULA_HOST_RENDERER)
#include "galaxy/experimental_ultrawide_aspect.h"
#include "galaxy/frame_timeline.h"
#include "galaxy/renderer_api.h"
#include "galaxy/native_host.h"
#include "galaxy/native_input.h"
#include "galaxy/native_thp_video.h"
#include "galaxy/perf_hint.h"
#include "galaxy/platform_time.h"
#include "galaxy/pointer_control.h"
#include "galaxy/achievements.h"
#include "galaxy/stats.h"
#if defined(_WIN32) && !defined(NEBULA_HOST_SDL)
// A real Wii Remote over Windows HID (wiimote_hid.h).
#define NEBULA_REAL_WIIMOTE 1
#include "galaxy/input/wiimote_input.h"
#include "galaxy/wiimote_hid.h"
#endif
#include "galaxy/runtime_settings.h"
#include "galaxy/render/curved_surfaces.h"
#include "galaxy/render/native_shapes.h"
#include "galaxy/render/galaxy_select.h"
#include "galaxy/render/render_config.h"
#include "galaxy/render/rt_materials.h"
#include "galaxy/render/texture_pack.h"
#if defined(NEBULA_HOST_D3D12)
#include "galaxy/render/rt_prepare.h"
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
inline int64_t perf_hint_retrace() noexcept { return 0; }
}  // namespace galaxy::host
#endif
#if defined(NEBULA_HOST_SDL)
#include "sdl_host.h"

#include <SDL3/SDL_main.h>
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
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
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
// retrace, the audio interface and the remotes keep their periods to well under a millisecond.
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

void log_line(const char* tag, const char* text) {
    const double t = std::chrono::duration<double>(Clock::now() - kStart).count();
    std::fprintf(stdout, "[%9.3f] %s %s\n", t, tag, text);
    std::fflush(stdout);
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

void gather_crash_facts(galaxy::crash::Facts::Kind kind, const char* text, bool in_game = false,
                        bool stack_overflow = false) noexcept {
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

// Crash dumps and reports: GALAXY_CRASH_DUMP_DIR, else %LOCALAPPDATA%\Nebula\crashes for the player's executable and
// crashes\ in the working directory for the headless test host.
fs::path crash_folder() {
#if defined(_WIN32)
    wchar_t dir[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"GALAXY_CRASH_DUMP_DIR", dir, MAX_PATH) != 0) return fs::path(dir);
#if defined(NEBULA_HOST_RENDERER)
    wchar_t local[MAX_PATH] = {};
    GetEnvironmentVariableW(L"LOCALAPPDATA", local, MAX_PATH);
    return fs::path(local) / "Nebula" / "crashes";
#endif
#else
    if (const char* dir = std::getenv("GALAXY_CRASH_DUMP_DIR"); dir != nullptr && dir[0] != 0) return fs::path(dir);
#endif
    return fs::path("crashes");
}

// The player sees the report; hidden test runs (GALAXY_HIDE_WINDOW) and the headless host keep it in its file.
bool crash_dialog_wanted() {
#if defined(NEBULA_HOST_RENDERER) || defined(NEBULA_HOST_SDL)
    const char* hidden = std::getenv("GALAXY_HIDE_WINDOW");
    return !(hidden != nullptr && hidden[0] != 0 && hidden[0] != '0');
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
                  dump.empty() ? "" : "\nDump: ", dump.empty() ? "" : dump.string().c_str());
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
        std::snprintf(line, sizeof(line), "report %s", file.string().c_str());
        log_line("crash:", line);
    }
    if (!crash_dialog_wanted()) return;
#if defined(_WIN32) && defined(NEBULA_HOST_RENDERER)
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring command = L"\"" + std::wstring(exe) + L"\" --crash-report \"" + file.wstring() + L"\"";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (CreateProcessW(exe, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
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
    std::fflush(stdout);
    std::_Exit(3);
}

uint32_t load_be32(const uint8_t* p) {
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

// The module's entry points.
struct Module {
    decltype(&nebula_game_attach) attach;
    decltype(&nebula_game_run) run;
    decltype(&nebula_game_disc_complete) disc_complete;
    decltype(&nebula_game_vi_retrace) vi_retrace;
    decltype(&nebula_game_ai_interrupt) ai_interrupt;
    decltype(&nebula_game_audio_channel_owner) audio_channel_owner;
    decltype(&nebula_game_mii_valid) mii_valid;
    decltype(&nebula_game_mii_changed) mii_changed;
    void (*debug_dump)();  // optional: the module's thread report
};
Module gModule;

// The GX command ring's size (sdk/gx/gx_fifo.c), mapped twice in a row: a span past the first mapping's end
// continues in the second.

// Progress counters for the host's log.
std::atomic<uint32_t> gDiscReads{0};
std::atomic<uint32_t> gFsOps{0};
std::atomic<uint32_t> gAiBlocks{0};
std::atomic<uint32_t> gDspFrames{0};
std::atomic<uint32_t> gMovieFrames{0};
std::atomic<uint64_t> gMovieDecodeNs{0};   // the THP decoder's time (svc_thp_video_decode)
std::atomic<uint32_t> gMovieSkipped{0};    // THP frames not decoded: a replacement video showed instead
std::atomic<uint32_t> gSceneBegins{0};

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
// GALAXY_PROFILE_GAME=<seconds>: the game thread (every game thread runs on it) sampled each millisecond from its
// start for that long, with its callers, into game-profile.txt beside the crash dumps: the modules loaded
// ("module <base> <size> <name>"), then a line a sample (return addresses, the innermost first).
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

void run(HANDLE thread, double seconds) {
    std::vector<std::pair<int, Frames>> samples;
    samples.reserve(static_cast<std::size_t>(seconds * 1200.0) + 16u);
    const auto until = Clock::now() + std::chrono::duration<double>(seconds);
    auto next = Clock::now();
    while (Clock::now() < until && samples.size() < samples.capacity()) {
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
    std::FILE* file = _wfopen((crash_folder() / L"game-profile.txt").c_str(), L"wb");
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
    std::snprintf(text, sizeof(text), "%zu samples in game-profile.txt", samples.size());
    log_line("profile:", text);
}

}  // namespace game_profile
#endif

void load_module(const fs::path& path) {
#if defined(_WIN32)
    void* module = LoadLibraryW(path.wstring().c_str());
#else
    void* module = dlopen(path.string().c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
    if (module == nullptr) {
        die("cannot load the game module %s", path.string().c_str());
    }
    gModule.attach = reinterpret_cast<decltype(gModule.attach)>(find_symbol(module, "nebula_game_attach"));
    gModule.run = reinterpret_cast<decltype(gModule.run)>(find_symbol(module, "nebula_game_run"));
    gModule.disc_complete = reinterpret_cast<decltype(gModule.disc_complete)>(find_symbol(module, "nebula_game_disc_complete"));
    gModule.vi_retrace = reinterpret_cast<decltype(gModule.vi_retrace)>(find_symbol(module, "nebula_game_vi_retrace"));
    gModule.ai_interrupt = reinterpret_cast<decltype(gModule.ai_interrupt)>(find_symbol(module, "nebula_game_ai_interrupt"));
    gModule.audio_channel_owner = reinterpret_cast<decltype(gModule.audio_channel_owner)>(
        find_symbol(module, "nebula_game_audio_channel_owner"));
    gModule.mii_valid = reinterpret_cast<decltype(gModule.mii_valid)>(find_symbol(module, "nebula_game_mii_valid"));
    gModule.mii_changed =
        reinterpret_cast<decltype(gModule.mii_changed)>(find_symbol(module, "nebula_game_mii_changed"));
#if defined(_WIN32)
    gModule.debug_dump = reinterpret_cast<void (*)()>(GetProcAddress(static_cast<HMODULE>(module), "nebula_game_debug_dump"));
#else
    gModule.debug_dump = reinterpret_cast<void (*)()>(dlsym(module, "nebula_game_debug_dump"));
#endif
}

#if defined(NEBULA_HOST_RENDERER)
void svc_gpu_coherent(void*, const void* address, uint32_t size);

#if defined(NEBULA_HOST_RENDERER)
// The images handed to the renderer (texture_image), by address: one handed over again unchanged needs no
// new record. Any write over one forgets it, as the renderer drops its copy then: the game handing the same bytes over
// after reloading a file must reach the renderer again.
std::mutex gHandedLock;
std::map<uint64_t, std::shared_ptr<const std::vector<std::byte>>> gHanded;

void forget_handed(uint64_t physical, uint32_t size) {
    const std::lock_guard guard(gHandedLock);
    auto it = gHanded.lower_bound(physical + size);
    while (it != gHanded.begin()) {
        --it;
        if (it->first + it->second->size() <= physical) {
            break;
        }
        it = gHanded.erase(it);
    }
}
#endif

// Host writes into game memory (disc DMA): images handed over there must be handed over again.
void mark_written(const void* dst, uint32_t size) {
#if defined(NEBULA_HOST_RENDERER)
    forget_handed(reinterpret_cast<uint64_t>(dst), size);
#else
    (void)dst, (void)size;
#endif
}
#endif

// The game partition: Setup's game.pak, the decrypted partition data at its disc offsets (boot.bin at 0, main.dol
// and the file table where boot.bin says), mapped read-only.
struct Pak {
    const uint8_t* data = nullptr;
    uint64_t size = 0;
    const uint8_t* fst = nullptr;
    size_t fst_size = 0;
    const uint8_t* dol = nullptr;
    size_t dol_size = 0;
    char id[7] = {};  // boot.bin's game id
};
Pak gPak;
// The mods this session (galaxy::mods): the disc's file table with their files, main.dol's patches, a save folder.
std::unique_ptr<galaxy::mods::ActiveMods> gMods;

void open_pak(const fs::path& path) {
#if defined(_WIN32)
    HANDLE file = CreateFileW(path.wstring().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    LARGE_INTEGER size{};
    if (file == INVALID_HANDLE_VALUE || !GetFileSizeEx(file, &size)) {
        die("cannot open %s", path.string().c_str());
    }
    HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    const void* view = mapping != nullptr ? MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0) : nullptr;
    if (view == nullptr) {
        die("cannot map %s", path.string().c_str());
    }
    gPak.data = static_cast<const uint8_t*>(view);
    gPak.size = static_cast<uint64_t>(size.QuadPart);
#else
    const int fd = open(path.c_str(), O_RDONLY);
    struct stat st {};
    if (fd < 0 || fstat(fd, &st) != 0) {
        die("cannot open %s", path.c_str());
    }
    void* view = mmap(nullptr, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (view == MAP_FAILED) {
        die("cannot map %s", path.c_str());
    }
    gPak.data = static_cast<const uint8_t*>(view);
    gPak.size = (uint64_t)st.st_size;
#endif
    if (gPak.size < 0x440) {
        die("%s is too short for a game partition", path.string().c_str());
    }
    // The releases the module plays (sdk/region.c): North America, Europe and Japan.
    std::memcpy(gPak.id, gPak.data, 6);
    if (std::strcmp(gPak.id, "RMGE01") != 0 && std::strcmp(gPak.id, "RMGP01") != 0 &&
        std::strcmp(gPak.id, "RMGJ01") != 0) {
        die("%s holds %.6s, not Super Mario Galaxy for North America, Europe or Japan", path.string().c_str(), gPak.id);
    }
    // Wii partition offsets are words: boot.bin holds them divided by 4.
    const uint64_t dol = (uint64_t)load_be32(gPak.data + 0x420) << 2;
    const uint64_t fst = (uint64_t)load_be32(gPak.data + 0x424) << 2;
    const uint64_t fst_size = (uint64_t)load_be32(gPak.data + 0x428) << 2;
    if (dol + 0x100 > gPak.size || fst + fst_size > gPak.size) {
        die("%s: boot.bin points past the partition", path.string().c_str());
    }
    gPak.fst = gPak.data + fst;
    gPak.fst_size = (size_t)fst_size;
    gPak.dol = gPak.data + dol;
    uint64_t end = 0x100;
    for (int s = 0; s < 18; s++) {
        const uint64_t section_end = (uint64_t)load_be32(gPak.dol + s * 4) + load_be32(gPak.dol + 0x90 + s * 4);
        if (load_be32(gPak.dol + 0x90 + s * 4) != 0 && section_end > end) {
            end = section_end;
        }
    }
    if (dol + end > gPak.size) {
        die("%s: main.dol runs past the partition", path.string().c_str());
    }
    gPak.dol_size = (size_t)end;
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

Worker gDisc(L"Nebula Host Disc");
Worker gFs(L"Nebula Host Saves");

// NAND: the console's absolute paths under the host's NAND root, in the Wii's layout.
fs::path gNandRoot;

// The game's save folder on the NAND (title/00010000/<the game's four-character code in hex>/data).
std::string gSaveDataPath;

bool nand_path(const char* path, fs::path* out) {
    if (path == nullptr || path[0] != '/') {
        return false;
    }
    fs::path result = gNandRoot;
    // A Riivolution mod's savegame: the game's saves in its folder.
    if (gMods != nullptr && !gMods->save_folder.empty() && !gSaveDataPath.empty() &&
        std::strncmp(path, gSaveDataPath.c_str(), gSaveDataPath.size()) == 0 &&
        (path[gSaveDataPath.size()] == '/' || path[gSaveDataPath.size()] == '\0')) {
        result = gMods->save_folder;
        path += gSaveDataPath.size();
        if (*path == '\0') {
            *out = result;
            return true;
        }
    }
    for (const fs::path& part : fs::path(path + 1)) {
        if (part == ".." || part == ".") {
            return false;
        }
        result /= part;
    }
    *out = result;
    return true;
}

// A rename, else a copy and a removal: a mod's save folder can be on another volume than the NAND's /tmp.
void move_path(const fs::path& from, const fs::path& to, std::error_code& error) {
    fs::rename(from, to, error);
    if (!error) {
        return;
    }
    std::error_code copy_error;
    if (fs::is_directory(from, copy_error)) {
        fs::copy(from, to, fs::copy_options::recursive | fs::copy_options::overwrite_existing, copy_error);
    } else {
        fs::copy_file(from, to, fs::copy_options::overwrite_existing, copy_error);
    }
    if (!copy_error) {
        fs::remove_all(from, copy_error);
        error.clear();
    }
}

std::FILE* open_file(const fs::path& path, uint32_t mode) {
    // NAND_ACCESS_READ 1, WRITE 2, RW 3: the file must exist.
#if defined(_WIN32)
    return _wfopen(path.wstring().c_str(), mode == 1 ? L"rb" : L"r+b");
#else
    return std::fopen(path.c_str(), mode == 1 ? "rb" : "r+b");
#endif
}

// Audio interface pacing: the AI takes a latched block at each block's end, as the console's DMA engine does when
// its count runs out, and raises the AI interrupt for the game to latch the next.
std::mutex gAiLock;
std::condition_variable gAiWake;
uint32_t gAiLatchedSize = 0;
const void* gAiLatchedStart = nullptr;
uint32_t gAiRate = 32000;
bool gAiRunning = false;

#if defined(NEBULA_HOST_RENDERER)
extern galaxy::host::NativeGameAudio* gAudio;
#endif

// The app out of sight (SDL hosts: Android's background): the display and audio clocks stop, so the game waits at
// its next retrace; they restart from its return, with nothing to catch up.
std::mutex gSuspendLock;
std::condition_variable gSuspendWake;
bool gSuspended = false;  // guarded by gSuspendLock

// The start runs ahead: the game shows nothing until its first 3D scene (the title's, or a test's galaxy), and its
// loads advance a step each retrace while it takes well under a millisecond a frame, so until then retraces come every
// kBootRetrace and one frame a display period is presented. The first 3D scene, or kBootRetraces retraces, end it.
// GALAXY_BOOT_FAST=0 keeps 60 retraces a second from the start.
constexpr auto kBootRetrace = std::chrono::microseconds(2000);
constexpr uint32_t kBootRetraces = 600;
std::atomic<bool> gBootFast{[] {
    const char* value = std::getenv("GALAXY_BOOT_FAST");
    return value == nullptr || value[0] != '0';
}()};

void end_boot_fast(const char* why) {
    if (gBootFast.exchange(false, std::memory_order_relaxed)) {
        boot_mark(why);
#if defined(NEBULA_HOST_RENDERER)
        galaxy::render::set_frame_end_presents(true);
#endif
    }
}

// True when it waited.
bool wait_while_suspended() {
    std::unique_lock<std::mutex> lock(gSuspendLock);
    if (!gSuspended) {
        return false;
    }
    gSuspendWake.wait(lock, [] { return !gSuspended; });
    return true;
}

// The audio interface's clock: a block taken every block's length (4 bytes a stereo sample at the AI rate), on
// fixed deadlines, as the console's DMA engine runs. With an output device the period follows the device's own clock:
// at most 1 % shorter while fewer blocks than the sink's restart level wait there unplayed, longer while more, so the
// queue holds about that level and the two clocks never drift apart. It never waits for the device: an audio
// interface that stalls leaves the game's sounds playing forever until its sound pools run out. A silent or failed
// device leaves the clock as it is.
void ai_thread() {
    name_thread(L"Nebula Host AI");
    Clock::time_point next{};
    bool scheduled = false;
    for (;;) {
        if (wait_while_suspended()) {
            scheduled = false;
        }
        uint32_t size = 0;
        const void* start = nullptr;
        uint32_t rate = 32000;
        {
            std::unique_lock<std::mutex> lock(gAiLock);
            // A stopped interface (or none latched yet) restarts the schedule from the block that resumes it.
            if (!(gAiRunning && gAiLatchedSize != 0)) {
                gAiWake.wait(lock, [] { return gAiRunning && gAiLatchedSize != 0; });
                scheduled = false;
            }
            size = gAiLatchedSize;
            start = gAiLatchedStart;
            rate = gAiRate != 0 ? gAiRate : 32000;
        }
        if (!scheduled) {
            next = Clock::now();
            scheduled = true;
        }
        double period_ns = static_cast<double>((uint64_t)size / 4 * 1000000000ull) / rate;
#if defined(NEBULA_HOST_RENDERER)
        bool played = false;
        if (start != nullptr) {
            played = gAudio->play(std::span<const std::byte>(static_cast<const std::byte*>(start), size), rate);
        }
        if (played && gAudio->output_available()) {
            const int error = static_cast<int>(gAudio->queued_blocks()) - static_cast<int>(gAudio->target_blocks());
            period_ns *= 1.0 + std::clamp(error * 0.0025, -0.01, 0.01);
        }
#else
        (void)start;
#endif
        gAiBlocks.fetch_add(1, std::memory_order_relaxed);
        // GALAXY_TEST_AI_STALL_AFTER=<blocks>: no interrupt after that many blocks. The game's sounds then never end and
        // its sound pools fill; it must refuse new sounds, not crash.
        static const uint64_t stall_after = [] {
            const char* value = std::getenv("GALAXY_TEST_AI_STALL_AFTER");
            return value != nullptr ? std::strtoull(value, nullptr, 10) : 0ull;
        }();
        if (stall_after == 0 || gAiBlocks.load(std::memory_order_relaxed) < stall_after) {
            gModule.ai_interrupt();
        }
        next += std::chrono::nanoseconds(static_cast<int64_t>(period_ns));
        // A stall past 100 ms (a debugger, a hitch) starts the schedule again rather than catching up in a burst.
        const auto now = Clock::now();
        if (next + std::chrono::milliseconds(100) < now) {
            next = now;
        }
        sleep_until_precise(next);
    }
}

// The display's retrace at 60 Hz: the console's NTSC 59.94 would repeat a frame every 17 s on a 60 Hz display. A late
// wake-up skips the fields it missed, as a display does, rather than delivering them back to back. On Android the
// retrace follows the display's own vsync (every second one at 120 Hz), so frames never drift against it and each
// stays on screen for the same time; the host's clock takes over where the display gives none.
void vi_thread() {
    name_thread(L"Nebula Host VI");
    const auto display_period = std::chrono::nanoseconds(1000000000ull / 60);
    auto period = gBootFast.load(std::memory_order_relaxed) ? std::chrono::nanoseconds(kBootRetrace) : display_period;
    auto next = Clock::now() + period;
#if defined(NEBULA_HOST_SDL)
    int64_t vsync_ns = 0;
    uint32_t on_vsync = 0;
    uint32_t retraces = 0;
    int64_t work_sum = 0;
    int64_t work_max = 0;
#endif
    for (;;) {
#if defined(NEBULA_HOST_SDL)
        const bool vsync = galaxy::sdl::wait_display_retrace(period.count(), vsync_ns);
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
        [[maybe_unused]] const int64_t work = galaxy::host::perf_hint_retrace();
        gModule.vi_retrace();
        if (period != display_period && !gBootFast.load(std::memory_order_relaxed)) {
            period = display_period;
        }
        next += period;
        const auto now = Clock::now();
        if (now >= next) {
            next = now + period;
        }
#if defined(NEBULA_HOST_SDL)
        if (vsync) {
            next = now + period;  // the host's clock continues from the display's
            ++on_vsync;
        }
        // Every 10 s: how the retraces were paced, and the frame work (the busier of the game and render threads).
        work_sum += work;
        work_max = std::max(work_max, work);
        if (++retraces == 600u) {
            std::fprintf(stderr, "[pacing] %u retraces, %u on the display's vsync (%.1f Hz); frame work %.2f ms average, "
                         "%.2f ms longest\n", retraces, on_vsync, galaxy::sdl::display_refresh_hz(),
                         static_cast<double>(work_sum) / retraces / 1e6, static_cast<double>(work_max) / 1e6);
            retraces = on_vsync = 0u;
            work_sum = work_max = 0;
        }
#endif
    }
}

void svc_log(void*, const char* text) {
    log_line("game:", text);
}

#if defined(_WIN32)
// GALAXY_TRACE_THREAD_CPU=1: each thread's CPU time and context switches over the last second, by name (or,
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
    if (status == (LONG)0xC0000004 /* STATUS_INFO_LENGTH_MISMATCH */ && needed != 0) {
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
            char narrow[64];
            WideCharToMultiByte(CP_UTF8, 0, description, -1, narrow, sizeof(narrow), nullptr, nullptr);
            name = narrow;
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
        wchar_t path[MAX_PATH];
        std::swprintf(path, MAX_PATH, L"%ls\\nebula-native-%04u%02u%02u-%02u%02u%02u-%lu.dmp", folder.c_str(), now.wYear, now.wMonth,
                      now.wDay, now.wHour, now.wMinute, now.wSecond, GetCurrentProcessId());
        const HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll");
        const auto write = dbghelp != nullptr
                               ? reinterpret_cast<MiniDumpWriteDumpFn>(GetProcAddress(dbghelp, "MiniDumpWriteDump"))
                               : nullptr;
        const HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (write != nullptr && file != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION exception{gCrashThread, gCrashInfo, FALSE};
            // Stacks and what they point at (the module's data segments would add its half-gigabyte image window).
            const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo);
            const BOOL ok = write(GetCurrentProcess(), GetCurrentProcessId(), file, type,
                                  gCrashInfo != nullptr ? &exception : nullptr, nullptr, nullptr);
            char text[MAX_PATH + 32];
            std::snprintf(text, sizeof(text), "%s %ls", ok ? "minidump" : "minidump failed:", path);
            log_line("crash:", text);
        }
        if (file != INVALID_HANDLE_VALUE) {
            CloseHandle(file);
        }
        report_crash_facts(fs::path(path));
        SetEvent(gCrashDone);
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
    if (gModule.debug_dump != nullptr) {
        gModule.debug_dump();
    }
    std::fflush(stdout);
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
    gather_crash_facts(galaxy::crash::Facts::Kind::Fault, text, owner == GetModuleHandleW(L"smg_game.dll"),
                       code == EXCEPTION_STACK_OVERFLOW);
    report_crash(info);
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
        (owner != GetModuleHandleW(nullptr) && owner != GetModuleHandleW(L"smg_game.dll"))) {
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
    report_crash(info);
    std::_Exit(4);
}

#if defined(NEBULA_HOST_RENDERER)
// NebulaNative --crash-report <file>: the report of an instance that stopped (report_crash_facts), shown by this new
// process. The technical details stay in the file, which the dialog offers to show.
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
    text += "\n\nOpen the folder with the report?";
    std::wstring wide(static_cast<std::size_t>(
                          MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0)),
                      L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), static_cast<int>(wide.size()));
    if (MessageBoxW(nullptr, wide.c_str(), L"Nebula stopped", MB_ICONERROR | MB_YESNO | MB_SETFOREGROUND | MB_TOPMOST) ==
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

void svc_fatal(void*, const char* text) {
    log_line("fatal:", text);
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

#if defined(NEBULA_HOST_RENDERER)
// The renderer: the game's records reach it in frames (render_frame at the game's frame end and, while the start runs
// ahead, at the retrace, which also presents). All of this runs on the simulation thread, where the game makes these
// calls.
using galaxy::RenderLogLevel;
using galaxy::RenderServices;

RenderServices gNative{};
std::atomic<uint64_t> gSubmissions{0};  // render_frame calls with commands: the renderer's frame index

void native_log(void*, RenderLogLevel, const char* text) {
    log_line("renderer:", text);
}

galaxy::host::NativeGameAudio* gAudio = nullptr;

void submit_frame_unchecked(bool present, bool black) {
    galaxy::render::render_frame(&gNative, present, black);
    gSubmissions.fetch_add(1, std::memory_order_relaxed);
}

// The renderer's hard failures end the game with their line and a dump. Caught here: thrown on the game's stacks they
// could not unwind through its frames.
void submit_frame(bool present, bool black) {
    try {
        submit_frame_unchecked(present, black);
    } catch (const std::exception& error) {
        char text[1024];
        std::snprintf(text, sizeof(text), "the renderer stopped: %s", error.what());
        svc_fatal(nullptr, text);
    }
}
#endif

// The stage the game reports before each 3D scene (game_stage); the scene's begin marker carries it.
std::string gSceneStage;

void svc_game_stage(void*, const char* name) {
    if (name != nullptr && gSceneStage != name) {
        gSceneStage = name;
    }
}

void svc_object_name(void*, uint64_t id, const char* name) {
#if defined(NEBULA_HOST_RENDERER)
    galaxy::render::note_object_name(id, name);
#else
    (void)id;
    (void)name;
#endif
}

float svc_light_reach(void*, const char* model, float radius) {
#if defined(NEBULA_HOST_RENDERER)
    return galaxy::render::rt_light_reach(model, radius);
#else
    (void)model;
    (void)radius;
    return 0.0f;
#endif
}

int svc_traced_sun(void*, float* direction) {
#if defined(NEBULA_HOST_RENDERER)
    std::array<float, 3> sun{};
    if (direction == nullptr || !galaxy::render::traced_sun(sun, 500u)) {
        return 0;
    }
    direction[0] = sun[0];
    direction[1] = sun[1];
    direction[2] = sun[2];
    return 1;
#else
    (void)direction;
    return 0;
#endif
}

// The Discord application Nebula's status shows under ("Super Mario Galaxy"; GALAXY_DISCORD_CLIENT_ID replaces it).
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
// UTF-8 text into a fixed field, cut at a whole character, always terminated.
template <std::size_t N>
void copy_settings_text(char (&field)[N], const std::string& text) {
    std::size_t length = std::min(text.size(), N - 1u);
    while (length > 0u && length < text.size() && (static_cast<unsigned char>(text[length]) & 0xC0u) == 0x80u) {
        --length;
    }
    std::memcpy(field, text.data(), length);
    field[length] = '\0';
}
#endif

#if defined(NEBULA_HOST_RENDERER)
static_assert(static_cast<uint32_t>(galaxy::render::NativeSettingsStatus::Leave) == NEBULA_SETTINGS_STATUS_LEAVE &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsStatus::ModsRestartAsk) == NEBULA_SETTINGS_STATUS_MODS_RESTART_ASK &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsStatus::KeepDisplay) == NEBULA_SETTINGS_STATUS_KEEP_DISPLAY &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsStatus::JoyCon) == NEBULA_SETTINGS_STATUS_JOYCON &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsStatus::RtRestartAsk) ==
                  NEBULA_SETTINGS_STATUS_RT_RESTART_ASK &&
              static_cast<uint32_t>(galaxy::render::NativeSettingsStatus::DiagnosticsAsk) ==
                  NEBULA_SETTINGS_STATUS_DIAGNOSTICS_ASK &&
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
// The headless host has no settings menu: always closed.
void svc_settings_view(void*, NebulaSettingsView* view) {
    std::memset(view, 0, sizeof(*view));
#if defined(NEBULA_HOST_RENDERER)
    const galaxy::render::NativeSettingsView source = galaxy::render::native_settings_view();
    view->open = source.open ? 1u : 0u;
    view->page = source.page;
    view->page_count = static_cast<uint32_t>(std::min<std::size_t>(source.pages.size(), NEBULA_SETTINGS_PAGES));
    for (uint32_t i = 0; i < view->page_count; ++i) {
        copy_settings_text(view->pages[i], source.pages[i]);
    }
    view->controls_page = source.controls_page;
    view->input = source.input;
    // A long page (many mods) is sent in part: the rows around the selection, more than the menu shows at once.
    const std::size_t total = source.rows.size();
    const std::size_t selected = std::min<std::size_t>(source.selected, total == 0u ? 0u : total - 1u);
    const std::size_t first = total <= NEBULA_SETTINGS_ROWS
                                  ? 0u
                                  : std::min(selected - std::min<std::size_t>(selected, NEBULA_SETTINGS_ROWS / 2u),
                                             total - NEBULA_SETTINGS_ROWS);
    view->total_rows = static_cast<uint32_t>(total);
    view->first_row = static_cast<uint32_t>(first);
    view->row_count = static_cast<uint32_t>(std::min<std::size_t>(total - first, NEBULA_SETTINGS_ROWS));
    for (uint32_t i = 0; i < view->row_count; ++i) {
        const galaxy::render::NativeSettingsViewRow& row = source.rows[first + i];
        copy_settings_text(view->rows[i].label, row.label);
        copy_settings_text(view->rows[i].value, row.value);
        view->rows[i].flags = (row.heading ? NEBULA_SETTINGS_ROW_HEADING : 0u) |
                              (row.changes ? NEBULA_SETTINGS_ROW_CHANGES : 0u) | (row.name ? NEBULA_SETTINGS_ROW_NAME : 0u) |
                              (row.active ? NEBULA_SETTINGS_ROW_ACTIVE : 0u) |
                              (row.label_name ? NEBULA_SETTINGS_ROW_LABEL_NAME : 0u) |
                              (row.buttons ? NEBULA_SETTINGS_ROW_BUTTONS : 0u) |
                              (row.mod_list == 1u ? NEBULA_SETTINGS_ROW_MOD_OFF : 0u) |
                              (row.mod_list == 2u ? NEBULA_SETTINGS_ROW_MOD_ON : 0u) | (row.held ? NEBULA_SETTINGS_ROW_HELD : 0u) |
                              (row.slider_steps != 0u ? NEBULA_SETTINGS_ROW_SLIDER : 0u) |
                              (row.inputs ? NEBULA_SETTINGS_ROW_BINDING : 0u) |
                              (row.shared ? NEBULA_SETTINGS_ROW_SHARED : 0u);
        view->rows[i].slider_steps = static_cast<uint16_t>(row.slider_steps);
        view->rows[i].slider_at = static_cast<uint16_t>(row.slider_at);
    }
    view->selected = static_cast<uint32_t>(selected);
    view->selected_button = source.selected_button;
    for (std::size_t r = 0; r < total; ++r) {
        const galaxy::render::NativeSettingsViewRow& row = source.rows[r];
        if (row.mod_list == 0u || row.mod_list > 2u || row.heading) continue;
        if (r < first) ++view->mod_list_first[row.mod_list - 1u];
        ++view->mod_list_count[row.mod_list - 1u];
    }
    copy_settings_text(view->help, source.help);
    view->status = static_cast<uint32_t>(source.status);
    view->status_number = source.status_number;
    copy_settings_text(view->status_text, source.status_text);
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

// A mod's code patch the game runs natively (NEBULA_PATCH_*), as the mods layer matched it at the start.
int svc_code_patch(void*, uint32_t id, uint32_t* value) {
    static_assert(galaxy::mods::kPatchFlipPanelGroup == NEBULA_PATCH_FLIP_PANEL_GROUP &&
                  galaxy::mods::kPatchMessengerExOneUps == NEBULA_PATCH_MESSENGER_EX_ONE_UPS);
    if (gMods == nullptr) return 0;
    const auto it = gMods->code_patches.find(id);
    if (it == gMods->code_patches.end()) return 0;
    if (value != nullptr) *value = it->second;
    // Logged the first time the game uses each.
    static std::atomic<uint64_t> logged{0};
    if (id < 64u && (logged.fetch_or(uint64_t{1} << id) & (uint64_t{1} << id)) == 0u) {
        char text[64];
        std::snprintf(text, sizeof(text), "native patch %u in use (value 0x%08X)", id, it->second);
        log_line("mods:", text);
    }
    return 1;
}

void svc_movie_frame(void*, const char* name, int32_t frame, float frame_rate, const void* y, const void* u, const void* v,
                     uint32_t width, uint32_t height) {
#if defined(NEBULA_HOST_RENDERER)
    galaxy::render::attach_movie_frame(name, frame, frame_rate, reinterpret_cast<uint64_t>(y), reinterpret_cast<uint64_t>(u),
                                   reinterpret_cast<uint64_t>(v), width, height);
#else
    (void)name, (void)frame, (void)frame_rate, (void)y, (void)u, (void)v, (void)width, (void)height;
#endif
}

void svc_scene_marker(void*, uint32_t marker, const float* view) {
    if (marker == NEBULA_SCENE_3D_BEGIN) {
        gSceneBegins.fetch_add(1, std::memory_order_relaxed);
        end_boot_fast("first 3D scene");
    }
#if defined(NEBULA_HOST_RENDERER)
    if (marker == NEBULA_SCENE_3D_BEGIN && view != nullptr) {
        galaxy::render::attach_scene_view(view, view[15] != 0.0f ? view + 12 : nullptr,
                                      gSceneStage.empty() ? nullptr : gSceneStage.c_str());
    }
    static constexpr galaxy::render::FifoMarker kMarkers[] = {
        galaxy::render::FifoMarker::SceneBegin, galaxy::render::FifoMarker::SceneEnd,
        galaxy::render::FifoMarker::Scene2DBegin, galaxy::render::FifoMarker::Scene2DEnd};
    if (marker < std::size(kMarkers)) {
        galaxy::render::attach_fifo_marker(kMarkers[marker]);
    }
#else
    (void)marker;
    (void)view;
#endif
}

void svc_stripe_particle(void*, uint64_t particle, uint32_t age_lifetime, uint8_t strip) {
#if defined(NEBULA_HOST_RENDERER)
    galaxy::render::attach_stripe_particle(0, particle, age_lifetime, strip);
#else
    (void)particle;
    (void)age_lifetime;
    (void)strip;
#endif
}

// The game's steady clock: nanoseconds since the process's first read, plus one (0 means "never" to its users).
uint64_t svc_clock_ns(void*) {
    static const Clock::time_point start = Clock::now();
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count()) + 1u;
}

int64_t svc_calendar_ns(void*) {
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

// GALAXY_EXPORT_ART=<folder>: Nebula's own pictures (NebulaIcons) as a texture pack, pack/RMG/tex1_..._5.png at their
// HD size, named by the texture the game draws; previews in native/ and hd/; pack-names.txt pairs them.
// The scale the renderer last drew the 2D layer at (render_core: the EFB in use when the scene ends).
uint32_t svc_layer_2d_scale(void*) {
#if defined(NEBULA_HOST_RENDERER)
    return galaxy::render::layer_2d_scale();
#else
    return 0u;
#endif
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

// A loaded model's triangles: the analysis its curved surfaces are rounded by, kept by its position array's address
// (galaxy/render/curved_surfaces.h).
void svc_curved_surface(void*, uint64_t key, const float* positions, uint32_t position_count, const float* normals,
                        uint32_t normal_count, const uint16_t* corners, uint32_t triangle_count) {
#if defined(NEBULA_HOST_RENDERER)
    namespace curved = galaxy::render::curved;
    if (positions == nullptr || normals == nullptr || corners == nullptr || triangle_count == 0u) {
        return;
    }
    std::vector<curved::Vec3> p(position_count);
    std::vector<curved::Vec3> n(normal_count);
    std::vector<curved::Corner> c(static_cast<std::size_t>(triangle_count) * 3u);
    std::memcpy(p.data(), positions, p.size() * sizeof(curved::Vec3));
    std::memcpy(n.data(), normals, n.size() * sizeof(curved::Vec3));
    for (std::size_t i = 0; i < c.size(); ++i) {
        c[i] = curved::Corner{corners[2u * i], corners[2u * i + 1u]};
    }
    curved::register_model_later(key, std::move(p), std::move(n), std::move(c));
#else
    (void)key, (void)positions, (void)position_count, (void)normals, (void)normal_count, (void)corners,
        (void)triangle_count;
#endif
}

#if defined(NEBULA_HOST_RENDERER)
// Native shape arrays and groups (galaxy/render/native_shapes.h): the game registers them from its loading threads too,
// so they wait here and join the stream ahead of the next draw record (which the game makes after them).
std::mutex gShapeLock;
std::vector<galaxy::render::shapes::Record> gShapePending;
std::atomic<bool> gShapePendingAny{false};

void queue_shape_record(galaxy::render::shapes::Record record) {
    std::lock_guard guard(gShapeLock);
    gShapePending.push_back(std::move(record));
    gShapePendingAny.store(true, std::memory_order_release);
}
#endif

void svc_shape_array(void*, const NebulaShapeArray* array) {
#if defined(NEBULA_HOST_RENDERER)
    namespace shapes = galaxy::render::shapes;
    if (array == nullptr || (array->values == nullptr && array->count != 0u)) {
        return;
    }
    auto values = std::make_shared<shapes::Array>();
    values->count = array->count;
    std::size_t floats = 0;
    switch (array->kind) {
    case NEBULA_SHAPE_ARRAY_POSITION: values->kind = shapes::ArrayKind::Position; floats = 3u; break;
    case NEBULA_SHAPE_ARRAY_NORMAL: values->kind = shapes::ArrayKind::Normal; floats = 3u; break;
    case NEBULA_SHAPE_ARRAY_NBT: values->kind = shapes::ArrayKind::Nbt; floats = 9u; break;
    case NEBULA_SHAPE_ARRAY_TEXCOORD: values->kind = shapes::ArrayKind::TexCoord; floats = 2u; break;
    case NEBULA_SHAPE_ARRAY_COLOR: values->kind = shapes::ArrayKind::Color; break;
    default: die("a shape array of unknown kind %u", array->kind);
    }
    if (values->kind == shapes::ArrayKind::Color) {
        const auto* colors = static_cast<const uint32_t*>(array->values);
        values->colors.assign(colors, colors + array->count);
    } else {
        const auto* f = static_cast<const float*>(array->values);
        values->floats.assign(f, f + static_cast<std::size_t>(array->count) * floats);
    }
    shapes::Record record;
    record.kind = shapes::Record::Kind::Array;
    record.key = array->key;
    record.array = std::move(values);
    queue_shape_record(std::move(record));
#else
    (void)array;
#endif
}

void svc_shape_geometry(void*, const NebulaShapeGeometry* geometry) {
#if defined(NEBULA_HOST_RENDERER)
    namespace shapes = galaxy::render::shapes;
    if (geometry == nullptr || (geometry->corner_count != 0u && geometry->corners == nullptr) ||
        (geometry->index_count != 0u && geometry->indices == nullptr)) {
        return;
    }
    auto g = std::make_shared<shapes::Geometry>();
    g->attributes = geometry->attributes;
    for (std::size_t i = 0; i < g->arrays.size(); ++i) {
        g->arrays[i] = geometry->arrays[i];
    }
    g->corners.resize(geometry->corner_count);
    for (uint32_t i = 0; i < geometry->corner_count; ++i) {
        const NebulaShapeCorner& in = geometry->corners[i];
        shapes::Corner& out = g->corners[i];
        out.position = in.position;
        out.normal = in.normal;
        out.tangent = in.tangent;
        out.binormal = in.binormal;
        out.color = {in.color[0], in.color[1]};
        for (unsigned k = 0; k < 8u; ++k) {
            out.texcoord[k] = in.texcoord[k];
            out.texture_matrix[k] = in.texture_matrix[k];
        }
        out.position_matrix = in.position_matrix;
    }
    g->indices.assign(geometry->indices, geometry->indices + geometry->index_count);
    for (const uint16_t index : g->indices) {
        if (index >= geometry->corner_count) {
            die("a shape group's triangle names corner %u of %u", index, geometry->corner_count);
        }
    }
    shapes::Record record;
    record.kind = shapes::Record::Kind::Geometry;
    record.key = geometry->key;
    record.geometry = std::move(g);
    queue_shape_record(std::move(record));
#else
    (void)geometry;
#endif
}

#if defined(NEBULA_HOST_RENDERER)
// A record the game makes now runs in the order the game makes it, after the arrays and groups it registered before it
// (which join the records here).
void attach_pending_records() {
    if (gShapePendingAny.load(std::memory_order_acquire)) {
        std::vector<galaxy::render::shapes::Record> pending;
        {
            std::lock_guard guard(gShapeLock);
            pending.swap(gShapePending);
            gShapePendingAny.store(false, std::memory_order_relaxed);
        }
        for (galaxy::render::shapes::Record& record : pending) {
            galaxy::render::attach_shape_record(std::move(record));
        }
    }
}

#endif

// A group drawn.
void svc_shape_draw(void*, uint64_t shape, uint64_t position, uint64_t normal, uint64_t color,
                    const NebulaShapeMatrix* matrices, uint32_t matrix_count, const NebulaShapeCurrent* current,
                    const NebulaShapeTexGen* texgens, uint32_t texgen_count) {
#if defined(NEBULA_HOST_RENDERER)
    namespace shapes = galaxy::render::shapes;
    attach_pending_records();
    shapes::Record record;
    record.kind = shapes::Record::Kind::Draw;
    record.key = shape;
    record.position = position;
    record.normal = normal;
    record.color = color;
    record.matrices.resize(matrices != nullptr ? matrix_count : 0u);
    for (std::size_t i = 0; i < record.matrices.size(); ++i) {
        const NebulaShapeMatrix& in = matrices[i];
        if (in.kind > NEBULA_SHAPE_MATRIX_POST_TEXTURE) {
            die("a shape draw's matrix of unknown kind %u", in.kind);
        }
        shapes::Matrix& out = record.matrices[i];
        out.kind = static_cast<shapes::MatrixKind>(in.kind);
        out.id = in.id;
        out.model_matrix = in.model_matrix;
        std::memcpy(out.values.data(), in.values, sizeof(in.values));
    }
    for (uint32_t i = 0; texgens != nullptr && i < texgen_count; ++i) {
        const NebulaShapeTexGen& g = texgens[i];
        record.texgens.push_back({g.coord, g.type, g.source, g.normalize, g.post_matrix});
    }
    if (current != nullptr) {
        record.has_current = true;
        record.current[0] = current->position;
        std::memcpy(record.current.data() + 1, current->texture, 8);
    }
    galaxy::render::attach_shape_record(std::move(record));
#else
    (void)shape, (void)position, (void)normal, (void)color, (void)matrices, (void)matrix_count,
        (void)current, (void)texgens, (void)texgen_count;
#endif
}

// An image the game hands over, named by its address in the game's memory.
void svc_texture_image(void*, const NebulaTextureImage* image) {
#if defined(NEBULA_HOST_RENDERER)
    namespace shapes = galaxy::render::shapes;
    if (image == nullptr || image->bytes == nullptr || image->size == 0u) {
        return;
    }
    const auto* first = static_cast<const std::byte*>(image->bytes);
    const uint64_t address = reinterpret_cast<uint64_t>(image->bytes);
    // The copies the renderer has (gHanded): an image handed over again unchanged needs nothing. Loading threads hand
    // images over too, so the record joins the stream ahead of the next one, as shape registrations do.
    std::shared_ptr<const std::vector<std::byte>> copy;
    {
        std::lock_guard guard(gHandedLock);
        auto& last = gHanded[address];
        if (last != nullptr && last->size() == image->size && std::memcmp(last->data(), first, image->size) == 0) {
            return;
        }
        last = std::make_shared<const std::vector<std::byte>>(first, first + image->size);
        copy = last;
    }
    shapes::Record record;
    record.kind = shapes::Record::Kind::TextureImage;
    record.key = address;
    record.image = std::move(copy);
    queue_shape_record(std::move(record));
#else
    (void)image;
#endif
}


// The Mii database in the file a console keeps it in, so the Miis a NAND (or an earlier Nebula) already holds stay
// where they are; the game reads its slots as typed records.
fs::path mii_database_path() {
    return gNandRoot / "shared2/menu/FaceLib/RFL_DB.dat";
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

int32_t svc_mii_record(void*, uint32_t slot, NebulaMii* mii) {
    const std::optional<galaxy::host::MiiRecord> record = galaxy::host::mii_library_slot(slot);
    if (!record) {
        return 0;
    }
    if (mii != nullptr) {
        *mii = nebula_mii(galaxy::host::decode_wii_mii(*record));
    }
    return 1;
}

void svc_immediate(void*, const NebulaImmediate* draw) {
#if defined(NEBULA_HOST_RENDERER)
    namespace shapes = galaxy::render::shapes;
    if (draw == nullptr || draw->count == 0u || draw->position == nullptr) {
        return;
    }
    const uint32_t n = draw->count;
    auto imm = std::make_shared<shapes::Immediate>();
    imm->primitive = draw->primitive;
    shapes::Geometry& g = imm->geometry;
    // Its values are its own: geometry the game builds as it draws.
    g.attributes = (draw->attributes & ~static_cast<uint32_t>(NEBULA_SHAPE_POSITION_MATRIX) |
                    (draw->position_matrix != nullptr ? static_cast<uint32_t>(NEBULA_SHAPE_POSITION_MATRIX) : 0u)) |
                   NEBULA_SHAPE_OWN_POSITIONS;
    auto floats = [&](unsigned slot, shapes::ArrayKind kind, const float* values, uint32_t per) {
        shapes::Array& a = imm->arrays[slot];
        a.kind = kind;
        a.count = n;
        a.floats.assign(values, values + static_cast<size_t>(n) * per);
        g.arrays[slot] = slot + 1u;
    };
    floats(0u, shapes::ArrayKind::Position, draw->position, 3u);
    if ((draw->attributes & NEBULA_SHAPE_NORMAL) != 0u && draw->normal != nullptr) {
        floats(1u, shapes::ArrayKind::Normal, draw->normal, 3u);
    } else {
        g.attributes &= ~static_cast<uint32_t>(NEBULA_SHAPE_NORMAL);
    }
    for (unsigned c = 0; c < 2u; ++c) {
        const uint32_t bit = c == 0u ? NEBULA_SHAPE_COLOR0 : NEBULA_SHAPE_COLOR1;
        if ((draw->attributes & bit) != 0u && draw->color[c] != nullptr) {
            shapes::Array& a = imm->arrays[2u + c];
            a.kind = shapes::ArrayKind::Color;
            a.count = n;
            a.colors.assign(draw->color[c], draw->color[c] + n);
            g.arrays[2u + c] = 3u + c;
        } else {
            g.attributes &= ~bit;
        }
    }
    for (unsigned t = 0; t < 8u; ++t) {
        const uint32_t bit = static_cast<uint32_t>(NEBULA_SHAPE_TEXCOORD0) << t;
        if ((draw->attributes & bit) != 0u && draw->texcoord[t] != nullptr) {
            floats(4u + t, shapes::ArrayKind::TexCoord, draw->texcoord[t], 2u);
        } else {
            g.attributes &= ~bit;
        }
    }
    g.corners.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        shapes::Corner& c = g.corners[i];
        const auto e = static_cast<uint16_t>(i);
        c.position = c.normal = c.color[0] = c.color[1] = e;
        c.texcoord.fill(e);
        c.position_matrix = draw->position_matrix != nullptr ? draw->position_matrix[i] : 0u;
    }
    shapes::Record record;
    record.kind = shapes::Record::Kind::Immediate;
    attach_pending_records();
    record.immediate = std::move(imm);
    galaxy::render::attach_shape_record(std::move(record));
#else
    (void)draw;
#endif
}

void svc_readback_capture(void*, uint32_t channel) {
#if defined(NEBULA_HOST_RENDERER)
    namespace shapes = galaxy::render::shapes;
    shapes::Record record;
    record.kind = shapes::Record::Kind::Readback;
    attach_pending_records();
    record.key = channel;
    galaxy::render::attach_shape_record(std::move(record));
#else
    (void)channel;
#endif
}

void svc_marker(void*, uint32_t kind, uint32_t aux, uint64_t a, uint64_t b) {
#if defined(NEBULA_HOST_RENDERER)
    namespace shapes = galaxy::render::shapes;
    shapes::Record record;
    record.kind = shapes::Record::Kind::Marker;
    attach_pending_records();
    record.key = kind;
    record.marker_aux = aux;
    record.marker = {a, b};
    galaxy::render::attach_shape_record(std::move(record));
#else
    (void)kind, (void)aux, (void)a, (void)b;
#endif
}

// The game's frame end: the renderer takes the frame's records.
void svc_frame_end(void*) {
#if defined(NEBULA_HOST_RENDERER)
    submit_frame(false, false);
#endif
}

uint32_t svc_readback_pixel(void*, uint32_t channel, uint32_t depth, uint32_t x, uint32_t y) {
#if defined(NEBULA_HOST_RENDERER)
    uint32_t value = 0;
    const auto kind = depth != 0u ? galaxy::render::EfbPeekKind::Depth : galaxy::render::EfbPeekKind::Color;
    if (!galaxy::render::peek_efb_published(static_cast<uint16_t>(x), static_cast<uint16_t>(y), kind,
                                        static_cast<uint16_t>(channel), value)) {
        return 0;
    }
    return value;
#else
    (void)channel, (void)depth, (void)x, (void)y;
    return 0;
#endif
}

uint32_t svc_readback_serial(void*, uint32_t channel) {
#if defined(NEBULA_HOST_RENDERER)
    return galaxy::render::readback_serial(static_cast<uint16_t>(channel));
#else
    (void)channel;
    return 0;
#endif
}

void svc_state(void*, const NebulaStateOp* ops, uint32_t count) {
#if defined(NEBULA_HOST_RENDERER)
    namespace shapes = galaxy::render::shapes;
    static_assert(sizeof(NebulaStateOp) == sizeof(galaxy::render::state::Op), "the renderer's copy of NebulaStateOp");
    if (ops == nullptr || count == 0u) {
        return;
    }
    shapes::Record record;
    record.kind = shapes::Record::Kind::State;
    attach_pending_records();
    record.state.resize(count);
    std::memcpy(record.state.data(), ops, sizeof(NebulaStateOp) * count);
    galaxy::render::attach_shape_record(std::move(record));
#else
    (void)ops, (void)count;
#endif
}

void svc_material_program(void*, uint64_t key, const NebulaMaterialProgram* program) {
#if defined(NEBULA_HOST_RENDERER)
    namespace shapes = galaxy::render::shapes;
    static_assert(sizeof(NebulaMaterialProgram) == sizeof(galaxy::render::material::Program),
                  "the renderer's copy of NebulaMaterialProgram");
    if (program == nullptr) {
        return;
    }
    auto copy = std::make_shared<galaxy::render::material::Program>();
    std::memcpy(copy.get(), program, sizeof(NebulaMaterialProgram));
    shapes::Record record;
    record.kind = shapes::Record::Kind::MaterialProgram;
    record.key = key;
    attach_pending_records();
    record.program = std::move(copy);
    galaxy::render::attach_shape_record(std::move(record));
#else
    (void)key;
    (void)program;
#endif
}

void svc_material(void*, uint64_t key, const NebulaMaterialValues* values) {
#if defined(NEBULA_HOST_RENDERER)
    namespace shapes = galaxy::render::shapes;
    static_assert(sizeof(NebulaMaterialValues) == sizeof(galaxy::render::material::Values),
                  "the renderer's copy of NebulaMaterialValues");
    if (values == nullptr) {
        return;
    }
    auto copy = std::make_shared<galaxy::render::material::Values>();
    std::memcpy(copy.get(), values, sizeof(NebulaMaterialValues));
    shapes::Record record;
    record.kind = shapes::Record::Kind::Material;
    record.key = key;
    attach_pending_records();
    record.material = std::move(copy);
    galaxy::render::attach_shape_record(std::move(record));
#else
    (void)key;
    (void)values;
#endif
}

// The wheel's turn the window gathered since the game last asked.
int32_t svc_pointer_wheel(void*) {
#if defined(NEBULA_HOST_RENDERER)
    return galaxy::render::take_pointer_wheel();
#else
    return 0;
#endif
}

uint32_t svc_art_export(void*, const NebulaArtExport* art) {
#if defined(NEBULA_HOST_RENDERER)
    static const std::filesystem::path folder = [] {
        const char* value = std::getenv("GALAXY_EXPORT_ART");
        if (value == nullptr || value[0] == '\0') return std::filesystem::path{};
        // This run's names only.
        std::error_code error;
        std::filesystem::create_directories(value, error);
        std::FILE* names = std::fopen((std::filesystem::path(value) / "pack-names.txt").string().c_str(), "wb");
        if (names != nullptr) std::fclose(names);
        return std::filesystem::path(value);
    }();
    if (folder.empty()) return 0u;
    if (art == nullptr) return 1u;
    const auto write = [](const std::filesystem::path& file, uint32_t width, uint32_t height, const uint8_t* rgba) {
        std::error_code error;
        std::filesystem::create_directories(file.parent_path(), error);
        if (rgba == nullptr || width == 0u || height == 0u || !galaxy::render::write_png(file, width, height, rgba)) {
            log_line("[art]", ("could not write " + file.string()).c_str());
        }
    };
    std::string line = art->name != nullptr ? art->name : "";
    constexpr uint32_t kRgb5a3 = static_cast<uint32_t>(galaxy::render::TexFormat::RGB5A3);
    if (art->texels != nullptr && art->format == kRgb5a3 && art->width % 4u == 0u && art->height % 4u == 0u) {
        galaxy::render::TexturePackName name{};
        name.width = static_cast<uint16_t>(art->width);
        name.height = static_cast<uint16_t>(art->height);
        name.format = static_cast<uint8_t>(art->format);
        name.hash = galaxy::render::texture_pack_hash(static_cast<const uint8_t*>(art->texels),
                                                  static_cast<std::size_t>(art->width) * art->height * 2u);
        const std::string stem = galaxy::render::texture_pack_name(name);
        write(folder / "pack" / "RMG" / (stem + ".png"), art->hd_width, art->hd_height, art->hd);
        line += "\t" + stem;
    }
    if (art->name != nullptr) {
        write(folder / "native" / (std::string(art->name) + ".png"), art->width, art->height, art->rgba);
        write(folder / "hd" / (std::string(art->name) + ".png"), art->hd_width, art->hd_height, art->hd);
    }
    std::FILE* names = std::fopen((folder / "pack-names.txt").string().c_str(), "ab");
    if (names != nullptr) {
        std::fprintf(names, "%s\n", line.c_str());
        std::fclose(names);
    }
    return 1u;
#else
    (void)art;
    return 0u;
#endif
}
std::atomic<uint32_t> gTiltSection{0};
std::atomic<bool> gMotor{false};

void svc_game_context(void*, uint32_t what, int32_t value) {
    char text[64];
    if (what == NEBULA_CONTEXT_STAR_POINTER_MODE) {
        if (gStarPointerMode.exchange(value, std::memory_order_relaxed) != value) {
            std::snprintf(text, sizeof(text), "star pointer mode %d", static_cast<int>(value));
            log_line("context:", text);
        }
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
    }
}

void svc_gpu_coherent(void*, const void* address, uint32_t size) {
#if defined(NEBULA_HOST_RENDERER)
    forget_handed(reinterpret_cast<uint64_t>(address), size);
#else
    (void)address;
    (void)size;
#endif
}

// The disc's file table: the mods' disc's when mods change files, else the partition's. Built at its first use (the
// mods are set up before the game starts).
const galaxy::disc::FileTable& disc_table() {
    static const galaxy::disc::FileTable table = [] {
        galaxy::disc::FileTable loaded;
        const bool mods = gMods != nullptr && !gMods->fst.empty();
        if (!(mods ? loaded.load(gMods->fst.data(), gMods->fst.size()) : loaded.load(gPak.fst, gPak.fst_size))) {
            die("the disc's file table is malformed");
        }
        return loaded;
    }();
    return table;
}

int disc_entry_out(const std::optional<galaxy::disc::Entry>& entry, NebulaDiscEntry* out) {
    if (!entry) {
        return 0;
    }
    *out = {};
    out->index = entry->index;
    out->length = entry->length;
    out->folder = entry->folder ? 1 : 0;
    out->name = entry->name;
    return 1;
}

int svc_disc_find(void*, const char* path, NebulaDiscEntry* out) {
    return path != nullptr ? disc_entry_out(disc_table().find(path), out) : 0;
}

int svc_disc_entry(void*, int32_t index, NebulaDiscEntry* out) {
    return disc_entry_out(disc_table().entry(index), out);
}

int svc_disc_child(void*, int32_t folder, uint32_t child, NebulaDiscEntry* out) {
    return disc_entry_out(disc_table().child(folder, child), out);
}

// A read of a file a mod changed: the crash report names the last one (galaxy::crash). The disc thread only.
void note_mod_file(uint64_t offset) {
    const std::string* path = nullptr;
    const galaxy::mods::DiscNode* file = gMods->disc.placed_file(offset, &path);
    if (file == nullptr) return;
    static const galaxy::mods::DiscNode* last = nullptr;
    static std::string mod;
    if (file != last) {
        last = file;
        // The mod is the folder under the mods folder its first host file is in.
        mod = "a mod";
        const fs::path mods = galaxy::mods::mods_folder();
        for (const galaxy::mods::DiscPiece& piece : file->pieces) {
            if (piece.source != galaxy::mods::DiscPiece::Source::HostFile) continue;
            const fs::path relative = piece.path.lexically_relative(mods);
            if (!relative.empty() && relative.begin()->native() != fs::path("..").native()) {
                mod = relative.begin()->string();
            }
            break;
        }
    }
    galaxy::crash::note_mod_read(mod, *path);
}

void svc_disc_read(void*, int32_t file, uint32_t file_offset, uint32_t size, void* dst, uint32_t slot) {
    gDiscReads.fetch_add(1, std::memory_order_relaxed);
    // Within the file, or up to 31 bytes past its end (reads rounded up to 32 bytes), as the console allowed.
    const std::optional<galaxy::disc::Entry> entry = disc_table().entry(file);
    if (!entry || entry->folder || file_offset > entry->length ||
        uint64_t{file_offset} + size >= uint64_t{entry->length} + 32) {
        gModule.disc_complete(slot, -1);
        return;
    }
    const uint64_t offset = entry->position + file_offset;
    // GALAXY_TEST_DISC_DELAY_US: a slow drive (each read takes this long), so waits on loads are exercised.
    static const uint32_t test_delay_us = [] {
        const char* value = std::getenv("GALAXY_TEST_DISC_DELAY_US");
        return value != nullptr ? static_cast<uint32_t>(std::strtoul(value, nullptr, 10)) : 0u;
    }();
    gDisc.post([=] {
        if (test_delay_us != 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(test_delay_us));
        }
        if (gMods != nullptr && !gMods->fst.empty()) {
            // The mods' disc: the partition, or a changed file's pieces past it.
            if (!gMods->disc.read(offset, size, static_cast<uint8_t*>(dst), gPak.data)) {
                gModule.disc_complete(slot, -1);
                return;
            }
            note_mod_file(offset);
        } else if (offset > gPak.size || gPak.size - offset < size) {
            gModule.disc_complete(slot, -1);
            return;
        } else {
            std::memcpy(dst, gPak.data + offset, size);
        }
#if defined(NEBULA_HOST_RENDERER)
        // The console's disc DMA writes memory the GPU sees.
        mark_written(dst, size);
#endif
        gModule.disc_complete(slot, (int32_t)size);
    });
}

const uint8_t* svc_disc_dol(void*, size_t* size) {
    if (gMods != nullptr && !gMods->dol.empty()) {
        *size = gMods->dol.size();
        return gMods->dol.data();
    }
    *size = gPak.dol_size;
    return gPak.dol;
}

const char* svc_disc_id(void*) {
    return gPak.id;
}

std::atomic<uint32_t> gRetraces{0};
std::atomic<uint64_t> gSceneFrames{0};  // svc_test_scene_frame's count (GALAXY_TEST_SCRIPT_CLOCK=frames)
void svc_vi_frame(void*, uint32_t xfb, int black, uint32_t retrace) {
    (void)xfb;
    gRetraces.store(retrace, std::memory_order_relaxed);
#if defined(NEBULA_HOST_RENDERER)
    // GALAXY_TRACE_VI_TIMES=1: each retrace's time, where a scripted press begins (press-to-present measurements).
    static const bool trace_vi = [] {
        const char* value = std::getenv("GALAXY_TRACE_VI_TIMES");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
    }();
    if (trace_vi) std::fprintf(stderr, "[latency] retrace vi=%u qpc=%lld\n", retrace, static_cast<long long>(galaxy::qpc_now()));
#endif
#if defined(NEBULA_HOST_RENDERER)
#if defined(_WIN32)
    galaxy::timeline::record(galaxy::timeline::Event::GuestFrame, retrace, xfb);
#endif
    if (retrace >= kBootRetraces) {
        end_boot_fast("start's retraces run out");
    }
    {
        // While the start runs ahead, one frame a display period goes to the screen.
        bool present = true;
        if (gBootFast.load(std::memory_order_relaxed)) {
            static Clock::time_point shown{};
            const Clock::time_point now = Clock::now();
            present = now - shown >= std::chrono::milliseconds(16);
            if (present) shown = now;
        }
        // The renderer's newest picture, or black while the game shows black.
        submit_frame(present, black != 0);
    }
    // GALAXY_TEST_MII_IMPORT=<retrace>:<file>: imports the file at that retrace, as the settings do.
    {
        static const char* const test_import = std::getenv("GALAXY_TEST_MII_IMPORT");
        static bool test_imported = false;
        if (test_import != nullptr && !test_imported) {
            const char* const colon = std::strchr(test_import, ':');
            if (colon != nullptr && retrace >= std::strtoul(test_import, nullptr, 10)) {
                test_imported = true;
                const std::filesystem::path file(colon + 1);
                std::string message;
                (void)galaxy::host::import_mii_files(std::span<const std::filesystem::path>(&file, 1u), &message);
                std::fprintf(stderr, "[mii] test import at %u: %s\n", retrace, message.c_str());
            }
        }
    }
    // Miis imported from the settings while the game runs: the game reads its slots again.
    if (const auto placed = galaxy::host::take_mii_installs(); !placed.empty()) {
        gModule.mii_changed();
        std::fprintf(stderr, "[mii] %zu imported while running\n", placed.size());
    }
    // The output's shape (the window's, or the chosen aspect) is latched once this retrace's picture is presented:
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
// latched output shape; headless, the Wii's 16:9.
uint32_t svc_display_extent(void*) {
#if defined(NEBULA_HOST_RENDERER)
    return static_cast<uint32_t>(galaxy::g_experimental_dynamic_aspect_word.load(std::memory_order_acquire));
#else
    return 0;
#endif
}

void svc_scene_surround(void*, float intensity) {
#if defined(NEBULA_HOST_RENDERER)
    galaxy::render::set_safety_scene_surround(intensity);
#else
    (void)intensity;
#endif
}

// The game's lights (NEBULA_LIGHT_*) into the renderer's light registry (game_lights.cpp), which the ray tracer reads.
void svc_game_light(void*, uint32_t kind, uint64_t id, const float* v) {
#if defined(NEBULA_HOST_RENDERER)
    switch (kind) {
    case NEBULA_LIGHT_SUN:
        galaxy::render::set_game_sun({v[0], v[1], v[2]}, v[3]);
        break;
    case NEBULA_LIGHT_STAR_CORE:
        galaxy::render::set_star_core({v[0], v[1], v[2]}, v[3]);
        break;
    case NEBULA_LIGHT_POINT:
        galaxy::render::note_game_point_light(id, {v[0], v[1], v[2]}, {v[3], v[4], v[5]}, v[6]);
        break;
    case NEBULA_LIGHT_POINT_REQUEST:
        galaxy::render::note_game_point_request(id, {v[0], v[1], v[2]}, {v[3], v[4], v[5]}, v[6]);
        break;
    case NEBULA_LIGHT_WORLD:
        galaxy::render::note_game_world_light({v[0], v[1], v[2]}, {v[3], v[4], v[5]});
        break;
    case NEBULA_LIGHT_AREA:
    case NEBULA_LIGHT_PLAYER: {
        galaxy::render::GameAreaLight light{};
        light.ambient = {v[0], v[1], v[2]};
        for (unsigned i = 0; i < 2u; ++i) {
            const float* l = v + 3 + 7 * i;
            light.lights[i].color = {l[0], l[1], l[2]};
            light.lights[i].position = {l[3], l[4], l[5]};
            light.lights[i].follows_camera = l[6] != 0.0f;
        }
        galaxy::render::set_game_area_light(kind == NEBULA_LIGHT_AREA ? galaxy::render::GameAreaLightKind::Stage
                                                                  : galaxy::render::GameAreaLightKind::Player,
                                        light);
        break;
    }
    default:
        break;
    }
#else
    (void)kind;
    (void)id;
    (void)v;
#endif
}

uint32_t svc_option(void*, uint32_t what) {
    switch (what) {
    case NEBULA_OPTION_TEXT_SPEED:
#if defined(NEBULA_HOST_RENDERER)
        return std::clamp(galaxy::get_runtime_settings().text_speed, 1u, 4u);
#else
        return 1;
#endif
    case NEBULA_OPTION_DRAW_IDENTITIES:
#if defined(NEBULA_HOST_RENDERER)
        return galaxy::render::draw_identity_wanted() ? 1u : 0u;
#else
        return 0;
#endif
    case NEBULA_OPTION_RT_SHADOW_CASTERS:
#if defined(NEBULA_HOST_RENDERER)
        return galaxy::render::rt_shadow_casters_wanted() ? 1u : 0u;
#else
        return 0;
#endif
    case NEBULA_OPTION_LOD_SCALE:
#if defined(NEBULA_HOST_RENDERER)
        return galaxy::render::scene_display_scale();
#else
        return 1;
#endif
    case NEBULA_OPTION_BLOOM:
#if defined(NEBULA_HOST_RENDERER)
        return galaxy::render::bloom_route();
#else
        return 0;
#endif
    case NEBULA_OPTION_RT_PREPARATION:
#if defined(NEBULA_HOST_D3D12)
        switch (galaxy::render::rt_prepare::first_start()) {
        case galaxy::render::rt_prepare::FirstStart::Opening: return 1u;
        case galaxy::render::rt_prepare::FirstStart::Compiling: return 2u;
        default: return 0u;
        }
#else
        return 0;
#endif
    case NEBULA_OPTION_RT_PREPARE_PROGRESS:
#if defined(NEBULA_HOST_D3D12)
        return static_cast<uint32_t>(galaxy::render::rt_prepare::progress() * 10000.0f + 0.5f);
#else
        return 0;
#endif
    case NEBULA_OPTION_TEST_DUMP_LAYOUTS: {
        static const bool dump = [] {
            const char* value = std::getenv("GALAXY_TEST_DUMP_LAYOUTS");
            return value != nullptr && value[0] == '1';
        }();
        return dump ? 1u : 0u;
    }
    case NEBULA_OPTION_TEST_LOD: {
        static const uint32_t lod = [] {
            const char* value = std::getenv("GALAXY_TEST_LOD");
            return value != nullptr && value[0] >= '1' && value[0] <= '3' ? static_cast<uint32_t>(value[0] - '0') : 0u;
        }();
        return lod;
    }
    default:
        return 0;
    }
}

static_assert(int{galaxy::saves::kDone} == NEBULA_SAVE_DONE && int{galaxy::saves::kNoFile} == NEBULA_SAVE_NO_FILE &&
                  int{galaxy::saves::kTooLarge} == NEBULA_SAVE_TOO_LARGE &&
                  int{galaxy::saves::kFailed} == NEBULA_SAVE_FAILED,
              "save results are the game's");

// A save file operation on the save worker (the game polls the status): galaxy/save_files.h in the game's save folder
// (gSaveDataPath under the NAND root, or a mod's save folder).
void svc_save_request(void*, const NebulaSaveRequest* request) {
    gFsOps.fetch_add(1, std::memory_order_relaxed);
    // GALAXY_TEST_NAND_DELAY_US: a slow drive for saves (each operation takes this long), so the windows that open
    // and close around a save are exercised as on a busy disk.
    static const uint32_t test_delay_us = [] {
        const char* value = std::getenv("GALAXY_TEST_NAND_DELAY_US");
        return value != nullptr ? static_cast<uint32_t>(std::strtoul(value, nullptr, 10)) : 0u;
    }();
    *request->status = NEBULA_SAVE_PENDING;
    const NebulaSaveRequest r = *request;
    const std::string name = r.name != nullptr ? r.name : "";
    gFs.post([r, name] {
        if (test_delay_us != 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(test_delay_us));
        }
        fs::path folder;
        galaxy::saves::Result result = galaxy::saves::kFailed;
        if (nand_path(gSaveDataPath.c_str(), &folder)) {
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
        std::atomic_ref<int32_t>(*const_cast<int32_t*>(r.status)).store(result, std::memory_order_release);
    });
}

// The system settings (SYSCONF) of a new console, as the game reads them. The pointer mapping (native_input's
// virtual sensor bar) assumes the bar above the screen.
uint32_t svc_sc_get(void*, uint32_t item) {
    switch (item) {
    case NEBULA_SC_LANGUAGE:
#if defined(NEBULA_HOST_RENDERER)
        return static_cast<uint32_t>(galaxy::game_language_for_boot());  // the language setting, fixed at boot
#else
        return 1;  // English
#endif
    case NEBULA_SC_SOUND_MODE:
        return 1;  // stereo
    case NEBULA_SC_ASPECT_RATIO:
        return 1;  // 16:9
    case NEBULA_SC_PROGRESSIVE_MODE:
        return 0;
    case NEBULA_SC_EURGB60_MODE:
        return 1;
    case NEBULA_SC_SCREEN_SAVER_MODE:
        return 1;  // IPL.SSV
    case NEBULA_SC_MOTOR_MODE:
        return 1;
    default:
        return 0;
    }
}

// Player 1: Nebula's controls (the bindings, keyboard and mouse, controllers; a real Wii Remote while it is in use)
// as a remote with a Nunchuk, connected from the start. The host takes a sample every 10 ms and one each time the
// game reads (svc_input_read), when it starts its 10 ms anew; the game gets every sample since its last read.
// Headless, the remote lies still: one sample per read, nothing pressed.
constexpr uint32_t kInputSamples = 120;  // a read takes at most the newest 1.2 s

NebulaInputSample rest_input_sample() {
    NebulaInputSample sample{};
    sample.roll[0] = 1.0f;
    sample.distance = (0.2f / 0.383864f) / 0.3125f;
    sample.gravity[2] = 1.0f;
    sample.nunchuk_gravity[2] = 1.0f;
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
        sample.gravity[i] = input.gravity[i];
        sample.nunchuk_gravity[i] = input.nunchuk_gravity[i];
    }
    sample.shake = static_cast<uint8_t>((input.shake ? NEBULA_SHAKE_REMOTE : 0u) |
                                        (input.nunchuk_shake ? NEBULA_SHAKE_NUNCHUK : 0u));
    return sample;
}

// Player 1's samples not yet read, oldest first; past kInputSamples the oldest go.
class InputSamples {
public:
    void push(const NebulaInputSample& sample) {
        const std::lock_guard<std::mutex> lock(lock_);
        ring_[(head_ + count_) % kInputSamples] = sample;
        if (count_ < kInputSamples) {
            ++count_;
        } else {
            head_ = (head_ + 1) % kInputSamples;
        }
    }
    uint32_t take(NebulaInputSample* out, uint32_t max) {
        const std::lock_guard<std::mutex> lock(lock_);
        const uint32_t count = std::min(count_, max);
        const uint32_t skip = count_ - count;
        for (uint32_t i = 0; i < count; ++i) {
            out[i] = ring_[(head_ + skip + i) % kInputSamples];
        }
        head_ = (head_ + count_) % kInputSamples;
        count_ = 0;
        return count;
    }

private:
    std::mutex lock_;
    std::array<NebulaInputSample, kInputSamples> ring_{};
    uint32_t head_ = 0;
    uint32_t count_ = 0;
};

InputSamples gInputSamples;
// One sample is taken at a time: on the input thread every 10 ms and on the game's thread when it reads.
std::mutex gInputSampleLock;
std::atomic<int64_t> gInputReadNs{std::numeric_limits<int64_t>::min() / 2};  // the game's last read, steady clock
std::atomic<bool> gInputRead{false};  // the game has read its input (the 10 ms samples start then)
std::atomic<bool> gControlsInput{true};  // player 1 is the controls (not a real remote)
std::atomic<uint8_t> gInputNunchuk{1};
std::atomic<uint8_t> gInputBattery{4};

// The controls now (under gInputSampleLock). Each sample renews the motor's state (a stale one stops).
NebulaInputSample controls_sample() {
    static const bool frame_clock = [] {
        const char* value = std::getenv("GALAXY_TEST_SCRIPT_CLOCK");
        return value != nullptr && std::strcmp(value, "frames") == 0;
    }();
    const int32_t mode = gStarPointerMode.load(std::memory_order_relaxed);
    galaxy::host::native_game_rumble(gMotor.load(std::memory_order_relaxed), mode);
    return input_sample(galaxy::host::native_game_input(
        frame_clock ? gSceneFrames.load(std::memory_order_relaxed) : gRetraces.load(std::memory_order_relaxed), mode,
        static_cast<uint8_t>(gTiltSection.load(std::memory_order_relaxed))));
}
#endif

uint32_t svc_input_read(void*, uint32_t player, NebulaInputDevice* device, NebulaInputSample* samples, uint32_t max) {
    if (device != nullptr) {
        *device = {};
        if (player == 0) {
            device->connected = 1;
#if defined(NEBULA_HOST_RENDERER)
            device->nunchuk = gInputNunchuk.load(std::memory_order_relaxed);
            device->battery = gInputBattery.load(std::memory_order_relaxed);
#else
            device->nunchuk = 1;
            device->battery = 4;
#endif
        }
    }
    if (player != 0 || samples == nullptr || max == 0) {
        return 0;
    }
#if defined(NEBULA_HOST_RENDERER)
    gInputRead.store(true, std::memory_order_release);
    // GALAXY_INPUT_READ_SAMPLE=0: only the 10 ms samples (the timing before samples followed the game's reads).
    static const bool sample_on_read = [] {
        const char* value = std::getenv("GALAXY_INPUT_READ_SAMPLE");
        return value == nullptr || value[0] != '0';
    }();
    if (sample_on_read && gControlsInput.load(std::memory_order_acquire)) {
        const std::lock_guard<std::mutex> lock(gInputSampleLock);
        gInputSamples.push(controls_sample());
        gInputReadNs.store(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count(),
                           std::memory_order_release);
    }
    return gInputSamples.take(samples, max);
#else
    samples[0] = rest_input_sample();
    return 1;
#endif
}

void svc_input_control(void*, uint32_t player, uint32_t what, uint32_t value) {
    if (player != 0 || what != NEBULA_INPUT_RUMBLE) {
        return;  // player 1's controller is never let go; there are no others
    }
    gMotor.store(value != 0, std::memory_order_relaxed);
#if defined(NEBULA_REAL_WIIMOTE)
    if (!galaxy::host::real_wiimote_in_use()) {
        galaxy::host::native_game_rumble(value != 0, gStarPointerMode.load(std::memory_order_relaxed));
    }
    galaxy::host::real_wiimote_set_rumble(value != 0);
#elif defined(NEBULA_HOST_RENDERER)
    galaxy::host::native_game_rumble(value != 0, gStarPointerMode.load(std::memory_order_relaxed));
#endif
}

#if defined(NEBULA_HOST_RENDERER)
void input_thread() {
    name_thread(L"Nebula Host Input");
    while (!gInputRead.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const auto period = std::chrono::milliseconds(10);
    auto next = Clock::now() + period;
#if defined(NEBULA_REAL_WIIMOTE)
    galaxy::host::ensure_real_wiimote_started();
    // The remote's data reports, IR camera on: with its Nunchuk, buttons, accelerometer, IR and extension; without.
    galaxy::host::real_wiimote_set_report(0x37);
    galaxy::host::real_wiimote_set_ir(true);
    // A real Wii Remote: `remote` while one is connected (its calibration), `real` while the game plays with it.
    std::optional<galaxy::host::RealWiimoteConnection> remote;
    galaxy::input::wiimote::Reader reader;
    bool real = false;
    std::optional<galaxy::input::DeviceKey> device_when_real;
    const auto use_remote = [&](bool on) {
        if (on == real || (on && !remote.has_value())) return;
        real = on;
        galaxy::host::real_wiimote_set_active(on);
        gControlsInput.store(!on, std::memory_order_release);
        gInputNunchuk.store(on ? (remote->nunchuk ? 1u : 0u) : 1u, std::memory_order_relaxed);
        gInputBattery.store(on ? remote->battery : 4u, std::memory_order_relaxed);
        device_when_real = galaxy::host::last_input_device();
        next = Clock::now() + period;
        log_line("[wiimote]", on ? "in use" : "keyboard, mouse and controllers in use");
    };
#else
    constexpr bool real = false;
#endif
    for (;;) {
#if defined(NEBULA_REAL_WIIMOTE)
        // A real Wii Remote's reports become samples as they arrive: in the Wii Remote input mode while it is
        // connected, in Auto from its first button press until a key, a mouse button or a controller is used. The
        // keyboard, mouse and controllers are read every 10 ms meanwhile (for that, and the settings menu).
        std::array<uint8_t, 22> real_report{};
        std::size_t real_size = 0;
        galaxy::host::RealWiimoteConnection real_connection{};
        const auto until_tick =
            std::chrono::duration_cast<std::chrono::milliseconds>(next - Clock::now()).count();
        const galaxy::host::RealWiimoteEvent event = galaxy::host::real_wiimote_wait(
            real ? static_cast<uint32_t>(std::clamp<long long>(until_tick, 0, 10)) : 0u, real_report, real_size,
            real_connection);
        const bool wii_remote_mode =
            galaxy::get_runtime_input_mode_state().mode == galaxy::RuntimeInputMode::RealWiimote;
        if (event == galaxy::host::RealWiimoteEvent::Connected) {
            remote = real_connection;
            reader.connect(real_connection.eeprom, real_connection.extension_calibration, real_connection.nunchuk);
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
            if (real && reader.read(bytes, input)) gInputSamples.push(input_sample(input));
            continue;
        }
        if (real && Clock::now() < next) {
            continue;
        }
#endif
        sleep_until_precise(next);
        // The game read within the last 10 ms: the next sample 10 ms after its.
        const Clock::time_point read{std::chrono::nanoseconds(gInputReadNs.load(std::memory_order_acquire))};
        if (!real && read + period > next) {
            next = read + period;
            continue;
        }
        next += period;
        const std::lock_guard<std::mutex> lock(gInputSampleLock);
        const NebulaInputSample sample = controls_sample();
#if defined(NEBULA_REAL_WIIMOTE)
        if (real) {
            // Another device used (in Auto): back to the keyboard, mouse and controllers, with this sample.
            if (galaxy::get_runtime_input_mode_state().mode == galaxy::RuntimeInputMode::RealWiimote ||
                galaxy::host::last_input_device() == device_when_real) {
                continue;
            }
            use_remote(false);
        }
#endif
        gInputSamples.push(sample);
    }
}
#endif

// The remote speaker's sound is not played yet (I-06: the host's speaker mixer, galaxy/wiimote_speaker.h).
void svc_speaker_samples(void*, uint32_t, const int16_t*, uint32_t) {}

// The JAudio DSP program, run on the host: it mixes each subframe when the CPU releases its last channel group
// (JASDSPChannel::updateAll's releaseHalt(3), a one-word command). Headless, the subframe completes unmixed.
#if defined(NEBULA_HOST_RENDERER)
// A pointer into the game's memory as the guest address the mixer reads by (0 outside it).
#endif

void svc_jaudio_setup(void*, uint32_t channels, NebulaJAudioChannel* records, const int16_t* resample_filter,
                      const int16_t* adpcm_filter, const NebulaJAudioFxLine* fx_lines) {
#if defined(NEBULA_HOST_RENDERER)
    gAudio->setup(channels, records, resample_filter, adpcm_filter, fx_lines);
#else
    (void)channels, (void)records, (void)resample_filter, (void)adpcm_filter, (void)fx_lines;
#endif
}

void svc_jaudio_aux(void*, const void* base, uint32_t size) {
#if defined(NEBULA_HOST_RENDERER)
    gAudio->set_aux(base, size);
#else
    (void)base, (void)size;
#endif
}

void svc_jaudio_frame(void*, uint32_t subframes, uint32_t mixer_level, int16_t* left, int16_t* right) {
#if defined(NEBULA_HOST_RENDERER)
    gAudio->frame(subframes, mixer_level, left, right);
#else
    (void)subframes, (void)mixer_level, (void)left, (void)right;
#endif
}

int svc_jaudio_mix(void*, uint32_t group, uint32_t active) {
#if defined(NEBULA_HOST_RENDERER)
    const int subframe = gAudio->mix(group, active) ? 1 : 0;
#else
    (void)active;
    const int subframe = group == 3u;
#endif
    gDspFrames.fetch_add(subframe, std::memory_order_relaxed);
    return subframe;
}

void svc_ai_dma(void*, const void* samples, uint32_t size) {
    std::lock_guard<std::mutex> lock(gAiLock);
    gAiLatchedStart = samples;
    gAiLatchedSize = size;
    gAiWake.notify_one();
}

void svc_ai_control(void*, uint32_t what, uint32_t value) {
    std::lock_guard<std::mutex> lock(gAiLock);
    if (what == NEBULA_AI_START) {
        gAiRunning = true;
    } else if (what == NEBULA_AI_STOP) {
        gAiRunning = false;
    } else if (what == NEBULA_AI_RATE) {
        gAiRate = value;
    }
    gAiWake.notify_one();
}

int svc_hbm(void*, uint32_t what, uint32_t value) {
#if defined(NEBULA_HOST_RENDERER)
    switch (what) {
    case NEBULA_HBM_OPEN:
        galaxy::render::home_menu_open();
        return 0;
    case NEBULA_HBM_POLL: {
        // GALAXY_TEST_HOME_ROW=<row>:<polls>: picks a row by the keys a player would press, and answers yes when it
        // asks (reset, leave).
        static const char* test = std::getenv("GALAXY_TEST_HOME_ROW");
        static unsigned polls = 0;
        if (test != nullptr && galaxy::render::home_menu_visible()) {
            unsigned row = 0, after = 0;
            if (std::sscanf(test, "%u:%u", &row, &after) == 2 && ++polls == after) {
                for (unsigned i = 0; i < row; ++i) {
                    galaxy::render::native_settings_post_key(VK_DOWN);
                }
                galaxy::render::native_settings_post_key(VK_RETURN);
                if (row != 0) {
                    galaxy::render::native_settings_post_key(VK_RIGHT);
                    galaxy::render::native_settings_post_key(VK_RETURN);
                }
            }
        }
        return galaxy::render::home_menu_poll();
    }
    case NEBULA_HBM_CLOSE:
        galaxy::render::home_menu_close();
        return 0;
    case NEBULA_HBM_VIEW:
        return galaxy::render::home_menu_view(value != 0u);
    case NEBULA_HBM_POINT:
        galaxy::render::home_menu_point(value);
        return 0;
    case NEBULA_HBM_CHOOSE:
        galaxy::render::home_menu_choose(value);
        return 0;
    default:
        return 0;
    }
#else
    (void)value;
    return what == NEBULA_HBM_VIEW ? -1 : 0;  // headless: the menu closes at once
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
//   GALAXY_TEST_EVENTS defeat:<model>@frame: the live actors with that model (as actors@ names it) are killed, as their
//     defeat ends: what they leave appears (KuriboChief, the Gateway's big Goomba: its key).
//   GALAXY_TEST_SCRIPT_CLOCK=frames: the scripted input (GALAXY_INPUT_*) counts scene frames instead of retraces, so a
//     run's game frames take the same input whatever its renderer's speed (builds compared frame for frame).
std::atomic<bool> gTourDone{false};

int svc_test_scene_frame(void*, NebulaTestAction* action) {
    struct Stop {
        std::string stage;
        int32_t scenario;
    };
    struct Teleport {
        float x, y, z;
        uint64_t frame;
    };
    static std::vector<Stop> stops;
    static std::vector<Teleport> teleports;
    struct Event {
        uint32_t kind;
        uint64_t frame;
        int32_t value;
        std::string name;  // NEBULA_TEST_KILL_ACTOR's model
    };
    static std::vector<Event> events;
    static uint64_t tour_frames = 1200;
    static bool touring = false;
    static bool loaded = false;
    static size_t next = 0;
    static uint64_t scene_frames = 0, since_stop = 0, stop_frame = 0;
    if (!loaded) {
        loaded = true;
        if (const char* tour = std::getenv("GALAXY_TEST_GALAXY_TOUR")) {
            if (FILE* file = std::fopen(tour, "r")) {
                char stage[64];
                int scenario = 0;
                while (std::fscanf(file, "%63s %d", stage, &scenario) == 2) {
                    stops.push_back({stage, scenario});
                }
                std::fclose(file);
            }
            touring = true;
            if (const char* frames = std::getenv("GALAXY_TEST_GALAXY_TOUR_FRAMES")) {
                tour_frames = std::max<uint64_t>(std::strtoull(frames, nullptr, 10), 60u);
            }
            char text[64];
            std::snprintf(text, sizeof(text), "stops %zu, %llu frames apart", stops.size(),
                          static_cast<unsigned long long>(tour_frames));
            log_line("tour:", text);
        }
        if (const char* list = std::getenv("GALAXY_TEST_EVENTS")) {
            std::string entries = list;
            size_t start = 0;
            while (start < entries.size()) {
                const size_t end = std::min(entries.find('/', start), entries.size());
                const std::string entry = entries.substr(start, end - start);
                const size_t at = entry.find('@');
                if (at != std::string::npos) {
                    const std::string kind = entry.substr(0, at);
                    const uint64_t frame = std::strtoull(entry.c_str() + at + 1, nullptr, 10);
                    if (kind == "kill") {
                        events.push_back({NEBULA_TEST_KILL_PLAYER, frame, 0});
                    } else if (kind == "star") {
                        events.push_back({NEBULA_TEST_GET_STAR, frame, -1});
                    } else if (kind == "grandstar") {
                        events.push_back({NEBULA_TEST_GET_GRAND_STAR, frame, -1});
                    } else if (kind == "mario" || kind == "luigi") {
                        events.push_back({NEBULA_TEST_SET_PLAYER, frame, kind == "luigi" ? 1 : 0});
                    } else if (kind == "shake") {
                        events.push_back({NEBULA_TEST_SHAKE_CAMERA, frame, 0});
                    } else if (kind == "actors") {
                        events.push_back({NEBULA_TEST_LIST_ACTORS, frame, 0});
                    } else if (kind.rfind("defeat:", 0) == 0) {
                        events.push_back({NEBULA_TEST_KILL_ACTOR, frame, 0, kind.substr(7)});
                    } else if (kind == "wait") {
                        events.push_back({0, frame, 0});
                    } else if (kind.rfind("movie", 0) == 0) {
                        events.push_back({NEBULA_TEST_START_MOVIE, frame, std::atoi(kind.c_str() + 5)});
                    }
                }
                start = end + 1;
            }
        }
        if (const char* list = std::getenv("GALAXY_TEST_TELEPORT")) {
            std::string entries = list;
            size_t start = 0;
            while (start < entries.size()) {
                const size_t end = std::min(entries.find('/', start), entries.size());
                Teleport t{};
                unsigned long long frame = 0;
                if (std::sscanf(entries.substr(start, end - start).c_str(), "%f,%f,%f,%llu", &t.x, &t.y, &t.z,
                                &frame) == 4) {
                    t.frame = frame;
                    teleports.push_back(t);
                }
                start = end + 1;
            }
        }
    }
    ++scene_frames;
    gSceneFrames.store(scene_frames, std::memory_order_relaxed);
#if defined(NEBULA_HOST_RENDERER)
    // The settings' galaxy select (ADVANCED page): a travel the player asked for, through the game's own stage change,
    // and the Grand Stars with the Observatory they power.
    {
        std::size_t stage = 0u;
        std::uint32_t star = 0u;
        if (galaxy::render::take_galaxy_travel(stage, star)) {
            action->kind = NEBULA_TEST_CHANGE_STAGE;
            action->scenario = static_cast<int32_t>(star);
            std::snprintf(action->stage, sizeof(action->stage), "%s", galaxy::render::kGalaxyStages[stage].id);
            return 1;
        }
        if (galaxy::render::take_grand_stars()) {
            action->kind = NEBULA_TEST_SET_GRAND_STARS;
            action->scenario = 6;
            std::snprintf(action->stage, sizeof(action->stage), "%s", galaxy::render::kObservatoryId);
            return 1;
        }
    }
#endif
    static std::vector<std::string> event_flags = [] {
        std::vector<std::string> names;
        if (const char* value = std::getenv("GALAXY_TEST_EVENT_FLAGS")) {
            std::string list = value;
            for (std::size_t start = 0; start < list.size();) {
                const std::size_t comma = std::min(list.find(',', start), list.size());
                if (comma > start) names.push_back(list.substr(start, comma - start));
                start = comma + 1;
            }
        }
        return names;
    }();
    if (!event_flags.empty() && scene_frames >= 2u) {
        action->kind = NEBULA_TEST_SET_EVENT_FLAG;
        std::snprintf(action->stage, sizeof(action->stage), "%s", event_flags.front().c_str());
        log_line("test: event flag", action->stage);
        event_flags.erase(event_flags.begin());
        return 1;
    }
    static const int32_t grand_stars = [] {
        const char* value = std::getenv("GALAXY_TEST_GRAND_STARS");
        return value != nullptr ? std::atoi(value) : 0;
    }();
    static bool grand_stars_set = false;
    if (grand_stars > 0 && !grand_stars_set && scene_frames >= 2u) {
        grand_stars_set = true;
        action->kind = NEBULA_TEST_SET_GRAND_STARS;
        action->scenario = grand_stars;
        action->stage[0] = '\0';  // the stage played starts again with them
        log_line("test:", "grand stars set");
        return 1;
    }
    // The last stop stays for its frames, or until 20 s after the last test event, then the run ends.
    static const uint64_t last_frame = [] {
        uint64_t last = 0;
        for (const Event& event : events) {
            last = std::max(last, event.frame + 1200u);
        }
        for (const Teleport& t : teleports) {
            last = std::max(last, t.frame + 1200u);
        }
        return last;
    }();
    if (touring && next == stops.size() && !gTourDone.load(std::memory_order_relaxed) &&
        ++since_stop >= std::max(tour_frames, last_frame)) {
        gTourDone.store(true, std::memory_order_relaxed);  // the run ends once the last stop has had its frames
        log_line("tour:", "done");
    }
    if (touring && next < stops.size() && ++since_stop >= tour_frames) {
        since_stop = 0;
        stop_frame = scene_frames;
        const Stop& stop = stops[next++];
        action->kind = NEBULA_TEST_CHANGE_STAGE;
        action->scenario = stop.scenario;
        std::snprintf(action->stage, sizeof(action->stage), "%s", stop.stage.c_str());
        char text[128];
        std::snprintf(text, sizeof(text), "%zu/%zu %s %d", next, stops.size(), stop.stage.c_str(), stop.scenario);
        log_line("tour:", text);
        return 1;
    }
    if (touring && stop_frame == 0) {
        return 0;
    }
    const uint64_t frame = scene_frames - stop_frame;
    for (const Event& event : events) {
        if (frame == event.frame && event.kind != 0) {
            action->kind = event.kind;
            action->scenario = event.value >= 0 ? event.value : (next > 0 ? stops[next - 1].scenario : 1);
            std::snprintf(action->stage, sizeof(action->stage), "%s", event.name.c_str());
            char text[64];
            std::snprintf(text, sizeof(text), "event %u at frame %llu", event.kind, static_cast<unsigned long long>(frame));
            log_line("test:", text);
            return 1;
        }
    }
    for (const Teleport& t : teleports) {
        if (frame >= t.frame && frame < t.frame + 30u) {
            action->kind = NEBULA_TEST_PLACE_PLAYER;
            action->position[0] = t.x;
            action->position[1] = t.y;
            action->position[2] = t.z;
            return 1;
        }
    }
    return 0;
}

// A restart starts this executable again with the same arguments (the game's boot, as the console's reset); the
// other power events close the game.
[[noreturn]] void svc_power(void*, uint32_t what) {
    log_line("host:", what == NEBULA_POWER_RESTART ? "restart" : "the game ended");
#if defined(NEBULA_HOST_RENDERER)
    galaxy::achievements::flush();
    galaxy::stats::flush();
#endif
    std::fflush(stdout);
#if defined(_WIN32)
    if (what == NEBULA_POWER_RESTART) {
        wchar_t path[MAX_PATH];
        if (GetModuleFileNameW(nullptr, path, MAX_PATH) != 0) {
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            PROCESS_INFORMATION process{};
            std::wstring command = GetCommandLineW();
            if (CreateProcessW(path, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process)) {
                CloseHandle(process.hThread);
                CloseHandle(process.hProcess);
            }
        }
    }
#endif
    std::_Exit(0);
}

#if defined(NEBULA_HOST_RENDERER)
// A THP frame's video component is one baseline JPEG whose SOF0 gives the movie's size; the decoder writes its planes
// in GX's I8 tiling. The planes are then the renderer's to re-read, as after the console's decode.
int32_t svc_thp_video_decode(void*, const void* frame, uint32_t size, void* y, void* u, void* v) {
    gMovieFrames.fetch_add(1, std::memory_order_relaxed);
    if (galaxy::render::movie_replacement_showing()) {
        // A replacement video's frames show in place of these planes: their decode would be unseen work.
        gMovieSkipped.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }
    const auto decode_start = std::chrono::steady_clock::now();
    struct DecodeTime {
        std::chrono::steady_clock::time_point start;
        ~DecodeTime() {
            gMovieDecodeNs.fetch_add(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                         std::chrono::steady_clock::now() - start).count()),
                                     std::memory_order_relaxed);
        }
    } decode_time{decode_start};
    const auto* p = static_cast<const uint8_t*>(frame);
    if (size < 4 || p[0] != 0xFF || p[1] != 0xD8) {
        return -1;
    }
    uint16_t width = 0, height = 0;
    for (size_t i = 2; i + 9 <= size && p[i] == 0xFF && p[i + 1] != 0xDA;
         i += 2 + ((size_t(p[i + 2]) << 8) | p[i + 3])) {
        if (p[i + 1] == 0xC0) {
            height = static_cast<uint16_t>((p[i + 5] << 8) | p[i + 6]);
            width = static_cast<uint16_t>((p[i + 7] << 8) | p[i + 8]);
        }
    }
    if (width == 0 || height == 0) {
        return -1;
    }
    const size_t end = size;
    const auto plane = [](size_t w, size_t h) { return ((w + 7) / 8) * ((h + 3) / 4) * 32; };
    const size_t luma = plane(width, height);
    const size_t chroma = plane((width + 1u) / 2u, (height + 1u) / 2u);
    const int32_t result = galaxy::thp::decode_video(
        {p, end}, width, height, {static_cast<uint8_t*>(y), luma}, {static_cast<uint8_t*>(u), chroma},
        {static_cast<uint8_t*>(v), chroma});
    return result;
}
#else
int32_t svc_thp_video_decode(void*, const void*, uint32_t, void*, void*, void*) {
    return 0;
}
#endif

}  // namespace

#if defined(NEBULA_HOST_RENDERER)
// The renderer settings read from the environment, at their shipped values (installer/runtime-env.json) where the
// environment does not set them.
void apply_shipped_renderer_defaults() {
    static const struct {
        const char* name;
        const char* value;
    } kDefaults[] = {
        {"GALAXY_ALLOW_TEARING", "0"},
        {"GALAXY_ASYNC_RENDER_THREAD", "1"},
        {"GALAXY_CAPTURE_MOUSE_BUTTONS", "1"},
        {"GALAXY_D3D_DEBUG", "0"},
        {"GALAXY_DISPLAY_LIST_DIRTY_INVALIDATE", "0"},
        {"GALAXY_FLIP_DISCARD", "1"},
        {"GALAXY_FRAME_RATE_DECOUPLE", "0"},
        {"GALAXY_FRAME_TELEMETRY", "1"},
        {"GALAXY_GPU_TIMESTAMPS", "1"},
        {"GALAXY_GX_DECODED_VERTEX_CACHE", "1"},
        {"GALAXY_GX_FRAME_TIMING_SAMPLE", "0"},
        {"GALAXY_GX_PSO_CYCLES", "0"},
        {"GALAXY_GX_SORTED_VERTEX_INVALIDATION", "1"},
        {"GALAXY_GX_UBER", "1"},
        {"GALAXY_HIDE_OS_CURSOR", "1"},
        {"GALAXY_MOUSE_IR_CURSOR_POLL", "auto"},
        {"GALAXY_MOUSE_IR_MESSAGE_RECENT_MS", "120"},
        {"GALAXY_PE_EVENTS_GPU_FENCE", "1"},
        {"GALAXY_PSO_PREWARM", "1"},
        {"GALAXY_PSO_PREWARM_BLOCKING", "1"},
        {"GALAXY_PSO_PREWARM_BLOCKING_MS", "30000"},
        {"GALAXY_PSO_PREWARM_LIMIT", "4096"},
        {"GALAXY_REALTIME_VI", "1"},
        {"GALAXY_RENDER_IMMUTABLE_RANGE_OWNERSHIP", "0"},
        {"GALAXY_RENDER_LIVE_MEMORY", "1"},
        {"GALAXY_RENDER_LIVE_MEMORY_WAIT", "0"},
        {"GALAXY_RENDER_MEMORY_SNAPSHOT", "0"},
        {"GALAXY_RENDER_MEMORY_SNAPSHOT_COMPACT", "1"},
        {"GALAXY_RENDER_QUEUE_DEPTH", "2"},
        {"GALAXY_RENDER_WORKER_MEMORY_SNAPSHOT", "0"},
        {"GALAXY_SKIP_DUPLICATE_XFB_PRESENTS", "1"},
        {"GALAXY_SMOOTH_REALTIME_PRESENT", "1"},
        {"GALAXY_SMOOTH_REALTIME_PRESENT_MAX_WAIT_US", "2000"},
        {"GALAXY_SWAP_CHAIN_FRAME_LATENCY", "1"},
        {"GALAXY_VI_RENDER_FIFO_WAIT", "0"},
        {"GALAXY_WAIT_FOR_FRAME_LATENCY", "0"},
        {"GALAXY_XFB_MAX_PRESERVE_AGE", "30"},
    };
    for (const auto& setting : kDefaults) {
        size_t length = 0;
        if (getenv_s(&length, nullptr, 0, setting.name) != 0 || length == 0) {
            _putenv_s(setting.name, setting.value);
        }
    }
}
#endif

#if defined(NEBULA_HOST_RENDERER)
#include "galaxy/mod_import.h"

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

std::string utf8_of(const fs::path& path) {
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

// The game id and main.dol from Setup's game.pak, read without mapping it; false (and nothing changed) for a file that
// is not one: the tools then work without it.
bool read_game_dol(const fs::path& path, std::string& id, std::vector<std::uint8_t>& dol) {
    std::ifstream file(path, std::ios::binary);
    std::uint8_t boot[0x440];
    if (!file.read(reinterpret_cast<char*>(boot), sizeof(boot))) return false;
    const std::uint64_t offset = (std::uint64_t)load_be32(boot + 0x420) << 2;
    std::uint8_t header[0x100];
    if (!file.seekg((std::streamoff)offset) || !file.read(reinterpret_cast<char*>(header), sizeof(header))) return false;
    std::uint64_t end = 0x100;
    for (int s = 0; s < 18; s++) {
        const std::uint32_t size = load_be32(header + 0x90 + s * 4);
        const std::uint64_t section_end = (std::uint64_t)load_be32(header + s * 4) + size;
        if (size != 0 && section_end > end) end = section_end;
    }
    // main.dol is a few megabytes; a larger one is not a game.pak.
    if (end > (64u << 20)) return false;
    std::vector<std::uint8_t> bytes((std::size_t)end);
    if (!file.seekg((std::streamoff)offset) || !file.read(reinterpret_cast<char*>(bytes.data()), (std::streamsize)end)) {
        return false;
    }
    id.assign(reinterpret_cast<const char*>(boot), 6);
    dol = std::move(bytes);
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
            // The game's own disc: its id and main.dol (another release's patches are then told apart).
            std::string id;
            std::vector<std::uint8_t> dol;
            if (read_game_dol(args[++i], id, dol)) {
                game_id = id;
                galaxy::mods::set_game_dol(dol);
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
                   ",\"code_patches\":" + std::to_string(mod.code_patches) + ",\"native_patches\":" +
                   std::to_string(mod.native_patches) + ",\"problem\":" + json_text(mod.problem) + ",\"languages\":[";
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
                       ",\"textures\":" + std::to_string(overlap.textures) + "}";
            }
            out += "]}";
        }
        out += "]}";
        std::printf("%s\n", out.c_str());
        return 0;
    }
    int failed = 0;
    std::string out = "{\"results\":[";
    for (std::size_t i = 0; i < sources.size(); ++i) {
        const galaxy::mods::ImportResult result = galaxy::mods::import_mod(sources[i], settings_folder / "mods");
        failed += result.ok ? 0 : 1;
        out += std::string(i ? "," : "") + "{\"ok\":" + (result.ok ? "true" : "false") + ",\"id\":" + json_text(result.id) +
               ",\"replaced\":" + (result.replaced ? "true" : "false") + ",\"files\":" + std::to_string(result.files) +
               ",\"bytes\":" + std::to_string(result.bytes) + ",\"kinds\":" + json_text(result.kinds) +
               ",\"message\":" + json_text(result.message) + "}";
    }
    out += "],\"ok\":" + std::string(failed == 0 && !sources.empty() ? "true" : "false") + "}";
    std::printf("%s\n", out.c_str());
    return failed == 0 && !sources.empty() ? 0 : 1;
}

}  // namespace
#endif

int main(int argc, char** argv) {
#if defined(NEBULA_HOST_RENDERER) && defined(_WIN32)
    if (argc >= 3 && std::strcmp(argv[1], "--crash-report") == 0) {
        return show_crash_report_dialog();
    }
#endif
#if defined(NEBULA_HOST_SDL)
    if (!galaxy::sdl::start()) {
        return 1;
    }
#endif
#if defined(NEBULA_HOST_RENDERER)
    apply_shipped_renderer_defaults();
#endif
#if defined(NEBULA_HOST_RENDERER) && defined(_WIN32)
    // GALAXY_FRAME_TIMELINE=1: the frame pipeline's events (frame_timeline.h), written at exit.
    galaxy::timeline::initialize_from_environment();
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
    // GALAXY_TEST_CPU_AFFINITY: the logical processors the process may use (hex mask; 0xF = two cores with SMT, a
    // 2C/4T laptop's shape).
    if (const char* mask = std::getenv("GALAXY_TEST_CPU_AFFINITY")) {
        SetProcessAffinityMask(GetCurrentProcess(), static_cast<DWORD_PTR>(std::strtoull(mask, nullptr, 16)));
    }
    install_crash_reporter();
#endif
#if defined(NEBULA_HOST_RENDERER)
    if (argc >= 2 && (std::strcmp(argv[1], "--import-mod") == 0 || std::strcmp(argv[1], "--list-mods") == 0 ||
                      std::strcmp(argv[1], "--remove-mod") == 0)) {
        return run_mods_tool();
    }
#endif
    boot_mark("main");
    fs::path pak, nand, module;
    double seconds = 0;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string key = argv[i];
        if (key == "--pak") {
            pak = argv[i + 1];
        } else if (key == "--nand") {
            nand = argv[i + 1];
        } else if (key == "--module") {
            module = argv[i + 1];
        } else if (key == "--seconds") {
            seconds = std::atof(argv[i + 1]);
        } else {
            die("unknown option %s", key.c_str());
        }
    }
    if (pak.empty() || nand.empty()) {
        die("usage: nebula_native_host --pak <game.pak> --nand <dir> [--module <smg_game.dll>] [--seconds N]");
    }
    if (module.empty()) {
#if defined(_WIN32)
        module = fs::path(argv[0]).parent_path() / "smg_game.dll";
#elif defined(__APPLE__)
        module = fs::path(argv[0]).parent_path() / "libsmg_game.dylib";
#else
        module = fs::path(argv[0]).parent_path() / "libsmg_game.so";
#endif
    }
    gNandRoot = nand;
    std::error_code error;
    fs::create_directories(gNandRoot / "tmp", error);  // where saves stage their banner before moving it
#if defined(NEBULA_HOST_RENDERER)
    // The system's Mii database, which every console has: an empty one when this NAND has none (the game treats a
    // missing database as a NAND error when it saves).
    const fs::path mii_database = mii_database_path();
    if (!fs::exists(mii_database, error)) {
        fs::create_directories(mii_database.parent_path(), error);
        const std::vector<std::byte> bytes = galaxy::host::native_game_empty_mii_database();
        if (FILE* file = std::fopen(mii_database.string().c_str(), "wb")) {
            std::fwrite(bytes.data(), 1, bytes.size(), file);
            std::fclose(file);
        }
    }
#endif

    open_pak(pak);
    boot_mark("disc opened");
    load_module(module);
    boot_mark("game module loaded");
#if defined(NEBULA_HOST_RENDERER)
    galaxy::render::set_texture_pack_game_id(gPak.id);  // packs' and dumps' game folders: the disc's release
    {
        // Mii import: the files in the import folder (next to the settings: any console's Mii files, mii_formats.h)
        // join the database before the game reads it; the settings import more while it runs.
        const fs::path settings_folder = galaxy::runtime_settings_folder();
        const fs::path import_folder = settings_folder.empty() ? fs::path{} : settings_folder / "miis";
        if (!import_folder.empty()) {
            fs::create_directories(import_folder, error);
        }
        galaxy::host::set_mii_library(mii_database, import_folder,
                                      [](galaxy::host::MiiRecord& mii) {
                                          galaxy::host::fix_wii_mii(mii);
                                          const NebulaMii fields = nebula_mii(galaxy::host::decode_wii_mii(mii));
                                          return gModule.mii_valid(&fields) != 0u;
                                      });
        std::string message;
        (void)galaxy::host::import_mii_folder(&message);
        std::fprintf(stderr, "[mii] import folder %s: %s (%u in the database)\n", import_folder.string().c_str(),
                     message.c_str(), galaxy::host::mii_library_count());
        (void)galaxy::host::take_mii_installs();  // in the file the game reads at boot
    }
#if defined(NEBULA_HOST_RENDERER)
    // The player's settings (GALAXY_RUNTIME_SETTINGS_PATH in tests) before the mods: the language they choose
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
        const std::string game_id(reinterpret_cast<const char*>(gPak.data), 6);
        char title[9];
        std::snprintf(title, sizeof(title), "%02x%02x%02x%02x", gPak.data[0], gPak.data[1], gPak.data[2], gPak.data[3]);
        gSaveDataPath = std::string("/title/00010000/") + title + "/data";
        fs::create_directories(gNandRoot / gSaveDataPath.substr(1), error);  // the release's own save folder
        // The disc's languages, and those its enabled mods add, before the game's language is first settled (the mods
        // below take their text in it): Auto settles on one the disc has (Japan's disc: Japanese), never on one only a
        // fuller list would offer.
        std::vector<std::string> boot_languages =
            galaxy::mods::language_folders(std::span<const std::uint8_t>(gPak.fst, gPak.fst_size));
#if defined(NEBULA_HOST_RENDERER)
        galaxy::set_disc_languages(boot_languages);
#endif
        if (!settings_folder.empty()) {
            fs::create_directories(settings_folder / "mods", error);
            galaxy::mods::set_mods_folder(settings_folder / "mods", settings_folder / "mods.ini", game_id);
#if defined(NEBULA_HOST_RENDERER)
            for (const galaxy::mods::ModInfo& mod : galaxy::mods::scan_mods(game_id)) {
                if (!mod.enabled) continue;
                for (const std::string& language : mod.languages) {
                    if (std::find(boot_languages.begin(), boot_languages.end(), language) == boot_languages.end()) {
                        boot_languages.push_back(language);
                    }
                }
            }
            galaxy::set_disc_languages(boot_languages);
#endif
            gMods = galaxy::mods::apply_mods(std::span<const std::uint8_t>(gPak.fst, gPak.fst_size),
                                             std::span<const std::uint8_t>(gPak.dol, gPak.dol_size),
                                             static_cast<uint64_t>(gPak.dol - gPak.data), gPak.data, gPak.size, game_id,
                                             gPak.data[7], gPak.data[6],
                                             galaxy::mods::language_folder(
                                                 game_id, static_cast<unsigned>(galaxy::game_language_for_boot())));
        }
        if (gMods != nullptr) {
            galaxy::render::set_texture_pack_mod_folders(gMods->texture_folders);
            galaxy::render::set_movie_mod_folders(gMods->movie_folders);
            std::fprintf(stderr, "[mods] %zu mod(s), %u file patch(es), %u unsupported\n%s", gMods->active.size(),
                         gMods->files_changed, gMods->unsupported, gMods->report.c_str());
            const std::string text = "Nebula mods applied at the last start (" + game_id + ")\n\n" + gMods->report;
            (void)galaxy::io::write_text_file_atomically(settings_folder / "mods-report.txt", text);
            if (!gMods->save_folder.empty()) {
                // Riivolution's clone: the NAND's saves copied into an empty save folder.
                const fs::path nand_saves = gNandRoot / gSaveDataPath.substr(1);
                const bool empty = !fs::exists(gMods->save_folder, error) || fs::is_empty(gMods->save_folder, error);
                fs::create_directories(gMods->save_folder, error);
                if (empty && gMods->save_clone && fs::exists(nand_saves, error)) {
                    fs::copy(nand_saves, gMods->save_folder, fs::copy_options::recursive, error);
                }
            }
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
            // The disc as the game reads it: its version and its languages (the mods' among them), which the
            // language setting offers.
            const std::vector<std::string> languages =
                gMods != nullptr && !gMods->fst.empty()
                    ? galaxy::mods::language_folders(gMods->fst)
                    : galaxy::mods::language_folders(std::span<const std::uint8_t>(gPak.fst, gPak.fst_size));
            std::string list;
            for (const std::string& language : languages) list += (list.empty() ? "" : ", ") + language;
            char text[256];
            std::snprintf(text, sizeof(text), "%s revision %u: %s", game_id.c_str(), static_cast<unsigned>(gPak.data[7]),
                          list.empty() ? "no language folders" : list.c_str());
            log_line("disc:", text);
#if defined(NEBULA_HOST_RENDERER)
            galaxy::set_disc_languages(languages);
#endif
        }
        if (gMods == nullptr && !settings_folder.empty() && fs::exists(settings_folder / "mods-report.txt", error)) {
            // No mod this start: an earlier start's report must not stand.
            (void)galaxy::io::write_text_file_atomically(settings_folder / "mods-report.txt",
                                                         "Nebula mods applied at the last start: none\n");
        }
    }
#endif

    boot_mark("settings and mods");
    static NebulaHostServices services{};
    services.version = NEBULA_GAME_API_VERSION;
    services.size = sizeof(services);
    services.log = svc_log;
    services.fatal = svc_fatal;
    services.scene_marker = svc_scene_marker;
    services.movie_frame = svc_movie_frame;
    services.code_patch = svc_code_patch;
    services.presence = svc_presence;
    services.achievement = svc_achievement;
    services.achievement_date = svc_achievement_date;
    services.stat_add = svc_stat_add;
    services.stat_get = svc_stat_get;
    services.settings_view = svc_settings_view;
    services.settings_input = svc_settings_input;
    services.sharp_2d = svc_sharp_2d;
    services.button_icons = svc_button_icons;
    services.art_export = svc_art_export;
    services.layer_2d_scale = svc_layer_2d_scale;
    services.pointer_wheel = svc_pointer_wheel;
    services.galaxy_select_stage = svc_galaxy_select_stage;
    services.galaxy_select_names_wanted = svc_galaxy_select_names_wanted;
    services.galaxy_select_name = svc_galaxy_select_name;
    services.curved_surface = svc_curved_surface;
    services.shape_array = svc_shape_array;
    services.shape_geometry = svc_shape_geometry;
    services.shape_draw = svc_shape_draw;
    services.material_program = svc_material_program;
    services.material = svc_material;
    services.state = svc_state;
    services.immediate = svc_immediate;
    services.texture_image = svc_texture_image;
    services.mii_record = svc_mii_record;
    services.readback_capture = svc_readback_capture;
    services.readback_pixel = svc_readback_pixel;
    services.frame_end = svc_frame_end;
    services.marker = svc_marker;
    services.readback_serial = svc_readback_serial;
    services.clock_ns = svc_clock_ns;
    services.calendar_ns = svc_calendar_ns;
    services.stripe_particle = svc_stripe_particle;
    services.disc_find = svc_disc_find;
    services.disc_entry = svc_disc_entry;
    services.disc_child = svc_disc_child;
    services.disc_read = svc_disc_read;
    services.disc_dol = svc_disc_dol;
    services.disc_id = svc_disc_id;
    services.vi_frame = svc_vi_frame;
    services.save_request = svc_save_request;
    services.sc_get = svc_sc_get;
    services.input_read = svc_input_read;
    services.input_control = svc_input_control;
    services.speaker_samples = svc_speaker_samples;
    services.jaudio_setup = svc_jaudio_setup;
    services.jaudio_aux = svc_jaudio_aux;
    services.jaudio_frame = svc_jaudio_frame;
    services.jaudio_mix = svc_jaudio_mix;
    services.ai_dma = svc_ai_dma;
    services.ai_control = svc_ai_control;
    services.hbm = svc_hbm;
    services.thp_video_decode = svc_thp_video_decode;
    services.gpu_coherent = svc_gpu_coherent;
    services.game_context = svc_game_context;
    services.power = svc_power;
    services.test_scene_frame = svc_test_scene_frame;
    services.display_extent = svc_display_extent;
    services.scene_surround = svc_scene_surround;
    services.option = svc_option;
    services.game_light = svc_game_light;
    services.game_stage = svc_game_stage;
    services.object_name = svc_object_name;
    services.light_reach = svc_light_reach;
    services.traced_sun = svc_traced_sun;
#if defined(NEBULA_HOST_RENDERER)
    // The display configuration (GALAXY_RENDER_CONFIG_PATH in tests), loaded before the window; the settings overlay
    // reads its frame art from the game files.
    try {
        galaxy::render::load_render_config_from_file();
    } catch (const std::exception& failure) {
        die("the settings did not load: %s", failure.what());
    }
    galaxy::render::set_native_settings_content_root(galaxy::host::locate_game(pak).content_root.string());
    // Discord: never from a hidden test run, which must leave the player's status alone.
    if (const char* hidden = std::getenv("GALAXY_HIDE_WINDOW"); hidden == nullptr || hidden[0] == '\0' || hidden[0] == '0') {
        const char* id = std::getenv("GALAXY_DISCORD_CLIENT_ID");
        galaxy::discord::set_enabled_provider([] { return galaxy::get_runtime_settings().discord_status; });
        galaxy::discord::start(id != nullptr ? id : kDiscordClientId);
    }
    // The environment is final from here: the input report's reads are served from a table (each read otherwise scans
    // the whole environment block, 100 reports a second).
    galaxy::host::set_env_flag_cache_enabled(true);
    const galaxy::render::RenderConfig render_config = galaxy::render::get_render_config();
    gNative.log = native_log;
    if (!galaxy::render::initialize(static_cast<int>(render_config.window_width),
                                static_cast<int>(render_config.window_height))) {
        const std::string reason = galaxy::render::startup_failure();
        die("the renderer did not start%s%s", reason.empty() ? "" : ": ", reason.c_str());
    }
    boot_mark("renderer started");
    // While the start runs ahead, the retraces' presents alone reach the screen (svc_vi_frame).
    galaxy::render::set_frame_end_presents(!gBootFast.load(std::memory_order_relaxed));
    gAudio = new galaxy::host::NativeGameAudio();  // lives as long as the process (the AI thread is detached)
    gAudio->set_channel_owner(gModule.audio_channel_owner);
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
    gDisc.start();
    gFs.start();
    std::thread(ai_thread).detach();
    std::thread(vi_thread).detach();
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
    if (const char* profile = std::getenv("GALAXY_PROFILE_GAME"); profile != nullptr && std::atof(profile) > 0.0) {
        const HANDLE sampled = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE,
                                          GetThreadId(static_cast<HANDLE>(game.native_handle())));
        if (sampled != nullptr) {
            std::thread([sampled, seconds = std::atof(profile)] {
                game_profile::run(sampled, seconds);
                CloseHandle(sampled);
            }).detach();
        }
    }
    // The simulation thread's CPU time, reported each second (the game and everything it calls on that thread).
    HANDLE game_thread = nullptr;  // its own handle: detach() closes the thread object's
    DuplicateHandle(GetCurrentProcess(), static_cast<HANDLE>(game.native_handle()), GetCurrentProcess(), &game_thread,
                    THREAD_QUERY_LIMITED_INFORMATION, FALSE, 0);
    uint64_t game_cycles_last = 0;
    const char* trace_env = std::getenv("GALAXY_TRACE_THREAD_CPU");
    const bool trace_thread_cpu = trace_env != nullptr && trace_env[0] == '1';
    // Thread cycles count at the time-stamp counter's rate, measured here against the performance counter.
    const double tsc_hz = [] {
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
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 4, QS_ALLINPUT);
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
        const uint32_t retraces = gRetraces.load(std::memory_order_relaxed);
        if (retraces != reported) {
            char text[384];
            std::snprintf(text, sizeof(text), "retrace %u, disc reads %u, nand ops %u, ai blocks %u, dsp subframes %u",
                          retraces,
                          gDiscReads.load(std::memory_order_relaxed), gFsOps.load(std::memory_order_relaxed),
                          gAiBlocks.load(std::memory_order_relaxed), gDspFrames.load(std::memory_order_relaxed));
#if defined(NEBULA_HOST_RENDERER)
            if (retraces / 60 % 30 == 0) {
                log_line("host:", ("jaudio " + gAudio->stats_line()).c_str());
            }
            const size_t length = std::strlen(text);
            static uint64_t last_decode_ns = 0;
            const uint64_t decode_ns = gMovieDecodeNs.load(std::memory_order_relaxed);
            std::snprintf(text + length, sizeof(text) - length,
                          ", renderer frames %llu, movie frames %u (decoded in %.1f ms/s, %u replaced)",
                          (unsigned long long)gSubmissions.load(std::memory_order_relaxed),
                          gMovieFrames.load(std::memory_order_relaxed),
                          static_cast<double>(decode_ns - last_decode_ns) / 1.0e6,
                          gMovieSkipped.load(std::memory_order_relaxed));
            last_decode_ns = decode_ns;
#endif
#if defined(_WIN32)
            if (trace_thread_cpu) {
                report_thread_cpu(tsc_hz);
            }
            ULONG64 cycles = 0;
            if (QueryThreadCycleTime(game_thread, &cycles)) {
                const size_t used = std::strlen(text);
                std::snprintf(text + used, sizeof(text) - used, ", game cpu %.1f ms/s",
                              (double)(cycles - game_cycles_last) * 1000.0 / tsc_hz);
                game_cycles_last = cycles;
            }
#endif
            log_line("host:", text);
            reported = retraces;
        }
    }
    log_line("host:", Clock::now() < end ? "the window was closed" : "time limit reached");
    log_line("host:", ("3D scenes " + std::to_string(gSceneBegins.load(std::memory_order_relaxed))).c_str());
    if (gModule.debug_dump != nullptr) {
        gModule.debug_dump();
    }
#if defined(NEBULA_HOST_RENDERER) && defined(_WIN32)
    galaxy::timeline::dump();
#endif
#if defined(NEBULA_HOST_RENDERER)
    galaxy::achievements::flush();
    galaxy::stats::flush();
#endif
    std::fflush(stdout);
    std::_Exit(0);
}
