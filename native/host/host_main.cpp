// The native game's host (docs/NATIVE_PORT.md, "Module layout"): loads the game module (smg_game.dll), gives it
// Nebula's services (nebula_game_api.h) and runs it. Built two ways:
//   - nebula_native_host (native/CMakeLists.txt): headless. The GX stream is released as it arrives and PE events
//     answered at once; audio is paced but not played; no remotes connect.
//   - NebulaNative (the top-level build, NEBULA_HOST_RENDERER): the stream goes to Nebula's D3D12 renderer, which
//     raises the PE events and presents the frame buffer the game displays.
//
//   <host> --pak <game.pak> --nand <dir> [--module <smg_game.dll>] [--seconds N]
#if defined(_WIN32)
#define NOMINMAX
#endif
#include <nebula_game_api.h>

#if defined(NEBULA_HOST_RENDERER)
#include "galaxy/gx_d3d12.h"
#include "galaxy/native_api.h"
#include "galaxy/native_host.h"
#include "galaxy/native_input.h"
#include "galaxy/native_thp_video.h"
#endif

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#include <fcntl.h>
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

[[noreturn]] void die(const char* format, ...) {
    char text[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    log_line("fatal:", text);
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
    decltype(&nebula_game_fs_complete) fs_complete;
    decltype(&nebula_game_ai_interrupt) ai_interrupt;
    decltype(&nebula_game_gx_event) gx_event;
    decltype(&nebula_game_gx_release) gx_release;
    decltype(&nebula_game_memory_regions) memory_regions;
    decltype(&nebula_game_wpad_connect) wpad_connect;
    decltype(&nebula_game_wpad_report) wpad_report;
    void (*debug_dump)();  // optional: the module's thread report
};
Module gModule;

// The GX command ring's size (sdk/gx/gx_fifo.c), mapped twice in a row: a span past the first mapping's end
// continues in the second.
constexpr size_t kGxRingSize = 16u << 20;

// Progress counters for the host's log.
std::atomic<uint64_t> gGxBytes{0};
std::atomic<uint32_t> gDiscReads{0};
std::atomic<uint32_t> gFsOps{0};
std::atomic<uint32_t> gAiBlocks{0};
std::atomic<uint32_t> gDspFrames{0};

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
    gModule.fs_complete = reinterpret_cast<decltype(gModule.fs_complete)>(find_symbol(module, "nebula_game_fs_complete"));
    gModule.ai_interrupt = reinterpret_cast<decltype(gModule.ai_interrupt)>(find_symbol(module, "nebula_game_ai_interrupt"));
    gModule.gx_event = reinterpret_cast<decltype(gModule.gx_event)>(find_symbol(module, "nebula_game_gx_event"));
    gModule.gx_release = reinterpret_cast<decltype(gModule.gx_release)>(find_symbol(module, "nebula_game_gx_release"));
    gModule.memory_regions = reinterpret_cast<decltype(gModule.memory_regions)>(find_symbol(module, "nebula_game_memory_regions"));
    gModule.wpad_connect = reinterpret_cast<decltype(gModule.wpad_connect)>(find_symbol(module, "nebula_game_wpad_connect"));
    gModule.wpad_report = reinterpret_cast<decltype(gModule.wpad_report)>(find_symbol(module, "nebula_game_wpad_report"));
#if defined(_WIN32)
    gModule.debug_dump = reinterpret_cast<void (*)()>(GetProcAddress(static_cast<HMODULE>(module), "nebula_game_debug_dump"));
#else
    gModule.debug_dump = reinterpret_cast<void (*)()>(dlsym(module, "nebula_game_debug_dump"));
#endif
}

#if defined(NEBULA_HOST_RENDERER)
void svc_gpu_coherent(void*, uint32_t physical, uint32_t size);

// Host writes into game memory (disc DMA): the renderer drops what it holds of it. `dst` is in one of the game's
// regions.
void mark_written(const void* dst, uint32_t size) {
    NebulaGameMemoryRegion regions[8];
    const int count = gModule.memory_regions(regions, 8);
    const auto* p = static_cast<const uint8_t*>(dst);
    for (int i = 0; i < count; i++) {
        if (p >= regions[i].host && p < regions[i].host + regions[i].size) {
            galaxy::gx::notify_guest_memory_write(regions[i].physical + (uint32_t)(p - regions[i].host), size);
            return;
        }
    }
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
};
Pak gPak;

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

