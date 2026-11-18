// SPDX-License-Identifier: GPL-3.0-only
// The test runs' host extension (nebula_host_extension.h), loaded with --extension: it runs the harness's plan
// (LUMA_HARNESS_PLAN, harness.cpp), which drives the controls, menu keys, window sizes, device losses, pictures and the
// fixed-step clock, and keeps the run's sound (GALAXY_AUDIO_WAV_DUMP). Without a plan it declines, so a run never
// goes on without the checks it was started for. A few test hooks stay in the environment because the host reads
// them when it starts (GALAXY_TEST_VRAM_BUDGET_MB, GALAXY_TEST_DISC_DELAY_US, GALAXY_TEST_SAVE_DELAY_US,
// GALAXY_TEST_MODS_SCREEN, GALAXY_TEST_MODS_CHANGED, GALAXY_TEST_REMAKE_TARGETS, GALAXY_TEST_REBUILD_DELAY_MS), and
// GALAXY_TEST_CPU_LOAD is the extension's own; luma_test.py's TEST_HOOKS lists them.
#include <nebula_host_extension.h>

#include "harness.h"
#include "test_services.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

#define NEBULA_EXTENSION_EXPORT extern "C" __declspec(dllexport)
#else
#include <time.h>
#if defined(__APPLE__)
#include <crt_externs.h>
#endif
#define NEBULA_EXTENSION_EXPORT extern "C" __attribute__((visibility("default")))
#endif
#include "data_guard.h"
#include "galaxy/hidden_start.h"

namespace {

const NebulaHostForExtension* g_host = nullptr;

// What the host offers, when it has it.
#define HOST_HAS(name) \
    (g_host != nullptr && g_host->size >= offsetof(NebulaHostForExtension, name) + sizeof(g_host->name) && g_host->name != nullptr)

void log(const std::string& line) {
    if (HOST_HAS(log)) {
        g_host->log(g_host->host, line.c_str());
    } else {
        std::fprintf(stderr, "%s\n", line.c_str());
    }
}

// The clock the latency lines are on (the host's: QueryPerformanceCounter, CLOCK_MONOTONIC in nanoseconds).
long long clock_ticks() {
#if defined(_WIN32)
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return now.QuadPart;
#else
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<long long>(now.tv_sec) * 1000000000ll + now.tv_nsec;
#endif
}

std::optional<std::string> env(const char* name) {
#if defined(_WIN32)
    std::string value(256u, '\0');
    DWORD length = GetEnvironmentVariableA(name, value.data(), static_cast<DWORD>(value.size()));
    if (length >= value.size()) {
        value.resize(length);  // the size it asks for, its terminator included
        length = GetEnvironmentVariableA(name, value.data(), static_cast<DWORD>(value.size()));
    }
    if (length == 0u || length >= value.size()) {
        return std::nullopt;
    }
    value.resize(length);
    return value;
#else
    const char* const value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return std::nullopt;
    }
    return std::string(value);
#endif
}

#if defined(_WIN32)
std::wstring wide_env(const wchar_t* name) {
    std::wstring value(256u, L'\0');
    DWORD length = GetEnvironmentVariableW(name, value.data(), static_cast<DWORD>(value.size()));
    if (length >= value.size()) {
        value.resize(length);
        length = GetEnvironmentVariableW(name, value.data(), static_cast<DWORD>(value.size()));
    }
    if (length == 0u || length >= value.size()) {
        return {};
    }
    value.resize(length);
    return value;
}

// The work-folder policy (data_guard.h) over this process's arguments; null when the run may go on.
const char* guard() {
    int count = 0;
    LPWSTR* const args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (args == nullptr) {
        return "cannot read the command line";
    }
    const char* const refusal = test_guard::error(count, args, wide_env, galaxy::host::player_data_folder().wstring());
    LocalFree(args);
    return refusal;
}
#elif !defined(__ANDROID__)
std::string text_env(const char* name) {
    const char* const value = std::getenv(name);
    return value != nullptr ? std::string(value) : std::string();
}

// The work-folder policy (data_guard.h) over this process's arguments; null when the run may go on.
const char* guard() {
    std::vector<std::string> words;
#if defined(__APPLE__)
    for (int i = 0; i < *_NSGetArgc(); ++i) {
        words.emplace_back((*_NSGetArgv())[i]);
    }
#else
    std::ifstream in("/proc/self/cmdline", std::ios::binary);
    for (std::string word; std::getline(in, word, '\0');) {
        words.push_back(word);
    }
#endif
    if (words.empty()) {
        return "cannot read the command line";
    }
    std::vector<const char*> args;
    for (const std::string& word : words) {
        args.push_back(word.c_str());
    }
    return test_guard::error(static_cast<int>(args.size()), args.data(), text_env, galaxy::host::player_data_folder().string());
}
#endif