// A worker thread taking jobs in order.
class Worker {
public:
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
};

Worker gDisc;
Worker gFs;

// NAND: the console's absolute paths under the host's NAND root, in the Wii's layout.
fs::path gNandRoot;

struct OpenFile {
    std::FILE* file = nullptr;
    fs::path path;
    fs::path temp;  // a safe open's copy, moved over `path` at the safe close
};
std::vector<OpenFile> gFiles(16);

constexpr int32_t kNandOk = 0;
constexpr int32_t kNandAccess = -1;
constexpr int32_t kNandExists = -6;
constexpr int32_t kNandInvalid = -8;
constexpr int32_t kNandMaxFd = -10;
constexpr int32_t kNandNoExists = -12;
constexpr int32_t kNandNotEmpty = -13;

bool nand_path(const char* path, fs::path* out) {
    if (path == nullptr || path[0] != '/') {
        return false;
    }
    fs::path result = gNandRoot;
    for (const fs::path& part : fs::path(path + 1)) {
        if (part == ".." || part == ".") {
            return false;
        }
        result /= part;
    }
    *out = result;
    return true;
}

std::FILE* open_file(const fs::path& path, uint32_t mode) {
    // NAND_ACCESS_READ 1, WRITE 2, RW 3: the file must exist.
#if defined(_WIN32)
    return _wfopen(path.wstring().c_str(), mode == 1 ? L"rb" : L"r+b");
#else
    return std::fopen(path.c_str(), mode == 1 ? "rb" : "r+b");
#endif
}

int32_t fs_run(const NebulaFsRequest& r) {
    std::error_code error;
    fs::path path;
    auto file_at = [](int32_t fd) -> OpenFile* {
        return fd >= 0 && fd < (int32_t)gFiles.size() && gFiles[fd].file != nullptr ? &gFiles[fd] : nullptr;
    };
    switch (r.op) {
    case NEBULA_FS_OPEN:
    case NEBULA_FS_SAFE_OPEN: {
        if (!nand_path(r.path, &path)) {
            return kNandInvalid;
        }
        if (!fs::is_regular_file(path, error)) {
            return kNandNoExists;
        }
        int32_t fd = 0;
        while (fd < (int32_t)gFiles.size() && gFiles[fd].file != nullptr) {
            fd++;
        }
        if (fd == (int32_t)gFiles.size()) {
            return kNandMaxFd;
        }
        OpenFile entry;
        entry.path = path;
        if (r.op == NEBULA_FS_SAFE_OPEN && r.mode != 1) {
            entry.temp = path;
            entry.temp += L".nebula-safe";
            fs::copy_file(path, entry.temp, fs::copy_options::overwrite_existing, error);
            if (error) {
                return kNandAccess;
            }
            entry.file = open_file(entry.temp, r.mode);
        } else {
            entry.file = open_file(path, r.mode);
        }
        if (entry.file == nullptr) {
            return kNandAccess;
        }
        gFiles[fd] = std::move(entry);
        return fd;
    }
    case NEBULA_FS_CLOSE:
    case NEBULA_FS_SAFE_CLOSE: {
        OpenFile* f = file_at(r.fd);
        if (f == nullptr) {
            return kNandInvalid;
        }
        std::fflush(f->file);
        std::fclose(f->file);
        f->file = nullptr;
        if (!f->temp.empty()) {
            fs::rename(f->temp, f->path, error);
            f->temp.clear();
            if (error) {
                return kNandAccess;
            }
        }
        return kNandOk;
    }
    case NEBULA_FS_READ: {
        OpenFile* f = file_at(r.fd);
        return f == nullptr ? kNandInvalid : (int32_t)std::fread(r.buffer, 1, r.size, f->file);
    }
    case NEBULA_FS_WRITE: {
        OpenFile* f = file_at(r.fd);
        return f == nullptr ? kNandInvalid : (int32_t)std::fwrite(r.buffer, 1, r.size, f->file);
    }
    case NEBULA_FS_SEEK: {
        OpenFile* f = file_at(r.fd);
        if (f == nullptr || std::fseek(f->file, r.offset, r.whence == 0 ? SEEK_SET : r.whence == 1 ? SEEK_CUR : SEEK_END) != 0) {
            return kNandInvalid;
        }
        return (int32_t)std::ftell(f->file);
    }
    case NEBULA_FS_LENGTH: {
        OpenFile* f = file_at(r.fd);
        if (f == nullptr) {
            return kNandInvalid;
        }
        const long at = std::ftell(f->file);
        std::fseek(f->file, 0, SEEK_END);
        const long length = std::ftell(f->file);
        std::fseek(f->file, at, SEEK_SET);
        return (int32_t)length;
    }
    case NEBULA_FS_CREATE:
    case NEBULA_FS_CREATE_DIR: {
        if (!nand_path(r.path, &path)) {
            return kNandInvalid;
        }
        if (fs::exists(path, error)) {
            return kNandExists;
        }
        if (!fs::is_directory(path.parent_path(), error)) {
            return kNandNoExists;
        }
        if (r.op == NEBULA_FS_CREATE_DIR) {
            return fs::create_directory(path, error) ? kNandOk : kNandAccess;
        }
#if defined(_WIN32)
        std::FILE* f = _wfopen(path.wstring().c_str(), L"wb");
#else
        std::FILE* f = std::fopen(path.c_str(), "wb");
#endif
        if (f == nullptr) {
            return kNandAccess;
        }
        std::fclose(f);
        return kNandOk;
    }
    case NEBULA_FS_DELETE: {
        if (!nand_path(r.path, &path)) {
            return kNandInvalid;
        }
        if (!fs::exists(path, error)) {
            return kNandNoExists;
        }
        fs::remove_all(path, error);
        return error ? kNandAccess : kNandOk;
    }
    case NEBULA_FS_RENAME: {
        fs::path to;
        if (!nand_path(r.path, &path) || !nand_path(r.path2, &to)) {
            return kNandInvalid;
        }
        if (!fs::exists(path, error)) {
            return kNandNoExists;
        }
        if (fs::is_directory(to, error) && !fs::is_empty(to, error)) {
            return kNandNotEmpty;
        }
        fs::rename(path, to, error);
        return error ? kNandAccess : kNandOk;
    }
    }
    return kNandInvalid;
}

// Audio interface pacing: the AI takes a latched block at each block's end, as the console's DMA engine does when
// its count runs out, and raises the AI interrupt for the game to latch the next.
std::mutex gAiLock;
std::condition_variable gAiWake;
uint32_t gAiLatchedSize = 0;
uint32_t gAiLatchedStart = 0;
uint32_t gAiRate = 32000;
bool gAiRunning = false;

#if defined(NEBULA_HOST_RENDERER)
const std::byte* guest_bytes(uint32_t address, uint32_t size);
extern galaxy::host::NativeGameAudio* gAudio;
#endif

void ai_thread() {
    for (;;) {
        uint32_t size = 0;
        uint32_t start = 0;
        uint32_t rate = 32000;
        {
            std::unique_lock<std::mutex> lock(gAiLock);
            gAiWake.wait(lock, [] { return gAiRunning && gAiLatchedSize != 0; });
            size = gAiLatchedSize;
            start = gAiLatchedStart;
            rate = gAiRate;
        }
        const auto begin = Clock::now();
#if defined(NEBULA_HOST_RENDERER)
        // The block the DMA engine takes now goes to the output device, whose clock then sets the pace: the next
        // block is taken once fewer than two wait there (the engine latches one block ahead of the one playing).
        bool played = false;
        if (const std::byte* samples = guest_bytes(start, size)) {
            played = gAudio->play(std::span<const std::byte>(samples, size), rate);
        }
#else
        (void)start;
#endif
        gAiBlocks.fetch_add(1, std::memory_order_relaxed);
        gModule.ai_interrupt();
#if defined(NEBULA_HOST_RENDERER)
        if (played && gAudio->output_available()) {
            const auto block = std::chrono::nanoseconds((uint64_t)size / 4 * 1000000000ull / rate);
            while (gAudio->queued_blocks() >= 2) {
                sleep_until_precise(Clock::now() + block / 8);
            }
            continue;
        }
#endif
        // No device: the block's length on the host clock (4 bytes per stereo sample).
        sleep_until_precise(begin + std::chrono::nanoseconds((uint64_t)size / 4 * 1000000000ull / rate));
    }
}