// Set and not starting with 0, f or n.
bool env_flag(const char* name) {
    const std::optional<std::string> value = env(name);
    return value.has_value() && (*value)[0] != '0' && (*value)[0] != 'f' && (*value)[0] != 'F' && (*value)[0] != 'n' &&
           (*value)[0] != 'N';
}

std::uint64_t env_u64(const char* name, std::uint64_t fallback) {
    const std::optional<std::string> value = env(name);
    return value.has_value() ? std::strtoull(value->c_str(), nullptr, 10) : fallback;
}

// --- Sound ---------------------------------------------------------------------------------------------------------

// GALAXY_AUDIO_WAV_DUMP=<file>: the mix as a 16-bit stereo WAV file, 15 s or GALAXY_AUDIO_WAV_DUMP_SECONDS (up to an
// hour). A rate change starts a new file beside it (<name>.rate-<hz>.part-<n>.wav). The run's end ends the dump for
// good: the mixes of the shutdown after it are not written (opening the file again would empty it). The mix calls
// add on the audio thread and the end comes on another, so both take the dump's lock. A start-again ends it the same
// way (the host's `ending`), and the start that takes over (the host's started_again) goes on in the same file when its
// first mix has the file's rate, so a run keeps one dump; the seconds count over every start.
class WavDump {
public:
    void start(std::filesystem::path base, std::uint32_t seconds, bool again) {
        base_ = std::move(base);
        seconds_ = seconds;
        again_ = again;
    }
    [[nodiscard]] bool on() const { return !base_.empty() && !failed_; }

    void add(const float* samples, std::uint32_t frames, std::uint32_t channels, std::uint32_t rate) {
        const std::lock_guard<std::mutex> hold(lock_);
        if (!on() || ended_ || channels != 2u) {
            return;
        }
        if (out_.is_open() && rate != rate_) {
            close();
            ++part_;
        }
        if (!out_.is_open() && !open(rate)) {
            return;
        }
        const std::uint64_t room = std::uint64_t{rate_} * 4u * seconds_ - bytes_;
        const std::uint64_t take = std::min<std::uint64_t>(std::uint64_t{frames} * 4u, room) / 4u;
        block_.resize(take * 4u);
        for (std::uint64_t i = 0; i < take * 2u; ++i) {
            const float value = std::clamp(samples[i] * 32768.0f, -32768.0f, 32767.0f);
            const auto bits = static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lrint(value)));
            block_[i * 2u] = static_cast<char>(bits & 0xFFu);
            block_[i * 2u + 1u] = static_cast<char>(bits >> 8u);
        }
        out_.write(block_.data(), static_cast<std::streamsize>(block_.size()));
        bytes_ += static_cast<std::uint32_t>(block_.size());
        if (!out_) {
            log("[audio] WAV dump write failed: " + path_.string());
            failed_ = true;
        }
    }

    // The run's end: the file is complete and nothing opens it again.
    void end() {
        const std::lock_guard<std::mutex> hold(lock_);
        close();
        ended_ = true;
    }