// The display's retrace: NTSC fields at 60000/1001 Hz. A late wake-up skips the fields it missed, as a display does,
// rather than delivering them back to back.
void vi_thread() {
    const auto period = std::chrono::nanoseconds(1001000000000ull / 60000);
    auto next = Clock::now() + period;
    for (;;) {
        sleep_until_precise(next);
        gModule.vi_retrace();
        next += period;
        const auto now = Clock::now();
        if (now >= next) {
            next = now + period;
        }
    }
}

// Services

void svc_log(void*, const char* text) {
    log_line("game:", text);
}

void svc_fatal(void*, const char* text) {
    log_line("fatal:", text);
    std::fflush(stdout);
    std::_Exit(3);
}

#if defined(NEBULA_HOST_RENDERER)
// The renderer. The game's memory is its guest memory: the module's physical regions and the cached and uncached
// mirrors of MEM1 and MEM2 (virtual addresses in display lists). The command stream reaches it in frames: the spans
// since the last submission, contiguous in the ring's double mapping, go to render_frame at a PE marker (draw sync,
// draw done) or at the retrace, which also presents; render_frame copies them, so they are released at once. The
// renderer raises PE events through gNative's callbacks when its parse and the GPU reach them. All of this runs on
// the simulation thread, where the game makes these calls.
using galaxy::GuestMemoryRegionV1;
using galaxy::GuestMemoryV1;
using galaxy::LogLevelV1;
using galaxy::NativeServicesV1;

GuestMemoryV1 gMemory{};
std::vector<GuestMemoryRegionV1> gRegions;
NativeServicesV1 gNative{};
const uint8_t* gPending = nullptr;
size_t gPendingSize = 0;
std::deque<galaxy::gx::FramePeCompletionToken> gTokens;
std::atomic<uint64_t> gSubmissions{0};  // render_frame calls with commands: the renderer's frame index

void native_log(void*, LogLevelV1, const char* text) {
    log_line("renderer:", text);
}

void native_fatal(void*, uint32_t, const char* text) {
    die("renderer: %s", text);
}

void native_pe_finish(void*) {
    gModule.gx_event(1, 0);
}

void native_pe_token(void*, uint16_t token, bool) {
    gModule.gx_event(0, token);
}

galaxy::host::NativeGameAudio* gAudio = nullptr;

void setup_renderer_memory() {
    NebulaGameMemoryRegion regions[8];
    const int count = gModule.memory_regions(regions, 8);
    for (int i = 0; i < count; i++) {
        const NebulaGameMemoryRegion& r = regions[i];
        std::byte* const host = reinterpret_cast<std::byte*>(r.host);
        for (const uint32_t mirror : {0x00000000u, 0x80000000u, 0xC0000000u}) {
            gRegions.push_back({r.physical | mirror, r.size, host});
            if ((r.physical & 0x0FFFFFFFu) == 0) {
                gMemory.fast_regions[(r.physical | mirror) >> 28] = {r.size, host};
            }
        }
    }
    gMemory.regions = gRegions.data();
    gMemory.region_count = static_cast<uint32_t>(gRegions.size());
    gNative.log = native_log;
    gNative.fatal = native_fatal;
    gNative.gx_pe_finish = native_pe_finish;
    gNative.gx_pe_token = native_pe_token;
}

std::once_flag gMemoryOnce;

void ensure_guest_memory() {
    std::call_once(gMemoryOnce, setup_renderer_memory);
}

// `size` bytes at a guest address, in the module's arena or image.
const std::byte* guest_bytes(uint32_t address, uint32_t size) {
    ensure_guest_memory();
    for (const galaxy::GuestMemoryRegionV1& region : gRegions) {
        if (address - region.guest_base < region.size && size <= region.size - (address - region.guest_base)) {
            return region.host_base + (address - region.guest_base);
        }
    }
    return nullptr;
}