private:
    void close() {
        if (!out_.is_open()) {
            return;
        }
        out_.seekp(4, std::ios::beg);
        u32(36u + bytes_);
        out_.seekp(40, std::ios::beg);
        u32(bytes_);
        out_.close();
        log("[audio] WAV dump wrote " + std::to_string(bytes_) + " bytes at " + std::to_string(rate_) + " Hz: " + path_.string());
        bytes_ = 0u;
    }

    bool open(std::uint32_t rate) {
        path_ = base_;
        if (part_ != 0u) {
            path_ = base_.parent_path() / (base_.stem().string() + ".rate-" + std::to_string(rate) + ".part-" +
                                           std::to_string(part_) + base_.extension().string());
        }
        if (again_ && part_ == 0u) {
            again_ = false;
            if (resume(rate)) {
                return true;
            }
        }
        std::error_code error;
        if (!path_.parent_path().empty()) {
            std::filesystem::create_directories(path_.parent_path(), error);
        }
        out_.open(path_, std::ios::binary | std::ios::trunc);
        if (!out_) {
            log("[audio] WAV dump open failed: " + path_.string());
            failed_ = true;
            return false;
        }
        rate_ = rate;
        out_.write("RIFF", 4);
        u32(36u);
        out_.write("WAVEfmt ", 8);
        u32(16u);
        u16(1u);
        u16(2u);
        u32(rate);
        u32(rate * 4u);
        u16(4u);
        u16(16u);
        out_.write("data", 4);
        u32(0u);
        // The first sample's time, to line a recording's sound up with its pictures ("[snapshot] ... qpc=").
        log("[audio] WAV dump enabled: " + path_.string() + " qpc=" + std::to_string(clock_ticks()));
        return true;
    }
    // The dump the start before this one ended (its header complete), opened to go on after its last sample.
    bool resume(std::uint32_t rate) {
        unsigned char head[44]{};
        {
            std::ifstream in(path_, std::ios::binary);
            if (!in.read(reinterpret_cast<char*>(head), sizeof(head))) {
                return false;
            }
        }
        const auto at = [&head](std::size_t i) {
            return std::uint32_t{head[i]} | (std::uint32_t{head[i + 1u]} << 8u) | (std::uint32_t{head[i + 2u]} << 16u) |
                   (std::uint32_t{head[i + 3u]} << 24u);
        };
        if (std::memcmp(head, "RIFF", 4) != 0 || std::memcmp(head + 8, "WAVEfmt ", 8) != 0 ||
            std::memcmp(head + 36, "data", 4) != 0 || at(24u) != rate) {
            return false;
        }
        const std::uint32_t had = at(40u);
        if (std::uint64_t{had} > std::uint64_t{rate} * 4u * seconds_) {
            return false;
        }
        out_.open(path_, std::ios::binary | std::ios::in | std::ios::out);
        if (!out_) {
            out_.clear();
            return false;
        }
        out_.seekp(static_cast<std::streamoff>(44u + had), std::ios::beg);
        rate_ = rate;
        bytes_ = had;
        log("[audio] WAV dump goes on after a start-again: " + path_.string() + " at " + std::to_string(had) + " bytes qpc=" +
            std::to_string(clock_ticks()));
        return true;
    }
    void u16(std::uint16_t value) {
        const char bytes[2] = {static_cast<char>(value & 0xFFu), static_cast<char>(value >> 8u)};
        out_.write(bytes, 2);
    }
    void u32(std::uint32_t value) {
        const char bytes[4] = {static_cast<char>(value & 0xFFu), static_cast<char>((value >> 8u) & 0xFFu),
                               static_cast<char>((value >> 16u) & 0xFFu), static_cast<char>(value >> 24u)};
        out_.write(bytes, 4);
    }

    std::filesystem::path base_, path_;
    std::ofstream out_;
    std::vector<char> block_;
    std::uint32_t seconds_ = 15u, rate_ = 0u, bytes_ = 0u, part_ = 0u;
    bool failed_ = false, ended_ = false, again_ = false;
    std::mutex lock_;
} g_wav;

void sound(void*, const float* samples, std::uint32_t frames, std::uint32_t channels, std::uint32_t rate) {
    g_wav.add(samples, frames, channels, rate);
}

// GALAXY_TEST_CPU_LOAD=<n>: n threads a logical processor busy at the normal priority from the start to the end, a machine loaded by other
// programs (N-1072: a device restore that waited for lowered threads took 16 s on one).
struct CpuLoad {
    std::atomic<bool> stop{false};
    std::vector<std::thread> threads;
    void start(std::uint64_t per_processor) {
        const std::uint64_t count = std::min<std::uint64_t>(per_processor * std::max(1u, std::thread::hardware_concurrency()), 1024u);
        for (std::uint64_t i = 0; i < count; ++i) {
            threads.emplace_back([this] {
                volatile std::uint64_t spin = 0u;
                while (!stop.load(std::memory_order_relaxed)) {
                    spin = spin + 1u;
                }
            });
        }
    }
    void end() {
        stop.store(true, std::memory_order_relaxed);
        for (std::thread& thread : threads) {
            thread.join();
        }
        threads.clear();
    }
};
CpuLoad g_cpu_load;

// The host's process end, a start-again's too: the dump complete, the load stopped.
void process_ending(void*, std::uint32_t) {
    g_wav.end();
    g_cpu_load.end();
}

bool g_mods_changed = false;
std::uint32_t mods_changed(void*) {
    return g_mods_changed ? 1u : 0u;
}