// Takes the receipts of finished submissions, in order (each is consumed once).
void take_finished_tokens() {
    while (!gTokens.empty() && galaxy::gx::wait_for_frame_pe_completion_for(gTokens.front(), 0)) {
        gTokens.pop_front();
    }
}

void submit_frame(bool present, uint32_t xfb) {
    const size_t size = gPendingSize;
    const auto token = galaxy::gx::render_frame(reinterpret_cast<const std::byte*>(gPending), size, &gMemory, &gNative,
                                                present, xfb);
    if (token) {
        gTokens.push_back(token);
        gSubmissions.fetch_add(1, std::memory_order_relaxed);
    }
    if (size != 0) {
        gPendingSize = 0;
        gModule.gx_release(size);
    }
    take_finished_tokens();
}
#endif

void svc_gx_submit(void*, const uint8_t* bytes, size_t size) {
    gGxBytes.fetch_add(size, std::memory_order_relaxed);
#if defined(NEBULA_HOST_RENDERER)
    ensure_guest_memory();
    if (gPendingSize == 0) {
        gPending = bytes;
    } else if (bytes != gPending + gPendingSize && bytes + kGxRingSize != gPending + gPendingSize) {
        die("a GX span does not continue the last (%p after %p + %zu)", (const void*)bytes, (const void*)gPending, gPendingSize);
    }
    gPendingSize += size;
#else
    (void)bytes;
    gModule.gx_release(size);
#endif
}

void svc_gx_pe_marker(void*, int finish, uint16_t token) {
#if defined(NEBULA_HOST_RENDERER)
    (void)finish;
    (void)token;
    submit_frame(false, 0);
#else
    gModule.gx_event(finish, token);
#endif
}

uint32_t svc_gx_peek_efb(void*, uint16_t x, uint16_t y, uint32_t type) {
#if defined(NEBULA_HOST_RENDERER)
    const auto kind = type == 0 ? galaxy::gx::EfbPeekKind::Color : galaxy::gx::EfbPeekKind::Depth;
    uint32_t value = 0;
    if (!galaxy::gx::peek_efb_published(x, y, kind, value)) {
        submit_frame(false, 0);
        (void)galaxy::gx::peek_efb(x, y, kind, value);
    }
    return value;
#else
    (void)x;
    (void)y;
    (void)type;
    return 0;
#endif
}

std::atomic<int32_t> gStarPointerMode{-1};
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
    }
}

void svc_gpu_coherent(void*, uint32_t physical, uint32_t size) {
#if defined(NEBULA_HOST_RENDERER)
    galaxy::gx::notify_guest_memory_write(physical, size);
#else
    (void)physical;
    (void)size;
#endif
}

const uint8_t* svc_disc_fst(void*, size_t* size) {
    *size = gPak.fst_size;
    return gPak.fst;
}

void svc_disc_read(void*, uint64_t offset, uint32_t size, void* dst, uint32_t slot) {
    gDiscReads.fetch_add(1, std::memory_order_relaxed);
    gDisc.post([=] {
        if (offset > gPak.size || gPak.size - offset < size) {
            gModule.disc_complete(slot, -1);
            return;
        }
        std::memcpy(dst, gPak.data + offset, size);
#if defined(NEBULA_HOST_RENDERER)
        // The console's disc DMA writes memory the GPU sees.
        mark_written(dst, size);
#endif
        gModule.disc_complete(slot, (int32_t)size);
    });
}

const uint8_t* svc_disc_dol(void*, size_t* size) {
    *size = gPak.dol_size;
    return gPak.dol;
}

std::atomic<uint32_t> gRetraces{0};
std::atomic<uint32_t> gXfb{0};

void svc_vi_frame(void*, uint32_t xfb, int black, uint32_t retrace) {
    gXfb.store(xfb, std::memory_order_relaxed);
    gRetraces.store(retrace, std::memory_order_relaxed);
#if defined(NEBULA_HOST_RENDERER)
    if (!gRegions.empty()) {
        submit_frame(true, xfb == 0 || black ? galaxy::gx::kNoDisplayedXfbAddr : xfb);
    }
#else
    (void)black;
#endif
}

void svc_fs_request(void*, const NebulaFsRequest* request, uint32_t slot) {
    gFsOps.fetch_add(1, std::memory_order_relaxed);
    const NebulaFsRequest r = *request;
    gFs.post([r, slot] {
        const int32_t result = fs_run(r);
        static const bool trace = std::getenv("NEBULA_TRACE_NAND") != nullptr;
        if (trace) {
            char text[400];
            std::snprintf(text, sizeof(text), "nand op %u path %s path2 %s fd %d size %u mode %u -> %d", r.op,
                          r.path != nullptr ? r.path : "-", r.path2 != nullptr ? r.path2 : "-", r.fd, r.size, r.mode, result);
            log_line("host:", text);
        }
        gModule.fs_complete(slot, result);
    });
}

// The system settings v1.0.1's SYSCONF gives the game (native_host.cpp, create_default_sysconf_file): its pointer
// mapping (native_input's virtual sensor bar) assumes the bar above the screen.
uint32_t svc_sc_get(void*, uint32_t item) {
    switch (item) {
    case NEBULA_SC_LANGUAGE:
        return 1;  // English
    case NEBULA_SC_SOUND_MODE:
        return 1;  // stereo
    case NEBULA_SC_ASPECT_RATIO:
        return 1;  // 16:9
    case NEBULA_SC_PROGRESSIVE_MODE:
        return 0;
    case NEBULA_SC_EURGB60_MODE:
        return 1;
    case NEBULA_SC_SENSOR_BAR_POSITION:
        return 1;  // above the screen
    case NEBULA_SC_DPD_SENSITIVITY:
        return 3;
    case NEBULA_SC_SPEAKER_VOLUME:
        return 0x58;
    case NEBULA_SC_SCREEN_SAVER_MODE:
        return 1;  // IPL.SSV
    case NEBULA_SC_MOTOR_MODE:
        return 1;
    default:
        return 0;
    }
}

#if defined(NEBULA_HOST_RENDERER)
// Remote 0: Nebula's controls (v1.0.1's bindings, keyboard and mouse, controllers) as a Wii Remote with a Nunchuk,
// connected when the game starts WPAD and reporting 100 times a second in the format the game asks for.
std::atomic<int> gWpadStarted{0};
std::atomic<uint32_t> gWpadReport{0x30};
std::atomic<int> gWpadDpd{0};

uint8_t report_id_for_format(uint32_t format) {
    switch (format) {
    case 0:
        return 0x30;  // WPAD_FMT_CORE
    case 1:
        return 0x31;  // WPAD_FMT_CORE_ACC
    case 2:
        return 0x33;  // WPAD_FMT_CORE_ACC_DPD
    default:
        return 0x37;  // the Nunchuk formats: core, accelerometer, IR and extension
    }
}