void frame(void*, const NebulaExtensionFrame* frame) {
    harness::frame(frame->game_frames);
}

void controls(void*, const NebulaExtensionFrame* frame, NebulaExtensionControls* out) {
    harness::controls(frame->game_frames, out);
}

const char* picture_path(void*, std::uint64_t game_frame, std::uint32_t kind) {
    return harness::picture_path(game_frame, kind);
}

NebulaHostExtension g_extension{};

// The harness's end: the sound file closed, then the run ends as closing the window ends it.
void harness_quit() {
    g_wav.end();
    if (HOST_HAS(quit)) {
        g_host->quit(g_host->host);
    }
}

}  // namespace

NEBULA_EXTENSION_EXPORT const NebulaHostExtension* nebula_extension_attach(const NebulaHostForExtension* host) {
    if (host == nullptr || host->version != NEBULA_HOST_EXTENSION_VERSION) {
        return nullptr;
    }
    g_host = host;
#if !defined(__ANDROID__)
    // Before anything is read or written: a test run keeps to its work folder, or the host stops. (An Android app's
    // data is its own private folder, which no player's folder can be.)
    if (const char* const refusal = guard()) {
        log(std::string("test extension: ") + refusal);
        return nullptr;
    }
#endif
    const std::optional<std::string> plan = env("LUMA_HARNESS_PLAN");
    if (!plan.has_value()) {
        log("test extension: no harness plan (LUMA_HARNESS_PLAN); it runs only a harness's plan");
        return nullptr;
    }
    harness::start(host, *plan, log, harness_quit);
    if (const std::optional<std::string> wav = env("GALAXY_AUDIO_WAV_DUMP")) {
        g_wav.start(*wav, static_cast<std::uint32_t>(std::clamp<std::uint64_t>(env_u64("GALAXY_AUDIO_WAV_DUMP_SECONDS", 15u), 1u, 3600u)),
                    host->size >= offsetof(NebulaHostForExtension, started_again) + sizeof(host->started_again) &&
                        host->started_again != 0u);
    }
    g_mods_changed = env("GALAXY_TEST_MODS_CHANGED").has_value();
    NebulaHostExtension& e = g_extension;
    e.version = NEBULA_HOST_EXTENSION_VERSION;
    e.size = sizeof(e);
    e.flags = (!harness::real_time() ? NEBULA_EXT_FIXED_STEPS : 0u) |
              (env_flag("GALAXY_AUDIO_WAV_DUMP_ONLY") ? NEBULA_EXT_NO_SOUND_DEVICE : 0u) |
              (env("GALAXY_TEST_MODS_SCREEN").has_value() ? NEBULA_EXT_MODS_SCREEN : 0u) |
              (harness::as_pad() ? NEBULA_EXT_CONTROLS_PAD : 0u) |
              (env("GALAXY_TEST_REMAKE_TARGETS").has_value() ? NEBULA_EXT_REMAKE_TARGETS : 0u);
    e.controls = controls;
    e.frame = frame;
    e.picture_path = picture_path;
    e.sound = g_wav.on() ? sound : nullptr;
    e.mods_changed = mods_changed;
    e.ending = process_ending;
    e.vram_budget_mb = static_cast<std::uint32_t>(env_u64("GALAXY_TEST_VRAM_BUDGET_MB", 0u));
    e.disc_read_delay_us = static_cast<std::uint32_t>(env_u64("GALAXY_TEST_DISC_DELAY_US", 0u));
    e.save_write_delay_us = static_cast<std::uint32_t>(env_u64("GALAXY_TEST_SAVE_DELAY_US", 0u));
    e.device_rebuild_delay_ms = static_cast<std::uint32_t>(env_u64("GALAXY_TEST_REBUILD_DELAY_MS", 0u));
    g_cpu_load.start(env_u64("GALAXY_TEST_CPU_LOAD", 0u));
    e.log_line = [](void*, const char* tag, const char* text) { harness::log_line(tag, text); };
    e.service = [](void*, const char* name, std::uint32_t major, std::uint32_t minor) { return harness::service(name, major, minor); };
    // The registry's test services (services/registry.md 14).
    e.luma_providers = test_services::providers(&e.luma_provider_count);
    e.motor = [](void*, std::uint8_t strength) { harness::motor(strength); };
    e.holds_menus = [](void*) -> std::uint32_t { return harness::holds_menus() ? 1u : 0u; };
    log("test extension attached");
    return &e;
}