void wpad_thread() {
    while (!gWpadStarted.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    NebulaWiimoteConnection connection{};
    galaxy::host::native_game_wiimote_calibration(connection.eeprom, connection.extension_calibration);
    connection.extension = 1;  // WPAD_DEV_FREESTYLE: the Nunchuk
    connection.battery = 0xC0;
    gModule.wpad_connect(0, 1, &connection);
    const auto period = std::chrono::milliseconds(10);
    auto next = Clock::now() + period;
    for (;;) {
        sleep_until_precise(next);
        next += period;
        const uint8_t id = static_cast<uint8_t>(gWpadReport.load(std::memory_order_relaxed));
        const int32_t mode = gStarPointerMode.load(std::memory_order_relaxed);
        const auto snapshot = galaxy::host::native_game_wiimote_snapshot(
            id, gRetraces.load(std::memory_order_relaxed), gWpadDpd.load(std::memory_order_relaxed) != 0, mode,
            static_cast<uint8_t>(gTiltSection.load(std::memory_order_relaxed)));
        // Every report renews the motor's state (a stale one stops), as the console's remote keeps its motor bit.
        galaxy::host::native_game_rumble(gMotor.load(std::memory_order_relaxed), mode);
        uint8_t report[galaxy::input::kMaxWiimoteInputReportBytes];
        const auto built = galaxy::input::build_wiimote_input_report(id, snapshot, report);
        if (built.status == galaxy::input::WiimoteReportBuildStatus::Ok && built.size > 1) {
            gModule.wpad_report(0, report + 1, built.size - 1);  // from the report id, past the HID header
        }
    }
}
#endif

void svc_wpad_control(void*, uint32_t chan, uint32_t what, uint32_t value) {
#if defined(NEBULA_HOST_RENDERER)
    if (chan != 0) {
        return;
    }
    switch (what) {
    case NEBULA_WPAD_INIT:
        gWpadStarted.store(1, std::memory_order_release);
        break;
    case NEBULA_WPAD_FORMAT:
        gWpadReport.store(report_id_for_format(value), std::memory_order_relaxed);
        break;
    case NEBULA_WPAD_DPD:
        gWpadDpd.store(value != 0, std::memory_order_relaxed);
        break;
    case NEBULA_WPAD_MOTOR:
        gMotor.store(value != 0, std::memory_order_relaxed);
        galaxy::host::native_game_rumble(value != 0, gStarPointerMode.load(std::memory_order_relaxed));
        break;
    default:
        break;
    }
#else
    (void)chan;
    (void)what;
    (void)value;
#endif
}
void svc_wpad_speaker_data(void*, uint32_t, const uint8_t*, uint32_t) {}

// The JAudio DSP program: v1.0.1's host version mixes each subframe when the CPU releases its last channel group
// (JASDSPChannel::updateAll's releaseHalt(3), a one-word command). Headless, the subframe completes unmixed.
int svc_audio_dsp(void*, const uint32_t* words, uint32_t count) {
#if defined(NEBULA_HOST_RENDERER)
    ensure_guest_memory();
    const int subframe = gAudio->dsp_command(words, count, gMemory) ? 1 : 0;
#else
    const int subframe = count == 0 && (words[0] >> 16) == 3;
#endif
    gDspFrames.fetch_add(subframe, std::memory_order_relaxed);
    return subframe;
}

void svc_ai_dma(void*, uint32_t start, uint32_t size) {
    std::lock_guard<std::mutex> lock(gAiLock);
    gAiLatchedStart = start;
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

int svc_hbm(void*, uint32_t what, uint32_t) {
    return what == NEBULA_HBM_POLL ? -1 : 0;
}

#if defined(NEBULA_HOST_RENDERER)
// The physical address of a host pointer into the game's memory (0 outside it).
uint32_t physical_of(const void* pointer) {
    const auto* p = static_cast<const std::byte*>(pointer);
    for (const galaxy::GuestMemoryRegionV1& region : gRegions) {
        if (region.guest_base < 0x80000000u && p >= region.host_base && p < region.host_base + region.size) {
            return region.guest_base + static_cast<uint32_t>(p - region.host_base);
        }
    }
    return 0;
}

// A THP frame's video component is one baseline JPEG whose SOF0 gives the movie's size; v1.0.1's decoder writes its
// planes in GX's I8 tiling. The planes are then the renderer's to re-read, as after the console's decode.
int32_t svc_thp_video_decode(void*, const void* frame, uint32_t size, void* y, void* u, void* v) {
    ensure_guest_memory();
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
    for (const auto& [plane_pointer, size] : {std::pair{y, luma}, std::pair{u, chroma}, std::pair{v, chroma}}) {
        if (const uint32_t physical = physical_of(plane_pointer)) {
            galaxy::gx::notify_guest_memory_write(physical, static_cast<uint32_t>(size));
        }
    }
    return result;
}
#else
int32_t svc_thp_video_decode(void*, const void*, uint32_t, void*, void*, void*) {
    return 0;
}
#endif

}  // namespace

int main(int argc, char** argv) {
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
        module = fs::path(argv[0]).parent_path() / "smg_game.dll";
    }
    gNandRoot = nand;
    std::error_code error;
    fs::create_directories(gNandRoot / "title/00010000/524d4745/data", error);
    fs::create_directories(gNandRoot / "tmp", error);  // where saves stage their banner before moving it
#if defined(NEBULA_HOST_RENDERER)
    // The system's Mii database, which every console has: an empty one when this NAND has none (the game treats a
    // missing database as a NAND error when it saves).
    const fs::path mii_database = gNandRoot / "shared2/menu/FaceLib/RFL_DB.dat";
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
    load_module(module);

    static NebulaHostServices services{};
    services.version = NEBULA_GAME_API_VERSION;
    services.size = sizeof(services);
    services.log = svc_log;
    services.fatal = svc_fatal;
    services.gx_submit = svc_gx_submit;
    services.gx_pe_marker = svc_gx_pe_marker;
    services.gx_peek_efb = svc_gx_peek_efb;
    services.disc_fst = svc_disc_fst;
    services.disc_read = svc_disc_read;
    services.disc_dol = svc_disc_dol;
    services.vi_frame = svc_vi_frame;
    services.fs_request = svc_fs_request;
    services.sc_get = svc_sc_get;
    services.wpad_control = svc_wpad_control;
    services.wpad_speaker_data = svc_wpad_speaker_data;
    services.audio_dsp = svc_audio_dsp;
    services.ai_dma = svc_ai_dma;
    services.ai_control = svc_ai_control;
    services.hbm = svc_hbm;
    services.thp_video_decode = svc_thp_video_decode;
    services.gpu_coherent = svc_gpu_coherent;
    services.game_context = svc_game_context;
#if defined(NEBULA_HOST_RENDERER)
    if (!galaxy::gx::initialize(1280, 720)) {
        die("the renderer did not start");
    }
    galaxy::gx::set_indexed_load_host_order(true);
    gAudio = new galaxy::host::NativeGameAudio();  // lives as long as the process (the AI thread is detached)
#endif
    if (gModule.attach(&services) != NEBULA_GAME_API_VERSION) {
        die("the game module's API version differs from the host's (%u)", NEBULA_GAME_API_VERSION);
    }

    gDisc.start();
    gFs.start();
    std::thread(ai_thread).detach();
    std::thread(vi_thread).detach();
#if defined(NEBULA_HOST_RENDERER)
    std::thread(wpad_thread).detach();
#endif
    std::thread([] { gModule.run(); }).detach();

    const auto end = Clock::now() + std::chrono::duration<double>(seconds > 0 ? seconds : 1e9);
    uint32_t reported = 0;
    auto next_report = Clock::now() + std::chrono::seconds(1);
    while (Clock::now() < end) {
#if defined(NEBULA_HOST_RENDERER)
        // The window's messages, on the thread that created it.
        MsgWaitForMultipleObjects(0, nullptr, FALSE, 4, QS_ALLINPUT);
        galaxy::gx::pump_messages();
        if (galaxy::gx::quit_requested()) {
            break;
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
            char text[256];
            std::snprintf(text, sizeof(text), "retrace %u, xfb 0x%08X, gx %.2f MB, disc reads %u, nand ops %u, ai blocks %u, dsp subframes %u",
                          retraces, gXfb.load(std::memory_order_relaxed), gGxBytes.load(std::memory_order_relaxed) / 1048576.0,
                          gDiscReads.load(std::memory_order_relaxed), gFsOps.load(std::memory_order_relaxed),
                          gAiBlocks.load(std::memory_order_relaxed), gDspFrames.load(std::memory_order_relaxed));
#if defined(NEBULA_HOST_RENDERER)
            if (retraces / 60 % 30 == 0) {
                log_line("host:", ("jaudio " + gAudio->stats_line()).c_str());
            }
            const size_t length = std::strlen(text);
            std::snprintf(text + length, sizeof(text) - length, ", renderer frames %llu",
                          (unsigned long long)gSubmissions.load(std::memory_order_relaxed));
#endif
            log_line("host:", text);
            reported = retraces;
        }
    }
    log_line("host:", Clock::now() < end ? "the window was closed" : "time limit reached");
    if (gModule.debug_dump != nullptr) {
        gModule.debug_dump();
    }
    std::fflush(stdout);
    std::_Exit(0);
}
