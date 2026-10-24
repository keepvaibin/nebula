#include "galaxy/native_host.h"
#include "galaxy/wii_device_data.h"

#include "galaxy/native_audio_anomaly.h"
#include "galaxy/native_audio_queue.h"
#include "galaxy/jaudio_host_mixer.h"
#include "galaxy/controller_hid.h"
#include "galaxy/input_bindings.h"
#include "galaxy/native_input.h"
#include "galaxy/native_settings_pad.h"
#include "galaxy/pointer_control.h"
#include "galaxy/rumble.h"
#include "galaxy/tilt.h"
#include "galaxy/diagnostic_stick_lease.h"
#include "galaxy/experimental_ultrawide_aspect.h"
#include "galaxy/gx_d3d12.h"
#include "galaxy/native_ios_anomaly.h"
#include "galaxy/owned_storage_worker.h"
#include "galaxy/runtime_settings.h"
#include "galaxy/scope_exit.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "galaxy/windows_power.h"
#include <SetupAPI.h>
#include <Xinput.h>
#include <bcrypt.h>
#include <hidsdi.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
#include <xaudio2.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <syncstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace galaxy::host {
namespace {

using Microsoft::WRL::ComPtr;

#if defined(GALAXY_NATIVE_HOST_TEST_SUPPORT)
// Fault points are confined to the test-support build and its calling thread.
using NativeHostOwnershipTestHook = bool (*)(
    unsigned, const std::filesystem::path&, unsigned);
thread_local NativeHostOwnershipTestHook s_host_ownership_test_hook{};
bool host_ownership_test_point(unsigned phase,
    const std::filesystem::path& path, unsigned index = 0u) {
    return s_host_ownership_test_hook != nullptr &&
        s_host_ownership_test_hook(phase, path, index);
}
#endif

bool read_env_flag(const char* name, bool default_value);
std::optional<std::string> read_env_string(const char* name);

constexpr std::uint32_t kRmge01BootArenaLow = 0x806BDFA0u;

std::optional<std::filesystem::path> get_env_path(const wchar_t* name) {
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(name, buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return std::nullopt;
    }
    return std::filesystem::path(buffer);
}

bool read_env_flag_uncached(const char* name, bool default_value) {
    char buffer[32]{};
    std::size_t length = 0;
    if (getenv_s(&length, buffer, sizeof(buffer), name) != 0 ||
        length <= 1) {
        return default_value;
    }
    return buffer[0] != '0' && buffer[0] != 'f' && buffer[0] != 'F' &&
           buffer[0] != 'n' && buffer[0] != 'N';
}

// Literal name pointers identify flags; repeated reads cost a few loads. The cache is enabled
// once the runtime has finished configuring. Tests toggle variables between address spaces and
// must see live values.
struct EnvFlagCacheSlot {
    std::atomic<const char*> name{nullptr};
    std::atomic<int> value{0};
};
EnvFlagCacheSlot g_env_flag_cache[256];
std::atomic<bool> g_env_flag_cache_enabled{false};

bool read_env_flag(const char* name, bool default_value = false) {
    if (!g_env_flag_cache_enabled.load(std::memory_order_acquire)) {
        return read_env_flag_uncached(name, default_value);
    }
    const auto key = reinterpret_cast<std::uintptr_t>(name);
    std::size_t index = static_cast<std::size_t>((key >> 3u) ^ (key >> 13u)) & 255u;
    for (std::size_t probe = 0; probe < 8u; ++probe, index = (index + 1u) & 255u) {
        EnvFlagCacheSlot& slot = g_env_flag_cache[index];
        const char* const owner = slot.name.load(std::memory_order_acquire);
        if (owner == name) {
            const int value = slot.value.load(std::memory_order_acquire);
            if (value != 0) return value > 0;
            break;
        }
        if (owner == nullptr) {
            const bool result = read_env_flag_uncached(name, default_value);
            const char* expected = nullptr;
            if (slot.name.compare_exchange_strong(expected, name, std::memory_order_acq_rel)) {
                slot.value.store(result ? 1 : -1, std::memory_order_release);
            }
            return result;
        }
    }
    return read_env_flag_uncached(name, default_value);
}

thread_local NativeInputPollBreakdown s_input_poll_breakdown{};
thread_local bool s_input_poll_breakdown_active{};
thread_local std::uint64_t s_input_poll_stage_started_ns{};
thread_local std::size_t s_input_poll_stage{};

std::uint64_t input_poll_steady_ns() noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

void input_poll_diagnostic_stage(NativeInputPollStage stage) noexcept {
    if (!s_input_poll_breakdown_active) return;
    const auto now = input_poll_steady_ns();
    if (now >= s_input_poll_stage_started_ns) {
        s_input_poll_breakdown.stage_ns[s_input_poll_stage] += now - s_input_poll_stage_started_ns;
    }
    s_input_poll_stage_started_ns = now;
    s_input_poll_stage = static_cast<std::size_t>(stage);
}

float read_env_float_setting(
    const char* name,
    float default_value,
    float min_value,
    float max_value) {
    // Cached string read: queried on every input report (deadzones, trigger thresholds).
    const std::optional<std::string> value = read_env_string(name);
    if (!value.has_value()) {
        return default_value;
    }
    const char* const buffer = value->c_str();
    char* end = nullptr;
    const float parsed = std::strtof(buffer, &end);
    if (end == buffer || *end != '\0' || !std::isfinite(parsed)) {
        throw std::runtime_error(
            std::string("invalid float environment value for ") + name);
    }
    return std::clamp(parsed, min_value, max_value);
}

int read_env_int_setting(
    const char* name,
    int default_value,
    int min_value,
    int max_value) {
    // Cached string read: queried on every input report (deadzones, trigger thresholds).
    const std::optional<std::string> value = read_env_string(name);
    if (!value.has_value()) {
        return default_value;
    }
    const char* const buffer = value->c_str();
    char* end = nullptr;
    const long parsed = std::strtol(buffer, &end, 10);
    if (end == buffer || *end != '\0') {
        throw std::runtime_error(
            std::string("invalid integer environment value for ") + name);
    }
    return std::clamp(
        static_cast<int>(parsed), min_value, max_value);
}

std::int16_t xinput_left_deadzone() {
    return static_cast<std::int16_t>(
        read_env_int_setting(
            "GALAXY_XINPUT_LEFT_DEADZONE",
            XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE,
            0,
            32767));
}

std::int16_t xinput_pointer_deadzone() {
    return static_cast<std::int16_t>(
        read_env_int_setting(
            "GALAXY_XINPUT_POINTER_DEADZONE",
            XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE,
            0,
            32767));
}

std::uint8_t xinput_trigger_threshold() {
    return static_cast<std::uint8_t>(
        read_env_int_setting(
            "GALAXY_XINPUT_TRIGGER_THRESHOLD",
            96,
            0,
            255));
}

float xinput_pointer_speed() {
    return read_env_float_setting(
        "GALAXY_XINPUT_POINTER_SPEED", 2.4f, 0.0f, 10.0f);
}

float keyboard_pointer_speed() {
    return read_env_float_setting(
        "GALAXY_KEYBOARD_POINTER_SPEED", 1.6f, 0.0f, 10.0f);
}

// The environment overrides take precedence over the CONTROLS settings.
float xinput_pointer_speed_or(float configured) {
    return read_env_float_setting(
        "GALAXY_XINPUT_POINTER_SPEED", configured, 0.0f, 10.0f);
}

float keyboard_pointer_speed_or(float configured) {
    return read_env_float_setting(
        "GALAXY_KEYBOARD_POINTER_SPEED", configured, 0.0f, 10.0f);
}

bool trace_boot_input() {
    static const bool enabled = read_env_flag("GALAXY_TRACE_BOOT_INPUT");
    return enabled;
}

bool trace_input_proof_compact() {
    static const bool enabled =
        read_env_flag("GALAXY_TRACE_INPUT_PROOF_COMPACT");
    return enabled;
}

std::uint64_t read_env_u64_setting(
    const char* name,
    std::uint64_t default_value) {
    // Cached string read: queried on every input report (deadzones, trigger thresholds).
    const std::optional<std::string> value = read_env_string(name);
    if (!value.has_value()) {
        return default_value;
    }
    const char* const buffer = value->c_str();
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(buffer, &end, 10);
    if (end == buffer || *end != '\0') {
        throw std::runtime_error(
            std::string("invalid integer environment value for ") + name);
    }
    return static_cast<std::uint64_t>(parsed);
}

bool trace_boot_input_at(std::uint64_t current_vi) {
    if (!trace_boot_input()) {
        return false;
    }
    static const std::uint64_t start =
        read_env_u64_setting("GALAXY_TRACE_BOOT_INPUT_START_VI", 0);
    static const std::uint64_t end =
        read_env_u64_setting("GALAXY_TRACE_BOOT_INPUT_END_VI", 0);
    return current_vi >= start && (end == 0 || current_vi <= end);
}

bool trace_boot_input_sample_at(
    std::uint64_t current_vi,
    bool interesting = false) {
    if (!trace_boot_input_at(current_vi)) {
        return false;
    }
    if (interesting && !trace_input_proof_compact()) {
        return true;
    }
    static const std::uint64_t start =
        read_env_u64_setting("GALAXY_TRACE_BOOT_INPUT_START_VI", 0);
    static const std::uint64_t first_full_samples =
        read_env_u64_setting("GALAXY_TRACE_BOOT_INPUT_INITIAL_SAMPLES", 8);
    static const std::uint64_t interval =
        read_env_u64_setting("GALAXY_TRACE_BOOT_INPUT_INTERVAL_VI", 30);
    const std::uint64_t elapsed = current_vi >= start ? current_vi - start : 0;
    if (elapsed < first_full_samples) {
        return true;
    }
    return interval == 0 || (elapsed % interval) == 0;
}

bool xinput_polling_disabled() {
    static const bool disabled = read_env_flag("GALAXY_DISABLE_XINPUT");
    return disabled;
}

bool xinput_requires_focus() {
    static const bool requires_focus =
        read_env_flag("GALAXY_XINPUT_REQUIRE_FOCUS");
    return requires_focus;
}

bool trace_audio_queue() {
    static const bool enabled = read_env_flag("GALAXY_TRACE_AUDIO_QUEUE");
    return enabled;
}

bool trace_host_startup() {
    static const bool enabled =
        read_env_flag("GALAXY_TRACE_HOST_STARTUP") ||
        read_env_flag("GALAXY_TRACE_RUNTIME_SCAFFOLD");
    return enabled;
}

bool strict_audio_proof_enabled() {
    static const bool enabled = read_env_flag("GALAXY_STRICT_AUDIO_PROOF");
    return enabled;
}

class ScopedMmcssAudioThread {
public:
    ScopedMmcssAudioThread() {
        if (!read_env_flag("GALAXY_AUDIO_MMCSS", true)) {
            return;
        }
        avrt_ = LoadLibraryW(L"avrt.dll");
        if (avrt_ == nullptr) {
            if (trace_audio_queue()) {
                std::cout << "[audio] MMCSS unavailable: avrt.dll not loaded\n";
            }
            return;
        }
        av_set_characteristics_ =
            reinterpret_cast<AvSetMmThreadCharacteristicsWFn>(
                GetProcAddress(avrt_, "AvSetMmThreadCharacteristicsW"));
        av_set_priority_ = reinterpret_cast<AvSetMmThreadPriorityFn>(
            GetProcAddress(avrt_, "AvSetMmThreadPriority"));
        av_revert_ = reinterpret_cast<AvRevertMmThreadCharacteristicsFn>(
            GetProcAddress(avrt_, "AvRevertMmThreadCharacteristics"));
        if (av_set_characteristics_ == nullptr || av_revert_ == nullptr) {
            if (trace_audio_queue()) {
                std::cout << "[audio] MMCSS unavailable: required exports missing\n";
            }
            FreeLibrary(avrt_);
            avrt_ = nullptr;
            return;
        }

        DWORD task_index = 0;
        task_handle_ = av_set_characteristics_(L"Pro Audio", &task_index);
        if (task_handle_ == nullptr) {
            task_index = 0;
            task_handle_ = av_set_characteristics_(L"Audio", &task_index);
        }
        if (task_handle_ == nullptr) {
            if (trace_audio_queue()) {
                std::cout << "[audio] MMCSS registration failed error="
                          << GetLastError() << '\n';
            }
            return;
        }

        if (av_set_priority_ != nullptr) {
            constexpr int kAvrtPriorityCritical = 2;
            av_set_priority_(task_handle_, kAvrtPriorityCritical);
        }
        if (trace_audio_queue()) {
            std::cout << "[audio] MMCSS audio worker registered\n";
        }
    }

    ~ScopedMmcssAudioThread() {
        if (task_handle_ != nullptr && av_revert_ != nullptr) {
            av_revert_(task_handle_);
        }
        if (avrt_ != nullptr) {
            FreeLibrary(avrt_);
        }
    }

    ScopedMmcssAudioThread(const ScopedMmcssAudioThread&) = delete;
    ScopedMmcssAudioThread& operator=(const ScopedMmcssAudioThread&) = delete;

private:
    using AvSetMmThreadCharacteristicsWFn = HANDLE (WINAPI*)(LPCWSTR, LPDWORD);
    using AvSetMmThreadPriorityFn = BOOL (WINAPI*)(HANDLE, int);
    using AvRevertMmThreadCharacteristicsFn = BOOL (WINAPI*)(HANDLE);

    HMODULE avrt_ = nullptr;
    HANDLE task_handle_ = nullptr;
    AvSetMmThreadCharacteristicsWFn av_set_characteristics_ = nullptr;
    AvSetMmThreadPriorityFn av_set_priority_ = nullptr;
    AvRevertMmThreadCharacteristicsFn av_revert_ = nullptr;
};

std::uint32_t read_env_u32(
    const char* name,
    std::uint32_t default_value,
    std::uint32_t min_value,
    std::uint32_t max_value) {
    char buffer[32]{};
    std::size_t length = 0;
    if (getenv_s(&length, buffer, sizeof(buffer), name) != 0 ||
        length <= 1) {
        return default_value;
    }
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(buffer, &end, 10);
    if (end == buffer) {
        return default_value;
    }
    return std::clamp(
        static_cast<std::uint32_t>(parsed),
        min_value,
        max_value);
}

// Frames of the fade-in after the output ran dry: 4 ms at 32 kHz, short enough to hide the step.
constexpr std::size_t kFadeInFrames = 128u;

// Little-endian 16-bit stereo: the first kFadeInFrames frames rise linearly from silence.
void fade_in_pcm16_le_stereo(std::span<std::byte> pcm) noexcept {
    const std::size_t frames = pcm.size() / 4u;
    const std::size_t ramp = std::min<std::size_t>(frames, kFadeInFrames);
    constexpr auto kSteps = static_cast<std::int32_t>(kFadeInFrames);
    for (std::size_t i = 0; i < ramp * 2u; ++i) {
        std::byte* sample = pcm.data() + i * 2u;
        const auto value = static_cast<std::int16_t>(
            std::to_integer<std::uint16_t>(sample[0]) |
            (std::to_integer<std::uint16_t>(sample[1]) << 8));
        const auto scaled = static_cast<std::uint16_t>(static_cast<std::int16_t>(
            value * static_cast<std::int32_t>(i / 2u) / kSteps));
        sample[0] = static_cast<std::byte>(scaled & 0xFFu);
        sample[1] = static_cast<std::byte>(scaled >> 8);
    }
}

class AudioWavDump {
public:
    ~AudioWavDump() {
        finish();
    }

    AudioWavDump(const AudioWavDump&) = delete;
    AudioWavDump& operator=(const AudioWavDump&) = delete;

    AudioWavDump() = default;

    bool append(std::span<const std::byte> pcm_le, std::uint32_t sample_rate) {
        // The control owner can capture unheard DMA while an old audio worker
        // finishes its last buffer during device loss. Serialize diagnostics.
        std::lock_guard<std::mutex> lock(mutex_);
        return append_locked(pcm_le, sample_rate);
    }

    bool finish() {
        std::lock_guard<std::mutex> lock(mutex_);
        return finish_locked();
    }

#if defined(GALAXY_NATIVE_HOST_TEST_SUPPORT)
    void fail_stream_for_test() {
        std::lock_guard<std::mutex> lock(mutex_);
        out_.setstate(std::ios::badbit);
    }
#endif

private:
    bool append_locked(std::span<const std::byte> pcm_le, std::uint32_t sample_rate) {
        if (out_.is_open() && sample_rate_ != sample_rate) {
            if (!finish_locked()) {
                return false;
            }
            data_bytes_ = 0;
            open_attempted_ = false;
            ++segment_index_;
        }
        if (!ensure_open(sample_rate)) {
            return false;
        }
        if (pcm_le.empty()) {
            return true;
        }
        if (skip_leading_silence_ && data_bytes_ == 0 &&
            is_silent_pcm(pcm_le)) {
            return true;
        }
        if (data_bytes_ >= max_bytes_) {
            return true;
        }
        std::size_t write_size = std::min<std::size_t>(
            pcm_le.size(),
            static_cast<std::size_t>(max_bytes_ - data_bytes_));
        write_size &= ~std::size_t{3};
        if (write_size == 0) {
            return true;
        }
        out_.write(
            reinterpret_cast<const char*>(pcm_le.data()),
            static_cast<std::streamsize>(write_size));
        if (!out_) {
            std::cerr << "[audio] WAV dump write failed: " << path_ << '\n';
            finish_locked();
            disabled_ = true;
            return false;
        }
        data_bytes_ += static_cast<std::uint32_t>(write_size);
        return true;
    }

    bool finish_locked() {
        if (!out_.is_open()) {
            return !disabled_;
        }
        out_.seekp(4, std::ios::beg);
        write_u32_le(out_, 36u + data_bytes_);
        out_.seekp(40, std::ios::beg);
        write_u32_le(out_, data_bytes_);
        out_.flush();
        const bool contents_written = static_cast<bool>(out_);
        out_.close();
        if (!contents_written || !out_) {
            disabled_ = true;
            std::cerr << "[audio] WAV dump finalization failed: " << path_ << '\n';
            return false;
        }
        std::cout << "[audio] WAV dump wrote " << data_bytes_
                  << " bytes at " << sample_rate_ << " Hz: " << path_
                  << '\n';
        return true;
    }

    std::mutex mutex_;

    static bool is_silent_pcm(std::span<const std::byte> pcm_le) {
        return std::all_of(
            pcm_le.begin(),
            pcm_le.end(),
            [](std::byte sample) { return sample == std::byte{0}; });
    }

    static void write_u16_le(std::ostream& out, std::uint16_t value) {
        const char bytes[2] = {
            static_cast<char>(value & 0xFFu),
            static_cast<char>((value >> 8) & 0xFFu),
        };
        out.write(bytes, sizeof(bytes));
    }

    static void write_u32_le(std::ostream& out, std::uint32_t value) {
        const char bytes[4] = {
            static_cast<char>(value & 0xFFu),
            static_cast<char>((value >> 8) & 0xFFu),
            static_cast<char>((value >> 16) & 0xFFu),
            static_cast<char>((value >> 24) & 0xFFu),
        };
        out.write(bytes, sizeof(bytes));
    }

    bool ensure_open(std::uint32_t sample_rate) {
        if (disabled_) {
            return false;
        }
        if (out_.is_open()) {
            return true;
        }
        if (open_attempted_) {
            return false;
        }
        open_attempted_ = true;

        const std::optional<std::filesystem::path> path =
            get_env_path(L"GALAXY_AUDIO_WAV_DUMP");
        if (!path.has_value() || path->empty()) {
            disabled_ = true;
            return false;
        }

        if (base_path_.empty()) {
            base_path_ = *path;
        }
        path_ = base_path_;
        if (segment_index_ != 0u) {
            const std::wstring extension = base_path_.extension().wstring();
            const std::wstring filename =
                base_path_.stem().wstring() + L".rate-" +
                std::to_wstring(sample_rate) + L".part-" +
                std::to_wstring(segment_index_) + extension;
            path_ = base_path_.parent_path() / filename;
        }
        max_bytes_ = read_env_u32(
            "GALAXY_AUDIO_WAV_DUMP_MAX_BYTES",
            32000u * 4u * 15u,
            4096u,
            128u * 1024u * 1024u);
        max_bytes_ &= ~3u;
        skip_leading_silence_ =
            read_env_flag("GALAXY_AUDIO_WAV_DUMP_SKIP_LEADING_SILENCE");
        std::error_code ec;
        const std::filesystem::path parent = path_.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent, ec);
            if (ec) {
                std::cerr << "[audio] WAV dump directory failed: "
                          << parent << " error=" << ec.message() << '\n';
                disabled_ = true;
                return false;
            }
        }

        out_.open(path_, std::ios::binary | std::ios::trunc);
        if (!out_) {
            std::cerr << "[audio] WAV dump open failed: " << path_ << '\n';
            disabled_ = true;
            return false;
        }

        out_.write("RIFF", 4);
        write_u32_le(out_, 36u);
        out_.write("WAVEfmt ", 8);
        write_u32_le(out_, 16u);
        write_u16_le(out_, 1u);
        write_u16_le(out_, 2u);
        write_u32_le(out_, sample_rate);
        write_u32_le(out_, sample_rate * 4u);
        write_u16_le(out_, 4u);
        write_u16_le(out_, 16u);
        out_.write("data", 4);
        write_u32_le(out_, 0u);
        if (!out_) {
            std::cerr << "[audio] WAV dump header failed: " << path_ << '\n';
            finish_locked();
            disabled_ = true;
            return false;
        }

        sample_rate_ = sample_rate;
        std::cout << "[audio] WAV dump enabled: " << path_
                  << " max-bytes=" << max_bytes_
                  << " skip-leading-silence="
                  << (skip_leading_silence_ ? 1 : 0) << '\n';
        return true;
    }

    std::ofstream out_;
    std::filesystem::path base_path_;
    std::filesystem::path path_;
    std::uint32_t data_bytes_{};
    std::uint32_t max_bytes_{};
    std::uint32_t sample_rate_{};
    std::uint32_t segment_index_{};
    bool disabled_{};
    bool open_attempted_{};
    bool skip_leading_silence_{};
};

}  // namespace

#if defined(GALAXY_NATIVE_HOST_TEST_SUPPORT)
void native_host_set_ownership_test_hook(NativeHostOwnershipTestHook hook) {
    s_host_ownership_test_hook = hook;
}

bool native_host_test_handle_cleanup() {
    const HANDLE event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    const HDEVINFO devices = SetupDiCreateDeviceInfoList(nullptr, nullptr);
    if (event == nullptr || devices == INVALID_HANDLE_VALUE) {
        const ScopedHostHandle event_owner(event);
        const ScopedDeviceInfoList devices_owner(devices);
        return false;
    }
    try {
        const ScopedHostHandle event_owner(event);
        const ScopedDeviceInfoList devices_owner(devices);
        throw std::bad_alloc();
    } catch (const std::bad_alloc&) {
    }
    DWORD flags{};
    SP_DEVINFO_LIST_DETAIL_DATA_W detail{};
    detail.cbSize = sizeof(detail);
    return GetHandleInformation(event, &flags) == FALSE &&
        SetupDiGetDeviceInfoListDetailW(devices, &detail) == FALSE;
}

bool native_host_test_wav_dump(std::span<const std::byte> pcm,
    std::uint32_t rate, bool fail_finalization) {
    AudioWavDump dump;
    if (!dump.append(pcm, rate)) return false;
    if (fail_finalization) dump.fail_stream_for_test();
    return dump.finish();
}

bool native_host_test_concurrent_wav_dump() {
    AudioWavDump dump;
    bool ok[2]{true, true};
    const auto write = [&](unsigned lane) {
        std::array<std::byte, 16> pcm;
        pcm.fill(static_cast<std::byte>(lane + 1u));
        for (unsigned i = 0; i < 200; ++i) ok[lane] &= dump.append(pcm, 32000u);
    };
    std::thread worker(write, 0u);
    write(1u);
    worker.join();
    return ok[0] && ok[1] && dump.finish();
}
#endif

class NativeAudioSink final : public IXAudio2VoiceCallback,
                              public IXAudio2EngineCallback {
public:
    NativeAudioSink() = default;
    ~NativeAudioSink() {
        // Preserves process-lifetime health before XAudio2 is released. Window deltas cannot prove that
        // boot and title were clean, and engine glitch counters are unavailable after release. Format
        // only after the worker stops, so terminal diagnostics cannot starve audio.
        const AudioSinkStats route_health = stats();
        const bool route_output_available = output_available();
        if (read_env_flag("GALAXY_TRACE_BOOT_LOG")) {
            std::cerr << "[boot] NativeAudioSink destructor begin\n";
        }
        shutdown();
        const AudioSinkStats terminal_health = stats();
        dump_anomaly_ledger();
        std::cout << std::dec << "[audio-route-health] scope=process-audio"
                  << " snapshot-before-shutdown=1 errors-include-shutdown=1"
                  << " ready=" << (route_health.initialization_ready ? 1 : 0)
                  << " first-submit=" << (route_health.first_submit_observed ? 1 : 0)
                  << " submitted=" << route_health.submitted_buffers
                  << " completed=" << route_health.completed_buffers
                  << " queued=" << route_health.queued_buffers
                  << " pending=" << route_health.pending_buffers
                  << " dropped=" << terminal_health.dropped_buffers
                  << " submit-failures=" << terminal_health.submit_failure_events
                  << " source-start-failures=" << terminal_health.source_start_failure_events
                  << " voice-errors=" << terminal_health.voice_error_events
                  << " engine-critical-errors=" << terminal_health.engine_critical_error_events
                  << " callback-wake-errors=" << terminal_health.callback_wake_error_events
                  << " frequency-ratio-snapshot-coherent=" << terminal_health.frequency_ratio_snapshot_coherent
                  << " frequency-ratio-errors=" << terminal_health.frequency_ratio_apply_error_events
                  << " pending-overflows=" << terminal_health.pending_overflow_events
                  << " engine-glitches=" << route_health.engine_glitches_since_start
                  << " queue-empty-events=" << route_health.queue_empty_events
                  << " empty-recoveries=" << route_health.empty_recovery_events
                  << " empty-over-1ms=" << route_health.empty_over_1_ms
                  << " empty-over-5ms=" << route_health.empty_over_5_ms
                  << " max-empty-us=" << route_health.max_empty_us
                  << " silent-us=" << total_silent_us_.load(std::memory_order_acquire)
                  << " max-silent-us=" << max_silent_us_.load(std::memory_order_acquire)
                  << " max-submit-gap-us=" << route_health.max_submit_delta_us
                  << " voice-recreations=" << route_health.voice_recreation_events
                  << " endpoint-output-available=" << route_output_available
                  << " endpoint-retry-attempts=" << endpoint_retry_attempts_
                  << " endpoint-unheard-buffers=" << endpoint_unheard_buffers_.load()
                  << '\n';
        if (read_env_flag("GALAXY_TRACE_BOOT_LOG")) {
            std::cerr << "[boot] NativeAudioSink destructor end\n";
        }
    }

    NativeAudioSink(const NativeAudioSink&) = delete;
    NativeAudioSink& operator=(const NativeAudioSink&) = delete;

    bool submit_pcm16_be_stereo(
        std::span<const std::byte> source,
        std::uint32_t sample_rate,
        std::uint32_t source_address,
        std::uint64_t guest_start_ticks) {
        if (source.empty()) {
            return true;
        }
        if ((source.size() & 3u) != 0u ||
            source.size() > std::numeric_limits<UINT32>::max()) {
            log_drop("invalid AI PCM buffer size");
            strict_failure_.store(
                true, std::memory_order_release);
            set_strict_failure_reason("invalid AI PCM buffer size");
            return false;
        }
        if (sample_rate != 32'000u && sample_rate != 48'000u) {
            log_drop("unsupported Wii AI PCM sample rate");
            return false;
        }
        if (read_env_flag("GALAXY_AUDIO_WAV_DUMP_ONLY")) {
            const std::optional<std::filesystem::path> dump_path =
                get_env_path(L"GALAXY_AUDIO_WAV_DUMP");
            if (!dump_path.has_value() || dump_path->empty()) {
                std::cerr << "[audio] GALAXY_AUDIO_WAV_DUMP_ONLY requires "
                             "GALAXY_AUDIO_WAV_DUMP\n";
                return false;
            }
            std::vector<std::byte> pcm_le;
            pcm_le.resize(source.size());
            if (!native_audio_convert_wii_ai_pcm16(
                    source, std::span<std::byte>(pcm_le))) {
                log_drop("invalid Wii AI PCM channel conversion");
                return false;
            }
            finish_output(pcm_le, sample_rate);
            if (!wav_dump_.append(
                std::span<const std::byte>(pcm_le.data(), pcm_le.size()),
                sample_rate)) {
                log_drop("WAV dump-only output failed");
                return false;
            }
            const auto submit_time = std::chrono::steady_clock::now();
            const std::uint64_t submit_measurement_generation =
                current_measurement_window_generation();
            std::uint64_t submit_delta_us = 0u;
            std::uint64_t expected_us = 0u;
            if (has_last_submit_time_) {
                const double submit_delta_ms =
                    std::chrono::duration<double, std::milli>(
                        submit_time - last_submit_time_).count();
                submit_delta_us =
                    static_cast<std::uint64_t>(submit_delta_ms * 1000.0);
                submit_delta_events_.fetch_add(1, std::memory_order_acq_rel);
                total_submit_delta_us_.fetch_add(
                    submit_delta_us, std::memory_order_acq_rel);
                std::uint64_t current_max =
                    max_submit_delta_us_.load(std::memory_order_acquire);
                while (submit_delta_us > current_max &&
                       !max_submit_delta_us_.compare_exchange_weak(
                           current_max,
                           submit_delta_us,
                           std::memory_order_acq_rel,
                           std::memory_order_acquire)) {
                }
                const std::uint64_t frames =
                    static_cast<std::uint64_t>(source.size()) / 4u;
                expected_us =
                    (frames * 1'000'000ull + sample_rate - 1u) /
                    sample_rate;
                if (expected_us != 0u && submit_delta_us > expected_us) {
                    submit_delta_over_expected_.fetch_add(
                        1, std::memory_order_acq_rel);
                }
                if (submit_delta_us > 25'000ull) {
                    submit_delta_over_25_ms_.fetch_add(
                        1, std::memory_order_acq_rel);
                }
            }
            if (!record_measurement_window_acceptance(
                    submit_measurement_generation,
                    sample_rate,
                    source.size())) {
                log_drop("native audio accepted-rate ledger overflow");
                return false;
            }
            if (!record_measurement_window_submission(
                    submit_measurement_generation,
                    sample_rate,
                    source.size(),
                    last_submit_time_,
                    submit_time,
                    submit_delta_us,
                    expected_us,
                    last_submit_measurement_generation_,
                    has_last_submit_time_)) {
                log_drop("native audio measurement rate ledger overflow");
                return false;
            }
            last_submit_time_ = submit_time;
            last_submit_measurement_generation_ =
                submit_measurement_generation;
            has_last_submit_time_ = true;
            const std::uint64_t submitted_buffers =
                submitted_buffers_.fetch_add(1, std::memory_order_acq_rel) + 1u;
            if (submitted_buffers <= 8 || submitted_buffers % 1000 == 0) {
                std::cout << "[audio] dump-only AI PCM buffer #"
                          << submitted_buffers
                          << " bytes=" << source.size() << '\n';
            }
            return true;
        }
        if (read_env_flag("GALAXY_AUDIO_DISABLE")) {
            if (!disabled_) {
                disabled_ = true;
                std::cout
                    << "[audio] native audio disabled by GALAXY_AUDIO_DISABLE\n";
            }
            return false;
        }
        const NativeAudioInitializationSnapshot initialization_before =
            initialization_gate_.snapshot();
        const bool measure_initialization = initialization_before.state !=
            NativeAudioInitializationState::Ready;
        const auto initialization_started = measure_initialization
            ? std::chrono::steady_clock::now()
            : std::chrono::steady_clock::time_point{};
        const bool initialized = initialization_gate_.ensure_ready_for_submit(
            [this] { return initialize_backend(); });
        const auto initialization_wait_us_signed = measure_initialization
            ? std::chrono::duration_cast<std::chrono::microseconds>(
                  std::chrono::steady_clock::now() - initialization_started).count()
            : 0;
        const auto initialization_wait_us = static_cast<std::uint64_t>(
            std::max<std::int64_t>(initialization_wait_us_signed, 0));
        const NativeAudioInitializationSnapshot initialization_after =
            initialization_gate_.snapshot();
        const bool initialized_backend_during_submit =
            initialization_after.initialization_attempts >
            initialization_before.initialization_attempts;
        bool expected_first_submit = false;
        if (first_submit_observed_.compare_exchange_strong(
                expected_first_submit,
                true,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            first_submit_initialization_wait_us_.store(
                initialization_wait_us, std::memory_order_release);
            first_submit_initialized_backend_.store(
                initialized_backend_during_submit,
                std::memory_order_release);
            first_submit_thread_id_.store(
                GetCurrentThreadId(), std::memory_order_release);
            std::osyncstream(std::cout)
                << "[audio-init] first-submit=1 ready-before="
                << (initialization_before.state ==
                            NativeAudioInitializationState::Ready
                        ? 1
                        : 0)
                << " preinitialized="
                << (initialization_before.preinitialized ? 1 : 0)
                << " init-at-submit="
                << (initialized_backend_during_submit ? 1 : 0)
                << " init-wait-us=" << initialization_wait_us
                << " init-thread="
                << initialization_thread_id_.load(std::memory_order_acquire)
                << " submit-thread=" << GetCurrentThreadId()
                << " result=" << (initialized ? "ready" : "failed")
                << '\n';
        }
        if (!initialized) {
            return false;
        }
        const std::uint32_t initialization_thread_id =
            initialization_thread_id_.load(std::memory_order_acquire);
        if (initialization_thread_id != 0u &&
            initialization_thread_id != GetCurrentThreadId()) {
            strict_failure_.store(true, std::memory_order_release);
            set_strict_failure_reason(
                "native audio submit moved off initialization thread");
            return false;
        }
        if (strict_failure_.load(std::memory_order_acquire)) {
            return false;
        }

        if (endpoint_recovery_task_.active() || endpoint_offline_ ||
            endpoint_recovery_pending_.load(std::memory_order_acquire)) {
            // The guest still produced this DMA at its normal AI deadline. It cannot be heard without a
            // device, so it is never reported as played.
            endpoint_unheard_buffers_.fetch_add(1u, std::memory_order_relaxed);
            // Keeps an explicitly requested PCM diagnostic even when the endpoint is absent. This output
            // remains unheard.
            if (get_env_path(L"GALAXY_AUDIO_WAV_DUMP").has_value()) {
                std::vector<std::byte> pcm_le(source.size());
                if (!native_audio_convert_wii_ai_pcm16(source, pcm_le)) {
                    log_drop("offline audio WAV diagnostic failed");
                    return false;
                }
                finish_output(pcm_le, sample_rate);
                if (!wav_dump_.append(pcm_le, sample_rate)) {
                    log_drop("offline audio WAV diagnostic failed");
                    return false;
                }
            }
            return true;
        }
        auto buffer = std::make_unique<SubmittedBuffer>();
        buffer->sample_rate = sample_rate;
        buffer->source_address = source_address;
        buffer->guest_start_ticks = guest_start_ticks;
        buffer->measurement_generation =
            current_measurement_window_generation();
        buffer->bytes.resize(source.size());
        if (!native_audio_convert_wii_ai_pcm16(
                source, std::span<std::byte>(buffer->bytes))) {
            log_drop("invalid Wii AI PCM channel conversion");
            return false;
        }
        finish_output(buffer->bytes, sample_rate);

        if (!enqueue_pending_buffer(std::move(buffer))) {
            return false;
        }
        return true;
    }

    // Accepted and not yet played: the worker's pending queue and the device's.
    unsigned unplayed_buffers() const {
        return queue_mirror_.queued_buffers() + pending_queue_depth_.load(std::memory_order_acquire);
    }

    // The buffers the voice waits for before it starts (and starts again after running dry).
    unsigned prebuffer_buffers() const noexcept {
        return prebuffer_buffers_;
    }

    std::uint64_t dropped_buffers() const {
        return dropped_buffers_.load(std::memory_order_relaxed) +
            endpoint_unheard_buffers_.load(std::memory_order_relaxed);
    }

    bool output_available() const noexcept {
        return !endpoint_recovery_task_.active() && initialized_ && !endpoint_offline_ &&
            !endpoint_recovery_pending_.load(std::memory_order_acquire);
    }

    AudioSinkStats stats() const {
        if (endpoint_recovery_task_.active()) {
            auto snapshot = endpoint_recovery_snapshot_;
            snapshot.dropped_buffers = dropped_buffers();
            return snapshot;
        }
        AudioSinkStats stats{};
        const NativeAudioFrequencyRatioLedgerSnapshot frequency_ratio =
            callback_ledger_.frequency_ratio_snapshot();
        const std::uint64_t engine_glitches =
            query_engine_glitches_since_start();
        const NativeAudioInitializationSnapshot initialization =
            initialization_gate_.snapshot();
        stats.initialization_attempts =
            initialization.initialization_attempts;
        stats.preinitialize_calls = initialization.preinitialize_calls;
        stats.submit_initialization_checks = initialization.submit_checks;
        stats.submit_initialization_attempts =
            initialization.submit_initialization_attempts;
        stats.preinitialize_elapsed_us =
            preinitialize_elapsed_us_.load(std::memory_order_acquire);
        stats.first_submit_initialization_wait_us =
            first_submit_initialization_wait_us_.load(
                std::memory_order_acquire);
        stats.initialization_thread_id =
            initialization_thread_id_.load(std::memory_order_acquire);
        stats.first_submit_thread_id =
            first_submit_thread_id_.load(std::memory_order_acquire);
        stats.initialization_ready =
            initialization.state == NativeAudioInitializationState::Ready;
        stats.preinitialized_before_guest = initialization.preinitialized;
        stats.first_submit_observed =
            first_submit_observed_.load(std::memory_order_acquire);
        stats.first_submit_initialized_backend =
            first_submit_initialized_backend_.load(
                std::memory_order_acquire);
        stats.submitted_buffers =
            submitted_buffers_.load(std::memory_order_acquire);
        stats.completed_buffers =
            callback_ledger_.completed_buffers();
        stats.queue_empty_events =
            queue_empty_events_.load(std::memory_order_acquire);
        stats.dropped_buffers = dropped_buffers();
        stats.queue_wait_events =
            queue_wait_events_.load(std::memory_order_acquire);
        stats.total_queue_wait_ms =
            total_queue_wait_ms_.load(std::memory_order_acquire);
        stats.submit_delta_events =
            submit_delta_events_.load(std::memory_order_acquire);
        stats.total_submit_delta_us =
            total_submit_delta_us_.load(std::memory_order_acquire);
        stats.max_submit_delta_us =
            max_submit_delta_us_.load(std::memory_order_acquire);
        stats.submit_delta_over_expected =
            submit_delta_over_expected_.load(std::memory_order_acquire);
        stats.submit_delta_over_25_ms =
            submit_delta_over_25_ms_.load(std::memory_order_acquire);
        stats.submit_failure_events =
            submit_failure_events_.load(std::memory_order_acquire);
        stats.source_start_failure_events =
            source_start_failure_events_.load(std::memory_order_acquire);
        stats.voice_error_events = callback_ledger_.voice_errors();
        stats.engine_critical_error_events =
            callback_ledger_.engine_critical_errors();
        stats.callback_wake_error_events =
            callback_ledger_.callback_wake_errors();
        stats.frequency_ratio_snapshot_coherent = frequency_ratio.coherent;
        stats.frequency_ratio_apply_calls =
            frequency_ratio.apply_calls;
        stats.frequency_ratio_change_actions =
            frequency_ratio.change_actions;
        stats.frequency_ratio_apply_error_events =
            frequency_ratio.apply_errors;
        stats.source_voice_creation_events =
            source_voice_creation_events_.load(std::memory_order_acquire);
        stats.voice_recreation_events =
            voice_recreation_events_.load(std::memory_order_acquire);
        // The single-voice implementation has no rate-drain operation. Any source-voice recreation is
        // conservatively reported as one, so proof tooling catches a regression to that design.
        stats.rate_drain_events = stats.voice_recreation_events;
        stats.engine_glitches_since_start = engine_glitches;
        stats.pending_overflow_events =
            pending_overflow_events_.load(std::memory_order_acquire);
        stats.empty_recovery_events =
            empty_recovery_events_.load(std::memory_order_acquire);
        stats.total_empty_us =
            total_empty_us_.load(std::memory_order_acquire);
        stats.empty_over_1_ms =
            empty_over_1_ms_.load(std::memory_order_acquire);
        stats.empty_over_5_ms =
            empty_over_5_ms_.load(std::memory_order_acquire);
        stats.queued_buffers = queue_mirror_.queued_buffers();
        // Kept in the diagnostics ABI for compatibility. There is no synthetic counter; both values are
        // the last worker-owned XAudio GetState observation.
        stats.synthetic_queued_buffers = stats.queued_buffers;
        stats.pending_buffers =
            pending_queue_depth_.load(std::memory_order_acquire);
        stats.max_queued_buffers = queue_mirror_.max_queued_buffers();
        stats.max_synthetic_queued_buffers = stats.max_queued_buffers;
        stats.max_pending_buffers =
            max_pending_queue_depth_.load(std::memory_order_acquire);
        stats.max_queue_depth_delta = 0;
        stats.max_queue_wait_ms =
            max_queue_wait_ms_.load(std::memory_order_acquire);
        stats.max_empty_us =
            max_empty_us_.load(std::memory_order_acquire);
        stats.queue_limit = queue_limit_;
        stats.prebuffer_buffers = prebuffer_buffers_;
        // Takes pending ownership before the generation ledger, matching the enqueue path's lock order.
        // This gives an exact current-generation FIFO count; accepted-minus-submitted can differ from it
        // only by the worker's one dequeued local buffer.
        {
            std::lock_guard<std::mutex> pending_lock(pending_mutex_);
            std::lock_guard<std::mutex> measurement_lock(
                measurement_window_mutex_);
            stats.measurement_window_accepted_buffers =
                measurement_window_accepted_buffers_;
            const NativeAudioRateLedgerSnapshot accepted_rate_ledger =
                measurement_window_accepted_rate_ledger_.snapshot();
            stats.measurement_window_accepted_buffers_32000 =
                accepted_rate_ledger.submitted_buffers_32000;
            stats.measurement_window_accepted_stereo_frames_32000 =
                accepted_rate_ledger.submitted_stereo_frames_32000;
            stats.measurement_window_accepted_buffers_48000 =
                accepted_rate_ledger.submitted_buffers_48000;
            stats.measurement_window_accepted_stereo_frames_48000 =
                accepted_rate_ledger.submitted_stereo_frames_48000;
            stats.measurement_window_accepted_sample_rate_change_events =
                accepted_rate_ledger.sample_rate_change_events;
            const NativeAudioSubmissionWindowSnapshot submission_window =
                measurement_window_submission_ledger_.snapshot();
            stats.measurement_window_submitted_buffers =
                submission_window.submitted_buffers;
            const NativeAudioRateLedgerSnapshot& rate_ledger =
                submission_window.rate_ledger;
            stats.measurement_window_submitted_buffers_32000 =
                rate_ledger.submitted_buffers_32000;
            stats.measurement_window_submitted_stereo_frames_32000 =
                rate_ledger.submitted_stereo_frames_32000;
            stats.measurement_window_submitted_buffers_48000 =
                rate_ledger.submitted_buffers_48000;
            stats.measurement_window_submitted_stereo_frames_48000 =
                rate_ledger.submitted_stereo_frames_48000;
            stats.measurement_window_sample_rate_change_events =
                rate_ledger.sample_rate_change_events;
            const NativeAudioStartedWindowSnapshot started_window =
                measurement_window_started_ledger_.snapshot();
            stats.measurement_window_started_buffers =
                started_window.started_buffers;
            const NativeAudioRateLedgerSnapshot& started_rate_ledger =
                started_window.rate_ledger;
            stats.measurement_window_started_buffers_32000 =
                started_rate_ledger.submitted_buffers_32000;
            stats.measurement_window_started_stereo_frames_32000 =
                started_rate_ledger.submitted_stereo_frames_32000;
            stats.measurement_window_started_buffers_48000 =
                started_rate_ledger.submitted_buffers_48000;
            stats.measurement_window_started_stereo_frames_48000 =
                started_rate_ledger.submitted_stereo_frames_48000;
            stats.measurement_window_started_sample_rate_change_events =
                started_rate_ledger.sample_rate_change_events;
            stats.measurement_window_frequency_ratio_apply_calls =
                started_window.frequency_ratio_apply_calls;
            stats.measurement_window_frequency_ratio_inherited_buffers =
                started_window.frequency_ratio_inherited_buffers;
            stats.measurement_window_frequency_ratio_change_actions =
                started_window.frequency_ratio_change_actions;
            stats.measurement_window_frequency_ratio_apply_errors =
                started_window.frequency_ratio_apply_errors;
            stats.measurement_window_buffer_start_callback_core_work_count =
                started_window.buffer_start_callback_core_work_count;
            stats.measurement_window_buffer_start_callback_core_work_total_ns =
                started_window.buffer_start_callback_core_work_total_ns;
            stats.measurement_window_buffer_start_callback_core_work_max_ns =
                started_window.buffer_start_callback_core_work_max_ns;
            stats.measurement_window_buffer_start_callback_core_work_over_1_ms =
                started_window.buffer_start_callback_core_work_over_1_ms;
            stats.measurement_window_buffer_start_callback_core_work_over_2_ms =
                started_window.buffer_start_callback_core_work_over_2_ms;
            if (measurement_window_active_) {
                const auto counter_delta = [](
                    std::uint64_t current,
                    std::uint64_t baseline) noexcept {
                    return current >= baseline
                        ? current - baseline
                        : std::numeric_limits<std::uint64_t>::max();
                };
                stats.measurement_window_voice_recreation_events =
                    counter_delta(
                        voice_recreation_events_.load(
                            std::memory_order_acquire),
                        measurement_window_voice_recreation_start_);
                stats.measurement_window_rate_drain_events =
                    stats.measurement_window_voice_recreation_events;
                stats.measurement_window_empty_buffer_end_boundaries =
                    counter_delta(
                        callback_ledger_.empty_buffer_end_boundaries(),
                        measurement_window_empty_boundary_start_);
                stats.measurement_window_unobserved_buffer_end_boundaries =
                    counter_delta(
                        callback_ledger_.unobserved_buffer_end_boundaries(),
                        measurement_window_unobserved_boundary_start_);
                for (const auto& pending : pending_buffers_) {
                    if (pending != nullptr &&
                        pending->measurement_generation ==
                            measurement_window_generation_) {
                        if (stats.measurement_window_pending_buffers ==
                            std::numeric_limits<std::uint32_t>::max()) {
                            stats.measurement_window_pending_buffers =
                                std::numeric_limits<std::uint32_t>::max();
                            break;
                        }
                        ++stats.measurement_window_pending_buffers;
                    }
                }
            }
            stats.measurement_window_submit_delta_events =
                submission_window.submit_delta_events;
            stats.measurement_window_total_submit_delta_us =
                submission_window.total_submit_delta_us;
            stats.measurement_window_max_submit_delta_us =
                submission_window.max_submit_delta_us;
            stats.measurement_window_submit_delta_over_expected =
                submission_window.submit_delta_over_expected;
            stats.measurement_window_submit_delta_over_25_ms =
                submission_window.submit_delta_over_25_ms;
            stats.measurement_window_engine_glitches =
                engine_glitches >= measurement_window_engine_glitch_start_
                    ? engine_glitches -
                          measurement_window_engine_glitch_start_
                    : std::numeric_limits<std::uint64_t>::max();
        }
        return stats;
    }

#if defined(GALAXY_NATIVE_HOST_TEST_SUPPORT)
    bool test_recovery_ownership() {
        if (!preinitialize() || !com_initialized_) return false;
        std::atomic_bool entered{false}, release{false};
        // Always unblock on early exit, before this sink's destructor joins.
        const ScopeExit unblock([&]() noexcept {
            release.store(true, std::memory_order_release);
            release.notify_one();
            try { endpoint_recovery_task_.finish(); } catch (...) {}
        });
        recovery_gate_for_test_ = [&] {
            entered.store(true, std::memory_order_release);
            entered.notify_one();
            release.wait(false, std::memory_order_acquire);
        };
        const auto before = stats();
        endpoint_recovery_pending_.store(true, std::memory_order_release);
        poll_health();
        entered.wait(false, std::memory_order_acquire);
        bool ok = endpoint_recovery_task_.active() && !output_available();
        const std::array<std::byte, 16> pcm{};
        const auto start = std::chrono::steady_clock::now();
        for (unsigned i = 0; i < 1000; ++i) {
            poll_health();
            reset_measurement_window();
            ok &= submit_pcm16_be_stereo(pcm, 32000u, 0u, 0u);
            ok &= !output_available() && current_measurement_window_generation() == 0u;
            ok &= stats().submitted_buffers == before.submitted_buffers;
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start).count();
        ok &= dropped_buffers() == before.dropped_buffers + 1000u;
        std::cout << "[audio-recovery-test] blocked-owner-polls=1000 elapsed-us="
                  << elapsed << " unplayed-buffers=1000\n";
        release.store(true, std::memory_order_release);
        release.notify_one();
        endpoint_recovery_task_.finish();
        poll_health();
        ok &= !strict_failure() &&
            initialization_thread_id_.load(std::memory_order_acquire) == GetCurrentThreadId();
        recovery_gate_for_test_ = {};
        return ok;
    }

    bool test_recovery_backpressure(unsigned test_case) {
        // No device is needed: exercises the producer wait and worker failure handoff with one retained
        // buffer and an immediate timeout.
        pending_queue_limit_ = 1u;
        pending_queue_timeout_ms_ = 0u;
        auto retained = std::make_unique<SubmittedBuffer>();
        const auto* retained_address = retained.get();
        pending_buffers_.push_back(std::move(retained));
        pending_queue_depth_.store(1u, std::memory_order_release);
        auto incoming = std::make_unique<SubmittedBuffer>();
        incoming->sample_rate = 32'000u;
        incoming->bytes = {std::byte{0x34}, std::byte{0x12},
                           std::byte{0x78}, std::byte{0x56}};
        std::atomic_bool entered_wait{false}, worker_finished{false};
        bool worker_requested_recovery = false;
        unsigned waits = 0u;
        std::thread loss_worker;
        const ScopeExit finish_worker([&]() noexcept {
            entered_wait.store(true, std::memory_order_release);
            entered_wait.notify_one();
            if (loss_worker.joinable()) loss_worker.join();
            pending_wait_for_test_ = {};
        });
        if (test_case == 0u) {
            loss_worker = std::thread([&] {
                entered_wait.wait(false, std::memory_order_acquire);
                callback_ledger_.record_engine_critical_error(
                    static_cast<std::int32_t>(HRESULT_FROM_WIN32(ERROR_NOT_FOUND)));
                worker_requested_recovery = !process_callback_failures();
                if (endpoint_recovery_pending_.load(std::memory_order_acquire)) {
                    clear_pending_after_worker_failure();
                }
                worker_finished.store(true, std::memory_order_release);
                worker_finished.notify_one();
            });
            pending_wait_for_test_ = [&] {
                ++waits;
                entered_wait.store(true, std::memory_order_release);
                entered_wait.notify_one();
                worker_finished.wait(false, std::memory_order_acquire);
            };
        } else if (test_case == 1u || test_case == 3u) {
            endpoint_recovery_pending_.store(true, std::memory_order_release);
            if (test_case == 3u) {
                callback_ledger_.record_engine_critical_error(
                    static_cast<std::int32_t>(E_FAIL));
                strict_failure_.store(true, std::memory_order_release);
                set_strict_failure_reason("injected unrelated fatal audio failure");
            }
        }
        const bool submitted = enqueue_pending_buffer(std::move(incoming));
        if (loss_worker.joinable()) loss_worker.join();
        const bool recoverable = test_case < 2u;
        const auto health = stats();
        const bool preserved = pending_buffers_.size() == 1u &&
            pending_buffers_.front().get() == retained_address &&
            health.pending_buffers == 1u && health.submitted_buffers == 0u;
        const bool recovered = submitted == recoverable &&
            strict_failure() != recoverable &&
            health.dropped_buffers == (recoverable ? 1u : 0u) &&
            health.pending_overflow_events == (test_case == 2u ? 1u : 0u) &&
            health.submit_failure_events == (test_case == 2u ? 1u : 0u);
        const bool interleaving = test_case != 0u ||
            (waits == 1u && worker_requested_recovery &&
             endpoint_recovery_pending_.load(std::memory_order_acquire));
        std::cout << "[audio-backpressure-test] case=" << test_case
                  << " preserved=" << preserved << " recovered=" << recovered
                  << " interleaving=" << interleaving << '\n';
        return preserved && recovered && interleaving;
    }
#endif

private:
    // The Wii Remote speaker joins the game's output before the master volume, which covers it too.
    void finish_output(std::vector<std::byte>& pcm_le, std::uint32_t sample_rate) noexcept {
        const galaxy::RuntimeAudioSettings audio = galaxy::get_runtime_settings().audio;
        if (wiimote_speaker_ != nullptr) {
            wiimote_speaker_->mix(pcm_le, sample_rate, audio.wiimote_speaker);
        }
        apply_output_master_gain(pcm_le, audio.master);
    }

    // Master volume, applied once to the final output so it also covers audio mixed outside the DSP
    // channels (the movie player). A change ramps across one buffer from the previous value. One
    // submitting thread.
    void apply_output_master_gain(std::vector<std::byte>& pcm_le, float master) noexcept {
        constexpr std::uint32_t kUnity = galaxy::audio::kNativeAudioUnityGainQ16;
        const std::uint32_t target = galaxy::audio::native_audio_gain_to_q16(master);
        // The first buffer starts at the configured gain; later changes ramp.
        const std::uint32_t start =
            output_master_started_ ? output_master_q16_ : target;
        output_master_started_ = true;
        output_master_q16_ = target;
        const std::size_t frames = pcm_le.size() / 4u;
        if ((start == kUnity && target == kUnity) || frames == 0u) {
            return;
        }
        const auto scale = [](std::byte* sample, std::int64_t gain) noexcept {
            const auto bits = static_cast<std::uint16_t>(
                std::to_integer<std::uint16_t>(sample[0]) |
                (std::to_integer<std::uint16_t>(sample[1]) << 8u));
            const std::int64_t scaled = std::clamp<std::int64_t>(
                (static_cast<std::int64_t>(static_cast<std::int16_t>(bits)) * gain) >> 16u,
                -32768, 32767);
            const auto out = static_cast<std::uint16_t>(static_cast<std::int16_t>(scaled));
            sample[0] = static_cast<std::byte>(out & 0xFFu);
            sample[1] = static_cast<std::byte>(out >> 8u);
        };
        if (start == target) {
            // Steady non-unity master: no per-frame ramp division.
            const std::int64_t gain = target;
            for (std::size_t offset = 0; offset < frames * 4u; offset += 2u) {
                scale(pcm_le.data() + offset, gain);
            }
            return;
        }
        const std::int64_t delta =
            static_cast<std::int64_t>(target) - static_cast<std::int64_t>(start);
        for (std::size_t frame = 0; frame < frames; ++frame) {
            const std::int64_t gain = static_cast<std::int64_t>(start) +
                delta * static_cast<std::int64_t>(frame + 1u) /
                    static_cast<std::int64_t>(frames);
            scale(pcm_le.data() + frame * 4u, gain);
            scale(pcm_le.data() + frame * 4u + 2u, gain);
        }
    }
    std::uint32_t output_master_q16_ = galaxy::audio::kNativeAudioUnityGainQ16;
    bool output_master_started_ = false;
    galaxy::input::speaker::Speaker* wiimote_speaker_ = nullptr;

    struct SubmittedBuffer {
        std::atomic_bool started{false};
        std::atomic_bool completed{false};
        bool started_accounted{};
        bool ratio_apply_required{};
        bool ratio_apply_invoked{};
        bool ratio_apply_succeeded{};
        bool ratio_inherited{};
        bool ratio_changed{};
        float requested_ratio{};
        float active_ratio{};
        std::uint64_t buffer_start_callback_core_work_ns{};
        std::uint32_t sample_rate{};
        std::uint64_t measurement_generation{};
        std::uint64_t anomaly_sequence{};
        std::uint64_t accepted_ns{};
        std::uint64_t dequeued_ns{};
        std::uint64_t submit_begin_ns{};
        std::uint64_t guest_start_ticks{};
        std::uint32_t source_address{};
        std::vector<std::byte> bytes;
    };

    static std::uint64_t host_time_ns(
        std::chrono::steady_clock::time_point time) noexcept {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                time.time_since_epoch()).count());
    }

    static NativeAudioAnomalyRecord buffer_anomaly_record(
        NativeAudioAnomalyKind kind, const SubmittedBuffer& buffer,
        std::uint64_t host_ns) noexcept {
        NativeAudioAnomalyRecord record{};
        record.kind = kind;
        record.host_ns = host_ns;
        record.buffer_sequence = buffer.anomaly_sequence;
        record.accepted_ns = buffer.accepted_ns;
        record.dequeued_ns = buffer.dequeued_ns;
        record.submit_begin_ns = buffer.submit_begin_ns;
        record.guest_ticks = buffer.guest_start_ticks;
        record.address = buffer.source_address;
        record.bytes = static_cast<std::uint32_t>(buffer.bytes.size());
        record.sample_rate = buffer.sample_rate;
        return record;
    }

    void dump_anomaly_ledger() const {
        std::ostringstream output;
        const auto anchor_before_ns = host_time_ns(std::chrono::steady_clock::now());
        const auto anchor_unix_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        const auto anchor_after_ns = host_time_ns(std::chrono::steady_clock::now());
        const auto attempted = anomaly_ledger_.attempted();
        std::uint64_t published = 0u;
        for (std::size_t i = 0; i < anomaly_ledger_.capacity(); ++i) {
            NativeAudioAnomalyRecord record{};
            if (!anomaly_ledger_.read(i, record)) {
                continue;
            }
            ++published;
            static constexpr std::array<const char*, 8> names = {
                "ai-state", "accepted", "submitted", "playback-started",
                "queue-empty", "empty-recovered", "voice-start-request", "host-pause"};
            output << "[audio-anomaly] schema=1 slot=" << i
                   << " kind=" << names[static_cast<std::size_t>(record.kind)]
                   << " host-ns=" << record.host_ns
                   << " buffer=" << record.buffer_sequence
                   << " accept-ns=" << record.accepted_ns
                   << " dequeue-ns=" << record.dequeued_ns
                   << " submit-begin-ns=" << record.submit_begin_ns
                   << " previous-submit-ns=" << record.previous_submit_ns
                   << " empty-since-ns=" << record.empty_since_ns
                   << " guest-ticks=" << record.guest_ticks
                   << " duration-ticks=" << record.duration_ticks
                   << " device-deadline-ticks=" << record.deadline_ticks
                   << " address=" << record.address << " bytes=" << record.bytes
                   << " rate=" << record.sample_rate << " blocks=" << record.blocks
                   << " queued=" << record.queued << " pending=" << record.pending
                   << " completed=" << record.completed << " reason=" << record.reason
                   << " vi=" << record.vi << " proof=" << record.proof
                   << " boundary-phase=" << record.boundary_phase
                   << " last-guest-ticks=" << record.last_guest_ticks
                   << " removed-ticks=" << record.removed_ticks
                   << " prepare-ticks=" << record.prepare_ticks
                   << " qpc-begin=" << record.qpc_begin << " qpc-end=" << record.qpc_end
                   << " qpc-hz=" << record.qpc_frequency
                   << " cpu-begin-100ns=" << record.cpu_begin_100ns
                   << " cpu-end-100ns=" << record.cpu_end_100ns << '\n';
        }
        output << "[audio-anomaly-summary] schema=1 capacity="
               << anomaly_ledger_.capacity() << " attempted=" << attempted
               << " published=" << published
               << " overflow=" << (attempted > anomaly_ledger_.capacity()
                       ? attempted - anomaly_ledger_.capacity() : 0u)
               << " incomplete=" << (std::min<std::uint64_t>(
                       attempted, anomaly_ledger_.capacity()) - published)
               << " anchor-steady-before-ns=" << anchor_before_ns
               << " anchor-unix-ns=" << anchor_unix_ns
               << " anchor-steady-after-ns=" << anchor_after_ns
               << " clock=steady-ns qpc=raw-counter cpu=100ns"
                  " missing-fields=zero dump-after-shutdown=1"
                  " ai-reason=1-stop,2-latch-shape-change,3-zero-block"
                  " empty-reason=1-callback-boundary,2-worker-callback,3-pre-submit"
                  " voice-reason=0-prebuffer,1-recovery"
                  " pause-qpc-scope=raw-before-cpu-sample,zero-after-unpaired-reset"
                  " pause-reason=ai-playing pause-proof=0-inactive,1-ready-ai,2-active-backlog\n";
        std::cout << output.str();
    }

    std::uint64_t query_engine_glitches_since_start() const noexcept {
        if (endpoint_recovery_task_.active()) {
            return endpoint_recovery_snapshot_.engine_glitches_since_start;
        }
        return query_backend_engine_glitches();
    }

    std::uint64_t query_backend_engine_glitches() const noexcept {
        if (engine_ == nullptr) {
            return engine_glitches_before_restart_;
        }
        XAUDIO2_PERFORMANCE_DATA performance{};
        engine_->GetPerformanceData(&performance);
        return engine_glitches_before_restart_ + performance.GlitchesSinceEngineStarted;
    }

    std::uint64_t current_measurement_window_generation() const {
        std::lock_guard<std::mutex> lock(measurement_window_mutex_);
        return measurement_window_active_ ? measurement_window_generation_ : 0u;
    }

    [[nodiscard]] bool record_measurement_window_acceptance(
        std::uint64_t generation,
        std::uint32_t sample_rate,
        std::uint64_t pcm_bytes) {
        std::lock_guard<std::mutex> lock(measurement_window_mutex_);
        if (measurement_window_active_ && generation != 0u &&
            generation == measurement_window_generation_) {
            if (measurement_window_accepted_buffers_ ==
                    std::numeric_limits<std::uint64_t>::max() ||
                !measurement_window_accepted_rate_ledger_.record_submission(
                    sample_rate, pcm_bytes)) {
                return false;
            }
            ++measurement_window_accepted_buffers_;
        }
        return true;
    }

    [[nodiscard]] bool record_measurement_window_submission(
        std::uint64_t generation,
        std::uint32_t sample_rate,
        std::uint64_t pcm_bytes,
        std::chrono::steady_clock::time_point previous_submit_time,
        std::chrono::steady_clock::time_point submit_time,
        std::uint64_t delta_us,
        std::uint64_t expected_us,
        std::uint64_t previous_buffer_generation,
        bool has_previous_submit) {
        std::lock_guard<std::mutex> lock(measurement_window_mutex_);
        if (measurement_window_active_ && generation != 0u &&
            generation == measurement_window_generation_) {
            const bool has_window_predecessor =
                has_previous_submit &&
                native_audio_submit_delta_in_window(
                    previous_buffer_generation,
                    generation,
                    measurement_window_generation_) &&
                previous_submit_time >= measurement_window_started_at_ &&
                submit_time >= measurement_window_started_at_;
            return measurement_window_submission_ledger_.record_submission(
                sample_rate,
                pcm_bytes,
                has_window_predecessor,
                delta_us,
                expected_us);
        }
        return true;
    }

    [[nodiscard]] bool record_measurement_window_start(
        std::uint64_t generation,
        std::uint32_t sample_rate,
        std::uint64_t pcm_bytes,
        bool ratio_apply_required,
        bool ratio_apply_invoked,
        float requested_ratio,
        float active_ratio,
        bool ratio_apply_succeeded,
        bool ratio_inherited,
        bool ratio_changed,
        std::uint64_t callback_core_work_ns) {
        std::lock_guard<std::mutex> lock(measurement_window_mutex_);
        if (measurement_window_active_ && generation != 0u &&
            generation == measurement_window_generation_) {
            const NativeAudioSubmissionWindowSnapshot submitted_window =
                measurement_window_submission_ledger_.snapshot();
            const NativeAudioStartedWindowSnapshot started_before =
                measurement_window_started_ledger_.snapshot();
            if (started_before.started_buffers >=
                    submitted_window.submitted_buffers ||
                !measurement_window_started_ledger_.record_start(
                    sample_rate,
                    pcm_bytes,
                    ratio_apply_required,
                    ratio_apply_invoked,
                    requested_ratio,
                    active_ratio,
                    ratio_apply_succeeded,
                    ratio_inherited,
                    ratio_changed,
                    callback_core_work_ns)) {
                return false;
            }
            const NativeAudioRateLedgerSnapshot& submitted =
                submitted_window.rate_ledger;
            const NativeAudioStartedWindowSnapshot started_window =
                measurement_window_started_ledger_.snapshot();
            const NativeAudioRateLedgerSnapshot& started =
                started_window.rate_ledger;
            if (started.submitted_buffers_32000 >
                    submitted.submitted_buffers_32000 ||
                started.submitted_stereo_frames_32000 >
                    submitted.submitted_stereo_frames_32000 ||
                started.submitted_buffers_48000 >
                    submitted.submitted_buffers_48000 ||
                started.submitted_stereo_frames_48000 >
                    submitted.submitted_stereo_frames_48000) {
                return false;
            }
        }
        return true;
    }

    bool enqueue_pending_buffer(std::unique_ptr<SubmittedBuffer> buffer) {
        if (buffer == nullptr) {
            return true;
        }
        if (strict_failure_.load(std::memory_order_acquire)) {
            return false;
        }

        const std::uint64_t measurement_generation =
            buffer->measurement_generation;
        const std::uint32_t sample_rate = buffer->sample_rate;
        const std::uint64_t pcm_bytes = buffer->bytes.size();
        std::uint32_t pending_depth = 0;
        bool acceptance_ledger_failed = false;
        {
            std::unique_lock<std::mutex> lock(pending_mutex_);
            // Device loss can race submit_pcm16_be_stereo's offline check. The worker leaves pending PCM owned
            // by recovery and stops draining it; the control owner must return before it can poll health and
            // launch that recovery, including under backpressure.
            if (endpoint_recovery_pending_.load(std::memory_order_acquire)) {
                lock.unlock();
                return preserve_unheard_buffer(*buffer);
            }
            if (worker_stop_.load(std::memory_order_acquire)) {
                submit_failure_events_.fetch_add(
                    1, std::memory_order_acq_rel);
                strict_failure_.store(true, std::memory_order_release);
                set_strict_failure_reason(
                    "native audio worker stopped before submit");
                return false;
            }
            const auto pending_wait_started =
                std::chrono::steady_clock::now();
            while (pending_buffers_.size() >= pending_queue_limit_) {
                if (!pending_overflow_logged_) {
                    pending_overflow_logged_ = true;
                    std::cout << "[audio] native pending queue full"
                              << " pending=" << pending_buffers_.size()
                              << " limit=" << pending_queue_limit_
                              << "; applying backpressure without dropping AI buffer\n";
                }
#if defined(GALAXY_NATIVE_HOST_TEST_SUPPORT)
                if (pending_wait_for_test_) pending_wait_for_test_();
#endif
                pending_cv_.wait_for(lock, std::chrono::milliseconds(1));
                if (strict_failure_.load(std::memory_order_acquire)) {
                    return false;
                }
                if (endpoint_recovery_pending_.load(std::memory_order_acquire)) {
                    lock.unlock();
                    return preserve_unheard_buffer(*buffer);
                }
                if (worker_stop_.load(std::memory_order_acquire)) {
                    submit_failure_events_.fetch_add(
                        1, std::memory_order_acq_rel);
                    strict_failure_.store(true, std::memory_order_release);
                    set_strict_failure_reason(
                        "native audio worker stopped before submit");
                    return false;
                }
                const auto pending_wait_ms =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() -
                        pending_wait_started)
                        .count();
                if (pending_wait_ms >=
                    static_cast<std::int64_t>(pending_queue_timeout_ms_)) {
                    pending_overflow_events_.fetch_add(
                        1, std::memory_order_acq_rel);
                    submit_failure_events_.fetch_add(
                        1, std::memory_order_acq_rel);
                    strict_failure_.store(true, std::memory_order_release);
                    set_strict_failure_reason(
                        "native audio pending queue remained full");
                    return false;
                }
            }
            if (endpoint_recovery_pending_.load(std::memory_order_acquire)) {
                lock.unlock();
                return preserve_unheard_buffer(*buffer);
            }
            buffer->anomaly_sequence = ++accepted_sequence_;
            buffer->accepted_ns = host_time_ns(std::chrono::steady_clock::now());
            pending_buffers_.push_back(std::move(buffer));
            if (!record_measurement_window_acceptance(
                    measurement_generation, sample_rate, pcm_bytes)) {
                pending_buffers_.pop_back();
                acceptance_ledger_failed = true;
            } else if (pending_buffers_.back()->anomaly_sequence <= 3u) {
                const auto& accepted = *pending_buffers_.back();
                auto record = buffer_anomaly_record(
                    NativeAudioAnomalyKind::Accepted, accepted, accepted.accepted_ns);
                record.pending = static_cast<std::uint32_t>(pending_buffers_.size());
                (void)anomaly_ledger_.record(record);
            }
            pending_depth =
                static_cast<std::uint32_t>(pending_buffers_.size());
            pending_queue_depth_.store(
                pending_depth, std::memory_order_release);
        }
        if (acceptance_ledger_failed) {
            submit_failure_events_.fetch_add(1u, std::memory_order_acq_rel);
            strict_failure_.store(true, std::memory_order_release);
            set_strict_failure_reason(
                "native audio accepted-rate ledger overflow");
            return false;
        }
        update_max_atomic(max_pending_queue_depth_, pending_depth);
        if (buffer_end_event_ == nullptr || !SetEvent(buffer_end_event_)) {
            submit_failure_events_.fetch_add(1u, std::memory_order_acq_rel);
            strict_failure_.store(true, std::memory_order_release);
            set_strict_failure_reason(
                "native audio worker submit wake failed");
            return false;
        }
        return true;
    }

    bool preserve_unheard_buffer(const SubmittedBuffer& buffer) {
        // This PCM never entered the pending FIFO or XAudio. Older queued buffers stay owned by recovery
        // teardown and are counted there.
        if (strict_failure_.load(std::memory_order_acquire)) return false;
        endpoint_unheard_buffers_.fetch_add(1u, std::memory_order_relaxed);
        if (get_env_path(L"GALAXY_AUDIO_WAV_DUMP").has_value() &&
            !wav_dump_.append(buffer.bytes, buffer.sample_rate)) {
            log_drop("offline audio WAV diagnostic failed");
            return false;
        }
        return true;
    }

    bool initialize_backend(bool control_owner = true) {
        if (disabled_) {
            return false;
        }
        if (initialized_) {
            return true;
        }
        if (control_owner) {
            initialization_thread_id_.store(
                GetCurrentThreadId(), std::memory_order_release);
        }

        if (read_env_flag("GALAXY_AUDIO_DISABLE")) {
            disabled_ = true;
            std::cout << "[audio] native audio disabled by GALAXY_AUDIO_DISABLE\n";
            return false;
        }

        const HRESULT com_hr = control_owner && !com_initialized_
            ? CoInitializeEx(nullptr, COINIT_MULTITHREADED) : S_FALSE;
        if (SUCCEEDED(com_hr) && control_owner) {
            com_initialized_ = true;
        } else if (FAILED(com_hr) && com_hr != RPC_E_CHANGED_MODE) {
            strict_failure_.store(true, std::memory_order_release);
            set_strict_failure_reason("native audio COM initialization failed");
            std::cerr << "[audio] CoInitializeEx failed hr=0x" << std::hex
                      << static_cast<unsigned long>(com_hr) << std::dec
                      << '\n';
            return false;
        }

        queue_limit_ = read_env_u32(
            "GALAXY_AUDIO_QUEUE_LIMIT",
            16,
            2,
            XAUDIO2_MAX_QUEUED_BUFFERS);
        queue_wait_ms_ = read_env_u32(
            "GALAXY_AUDIO_QUEUE_WAIT_MS",
            100,
            1,
            1000);
        queue_stuck_recovery_ms_ = read_env_u32(
            "GALAXY_AUDIO_QUEUE_STUCK_RECOVERY_MS",
            750,
            queue_wait_ms_,
            30000);
        pending_queue_limit_ = read_env_u32(
            "GALAXY_AUDIO_PENDING_QUEUE_LIMIT",
            256,
            queue_limit_,
            4096);
        pending_queue_timeout_ms_ = read_env_u32(
            "GALAXY_AUDIO_PENDING_QUEUE_TIMEOUT_MS",
            250,
            10,
            30000);
        if (buffer_end_event_ == nullptr) {
            buffer_end_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (buffer_end_event_ == nullptr) {
                strict_failure_.store(true, std::memory_order_release);
                set_strict_failure_reason(
                    "native audio worker event creation failed");
                std::cerr << "[audio] CreateEventW for queue wake failed error="
                          << GetLastError() << '\n';
                shutdown_backend(false);
                return false;
            }
        }

        endpoint_unavailable_ = false;
        HRESULT hr = XAudio2Create(&engine_, 0, XAUDIO2_DEFAULT_PROCESSOR);
        if (FAILED(hr)) {
            strict_failure_.store(true, std::memory_order_release);
            set_strict_failure_reason("XAudio2 engine creation failed");
            std::cerr << "[audio] XAudio2Create failed hr=0x" << std::hex
                      << static_cast<unsigned long>(hr) << std::dec << '\n';
            shutdown_backend(false);
            return false;
        }
        hr = engine_->RegisterForCallbacks(this);
        if (FAILED(hr)) {
            strict_failure_.store(true, std::memory_order_release);
            set_strict_failure_reason(
                "XAudio2 engine callback registration failed");
            std::cerr << "[audio] RegisterForCallbacks failed hr=0x"
                      << std::hex << static_cast<unsigned long>(hr)
                      << std::dec << '\n';
            shutdown_backend(false);
            return false;
        }
        engine_callback_registered_ = true;

        // Keeps the endpoint-discovery evidence until initialization succeeds or takes the existing
        // hard-fail path. Endpoint availability is scoped to the Windows session, so device-manager state
        // alone cannot explain a missing Core Audio render endpoint.
        struct MasteringVoiceEndpointProbe {
            HRESULT default_create_result{E_FAIL};
            HRESULT enumerator_create_result{E_FAIL};
            HRESULT enumerate_active_result{E_FAIL};
            HRESULT active_count_result{E_FAIL};
            std::optional<HRESULT> last_active_endpoint_result{};
            UINT active_render_endpoint_count{};
        } endpoint_probe{};

        auto try_create_mastering_voice = [&]() -> HRESULT {
            HRESULT create_hr = engine_->CreateMasteringVoice(&mastering_voice_);
            endpoint_probe.default_create_result = create_hr;
            if (SUCCEEDED(create_hr)) {
                return create_hr;
            }
            if (create_hr != HRESULT_FROM_WIN32(ERROR_NOT_FOUND)) {
                return create_hr;
            }

            ComPtr<IMMDeviceEnumerator> enumerator;
            HRESULT enum_hr = CoCreateInstance(
                __uuidof(MMDeviceEnumerator),
                nullptr,
                CLSCTX_ALL,
                IID_PPV_ARGS(&enumerator));
            endpoint_probe.enumerator_create_result = enum_hr;
            if (FAILED(enum_hr)) {
                std::cerr << "[audio] MMDeviceEnumerator failed hr=0x"
                          << std::hex << static_cast<unsigned long>(enum_hr)
                          << std::dec << '\n';
                return create_hr;
            }

            ComPtr<IMMDeviceCollection> endpoints;
            enum_hr = enumerator->EnumAudioEndpoints(
                eRender,
                DEVICE_STATE_ACTIVE,
                &endpoints);
            endpoint_probe.enumerate_active_result = enum_hr;
            if (FAILED(enum_hr)) {
                std::cerr << "[audio] EnumAudioEndpoints failed hr=0x"
                          << std::hex << static_cast<unsigned long>(enum_hr)
                          << std::dec << '\n';
                return create_hr;
            }

            UINT count = 0;
            enum_hr = endpoints->GetCount(&count);
            endpoint_probe.active_count_result = enum_hr;
            if (FAILED(enum_hr)) {
                std::cerr << "[audio] IMMDeviceCollection::GetCount failed hr=0x"
                          << std::hex << static_cast<unsigned long>(enum_hr)
                          << std::dec << '\n';
                return create_hr;
            }
            endpoint_probe.active_render_endpoint_count = count;
            if (count == 0) {
                return create_hr;
            }

            HRESULT last_hr = create_hr;
            for (UINT index = 0; index < count; ++index) {
                ComPtr<IMMDevice> endpoint;
                enum_hr = endpoints->Item(index, &endpoint);
                if (FAILED(enum_hr) || endpoint == nullptr) {
                    last_hr = FAILED(enum_hr) ? enum_hr : E_POINTER;
                    endpoint_probe.last_active_endpoint_result = last_hr;
                    continue;
                }

                LPWSTR raw_id = nullptr;
                enum_hr = endpoint->GetId(&raw_id);
                if (FAILED(enum_hr) || raw_id == nullptr) {
                    last_hr = FAILED(enum_hr) ? enum_hr : E_POINTER;
                    endpoint_probe.last_active_endpoint_result = last_hr;
                    continue;
                }
                const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)>
                    endpoint_id_owner(raw_id, &CoTaskMemFree);
                const std::wstring endpoint_id(endpoint_id_owner.get());

                HRESULT endpoint_hr = engine_->CreateMasteringVoice(
                    &mastering_voice_,
                    XAUDIO2_DEFAULT_CHANNELS,
                    XAUDIO2_DEFAULT_SAMPLERATE,
                    0,
                    endpoint_id.c_str());
                if (SUCCEEDED(endpoint_hr)) {
                    std::cout << "[audio] default endpoint missing; using active "
                                 "render endpoint index="
                              << index << "/" << count << '\n';
                    return endpoint_hr;
                }
                last_hr = endpoint_hr;
                endpoint_probe.last_active_endpoint_result = endpoint_hr;
                if (index < 8u) {
                    std::cerr << "[audio] active render endpoint index="
                              << index
                              << " rejected by XAudio2 hr=0x" << std::hex
                              << static_cast<unsigned long>(endpoint_hr)
                              << std::dec << '\n';
                }
            }
            return last_hr;
        };

        hr = try_create_mastering_voice();
        if (FAILED(hr)) {
            const bool endpoint_not_found =
                native_audio_endpoint_error_recoverable(static_cast<std::uint32_t>(hr), false);
            if (endpoint_not_found) {
                endpoint_unavailable_ = true;
                if (strict_audio_proof_enabled()) {
                    strict_failure_.store(true, std::memory_order_release);
                    set_strict_failure_reason("no usable Windows audio render endpoint");
                }
                if (!endpoint_unavailable_logged_) {
                    endpoint_unavailable_logged_ = true;
                    std::cerr
                        << "[audio] CreateMasteringVoice found no default "
                           "or active Windows audio endpoint"
                        << " default-hr=0x" << std::hex
                        << static_cast<unsigned long>(
                               endpoint_probe.default_create_result)
                        << " enumerator-hr=0x"
                        << static_cast<unsigned long>(
                               endpoint_probe.enumerator_create_result)
                        << " enumerate-active-hr=0x"
                        << static_cast<unsigned long>(
                               endpoint_probe.enumerate_active_result)
                        << " active-count-hr=0x"
                        << static_cast<unsigned long>(
                               endpoint_probe.active_count_result)
                        << " active-render-count=" << std::dec
                        << endpoint_probe.active_render_endpoint_count
                        << " last-active-endpoint-hr=";
                    if (endpoint_probe.last_active_endpoint_result.has_value()) {
                        std::cerr
                            << "0x" << std::hex
                            << static_cast<unsigned long>(
                                   *endpoint_probe.last_active_endpoint_result)
                            << std::dec;
                    } else {
                        std::cerr << "none";
                    }
                    std::cerr << '\n';
                }
                if (!strict_audio_proof_enabled()) {
                    shutdown_backend(false);
                    if (strict_failure_.load(std::memory_order_acquire)) return false;
                    // The logical sink stays usable; physical output does not. poll_health retries without
                    // re-entering the gate.
                    wait_for_endpoint_after_shutdown();
                    return true;
                }
            } else {
                strict_failure_.store(true, std::memory_order_release);
                set_strict_failure_reason(
                    "XAudio2 mastering voice creation failed");
                std::cerr << "[audio] CreateMasteringVoice failed hr=0x"
                          << std::hex << static_cast<unsigned long>(hr)
                          << std::dec << '\n';
            }
            shutdown_backend(false);
            return false;
        }
        endpoint_unavailable_ = false;
        endpoint_offline_ = false;

        hr = engine_->StartEngine();
        if (FAILED(hr)) {
            if (native_audio_endpoint_error_recoverable(
                    static_cast<std::uint32_t>(hr), strict_audio_proof_enabled())) {
                shutdown_backend(false);
                if (strict_failure_.load(std::memory_order_acquire)) return false;
                wait_for_endpoint_after_shutdown();
                return true;
            }
            strict_failure_.store(true, std::memory_order_release);
            set_strict_failure_reason("XAudio2 engine start failed");
            std::cerr << "[audio] XAudio2 StartEngine failed hr=0x" << std::hex
                      << static_cast<unsigned long>(hr) << std::dec << '\n';
            shutdown_backend(false);
            return false;
        }
        strict_engine_glitch_baseline_ = query_backend_engine_glitches();
        if (!create_source_voice()) {
            const bool unavailable = endpoint_recovery_pending_.load(std::memory_order_acquire);
            shutdown_backend(false);
            if (unavailable && !strict_failure_.load(std::memory_order_acquire)) {
                endpoint_recovery_pending_.store(false, std::memory_order_release);
                wait_for_endpoint_after_shutdown();
                return true;
            }
            return false;
        }

        prebuffer_buffers_ = std::min<std::uint32_t>(
            queue_limit_,
            read_env_u32(
                "GALAXY_AUDIO_PREBUFFER_BUFFERS",
                4,
                1,
                XAUDIO2_MAX_QUEUED_BUFFERS));
        shutting_down_.store(false, std::memory_order_release);
        worker_stop_.store(false, std::memory_order_release);
        try {
            worker_thread_ =
                std::thread(&NativeAudioSink::audio_worker_loop, this);
        } catch (const std::exception& ex) {
            submit_failure_events_.fetch_add(
                1, std::memory_order_acq_rel);
            strict_failure_.store(true, std::memory_order_release);
            set_strict_failure_reason("native audio worker thread failed");
            std::cerr << "[audio] failed to start native audio worker: "
                      << ex.what() << '\n';
            shutdown_backend(false);
            return false;
        }
        initialized_ = true;
        if (trace_host_startup() || trace_audio_queue()) {
            std::cout << "[audio] XAudio2 native output initialized: "
                      << "one 32 kHz stereo PCM16 voice, boundary-only 1.0/1.5 frequency ratio, queue-limit="
                      << queue_limit_ << " prebuffer=" << prebuffer_buffers_
                      << " pending-limit=" << pending_queue_limit_
                      << '\n';
        }
        return true;
    }

    void wait_for_endpoint_after_shutdown() noexcept {
        endpoint_offline_ = true;
        endpoint_last_attempt_ns_ = host_time_ns(std::chrono::steady_clock::now());
        initialized_ = true;
    }

    void shutdown() {
        try {
            endpoint_recovery_task_.finish();
        } catch (...) {
            strict_failure_.store(true, std::memory_order_release);
            set_strict_failure_reason("native audio device recovery task failed during shutdown");
        }
        shutdown_backend(true);
    }

    void shutdown_backend(bool release_control_com) {
        shutting_down_.store(true, std::memory_order_release);
        worker_stop_.store(true, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(pending_mutex_);
            if (endpoint_recovery_teardown_) {
                endpoint_unheard_buffers_.fetch_add(pending_buffers_.size(), std::memory_order_relaxed);
            }
            pending_buffers_.clear();
            pending_queue_depth_.store(0, std::memory_order_release);
        }
        pending_cv_.notify_all();
        if (buffer_end_event_ != nullptr) {
            SetEvent(buffer_end_event_);
        }
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
        if (endpoint_recovery_teardown_) {
            for (const auto& buffer : in_flight_buffers_) {
                if (!buffer->completed.load(std::memory_order_acquire)) {
                    endpoint_unheard_buffers_.fetch_add(1u, std::memory_order_relaxed);
                }
            }
        }
        if (source_voice_ != nullptr) {
            const HRESULT stop_hr = source_voice_->Stop(0);
            if (FAILED(stop_hr) && !native_audio_endpoint_error_recoverable(
                    static_cast<std::uint32_t>(stop_hr), strict_audio_proof_enabled())) {
                submit_failure_events_.fetch_add(
                    1u, std::memory_order_acq_rel);
                strict_failure_.store(true, std::memory_order_release);
                set_strict_failure_reason(
                    "XAudio2 source voice failed to stop during shutdown");
                std::cerr << "[audio] SourceVoice Stop during shutdown failed hr=0x"
                          << std::hex << static_cast<unsigned long>(stop_hr)
                          << std::dec << '\n';
            }
            const HRESULT flush_hr = source_voice_->FlushSourceBuffers();
            if (FAILED(flush_hr) && !native_audio_endpoint_error_recoverable(
                    static_cast<std::uint32_t>(flush_hr), strict_audio_proof_enabled())) {
                submit_failure_events_.fetch_add(
                    1u, std::memory_order_acq_rel);
                strict_failure_.store(true, std::memory_order_release);
                set_strict_failure_reason(
                    "XAudio2 source voice failed to flush during shutdown");
                std::cerr
                    << "[audio] SourceVoice FlushSourceBuffers during shutdown failed hr=0x"
                    << std::hex << static_cast<unsigned long>(flush_hr)
                    << std::dec << '\n';
            }
            source_voice_->DestroyVoice();
            callback_source_voice_.store(nullptr, std::memory_order_release);
            source_voice_ = nullptr;
        }
        // FlushSourceBuffers may end queued buffers without starting them. DestroyVoice has quiesced
        // callbacks; only this teardown boundary accepts that outcome. Live playback keeps the strict check.
        (void)account_started_and_reap_completed_buffers(true);
        empty_boundary_tracker_.reset_after_voice_destruction(
            callback_ledger_.empty_buffer_end_boundaries());
        (void)process_callback_failures();
        in_flight_buffers_.clear();
        callback_active_sample_rate_.store(
            0u, std::memory_order_release);
        queue_mirror_.publish_from_worker(0);
        if (mastering_voice_ != nullptr) {
            mastering_voice_->DestroyVoice();
            mastering_voice_ = nullptr;
        }
        if (engine_ != nullptr) {
            if (engine_callback_registered_) {
                engine_->UnregisterForCallbacks(this);
                engine_callback_registered_ = false;
            }
            engine_->Release();
            engine_ = nullptr;
        }
        if (com_initialized_ && release_control_com) {
            CoUninitialize();
            com_initialized_ = false;
        }
        if (buffer_end_event_ != nullptr) {
            CloseHandle(buffer_end_event_);
            buffer_end_event_ = nullptr;
        }
        playback_lifecycle_.reset();
        empty_active_.store(false, std::memory_order_release);
        has_last_submit_time_ = false;
        initialized_ = false;
        if (trace_audio_queue() &&
            (submitted_buffers_.load(std::memory_order_acquire) != 0 ||
             callback_ledger_.completed_buffers() != 0)) {
            std::cout << "[audio-queue] summary submitted="
                      << submitted_buffers_.load(std::memory_order_acquire)
                      << " completed="
                      << callback_ledger_.completed_buffers()
                      << " queued="
                      << queue_mirror_.queued_buffers()
                      << " pending="
                      << pending_queue_depth_.load(std::memory_order_acquire)
                      << " empty-events="
                      << queue_empty_events_.load(std::memory_order_acquire)
                      << " dropped=" << dropped_buffers_ << '\n';
        }
    }

    void audio_worker_loop() noexcept {
        try {
            audio_worker_body();
        } catch (...) {
            submit_failure_events_.fetch_add(1, std::memory_order_acq_rel);
            strict_failure_.store(true, std::memory_order_release);
            set_strict_failure_reason("native audio worker exception");
            // Accepted buffers stay in in_flight_buffers_ until the stopped voice is destroyed. Unsubmitted
            // buffers can be discarded.
            try { clear_pending_after_worker_failure(); } catch (...) {}
            pending_cv_.notify_all();
            if (buffer_end_event_ != nullptr) (void)SetEvent(buffer_end_event_);
        }
    }

    void audio_worker_body() {
        galaxy::host::name_current_thread(L"Nebula Audio Queue");
        const ScopedMmcssAudioThread mmcss_thread;
        std::uint64_t observed_callback_sequence =
            callback_ledger_.event_sequence();
        for (;;) {
            std::unique_ptr<SubmittedBuffer> buffer;
            {
                std::lock_guard<std::mutex> lock(pending_mutex_);
                if (worker_stop_.load(std::memory_order_acquire) &&
                    pending_buffers_.empty()) {
                    pending_queue_depth_.store(
                        0, std::memory_order_release);
                    return;
                }
                if (!pending_buffers_.empty()) {
                    buffer = std::move(pending_buffers_.front());
                    pending_buffers_.pop_front();
                    buffer->dequeued_ns = host_time_ns(
                        std::chrono::steady_clock::now());
                }
                pending_queue_depth_.store(
                    static_cast<std::uint32_t>(pending_buffers_.size()),
                    std::memory_order_release);
            }
            pending_cv_.notify_all();

            const std::uint64_t callback_sequence =
                callback_ledger_.event_sequence();
            const bool callback_event =
                callback_sequence != observed_callback_sequence;
            observed_callback_sequence = callback_sequence;
            if (!account_started_and_reap_completed_buffers()) {
                clear_pending_after_worker_failure();
                return;
            }
            if (!process_callback_failures()) {
                if (buffer != nullptr && endpoint_recovery_pending_.load(std::memory_order_acquire)) {
                    endpoint_unheard_buffers_.fetch_add(1u, std::memory_order_relaxed);
                }
                clear_pending_after_worker_failure();
                return;
            }
            if (buffer == nullptr && !callback_event) {
                const DWORD idle_wait_ms = static_cast<DWORD>(
                    std::max<std::uint32_t>(1u, queue_wait_ms_));
                const DWORD wait_result = buffer_end_event_ != nullptr
                    ? WaitForSingleObject(buffer_end_event_, idle_wait_ms)
                    : WAIT_FAILED;
                if (wait_result == WAIT_FAILED &&
                    !worker_stop_.load(std::memory_order_acquire)) {
                    submit_failure_events_.fetch_add(
                        1, std::memory_order_acq_rel);
                    strict_failure_.store(true, std::memory_order_release);
                    set_strict_failure_reason(
                        "native audio worker wake wait failed");
                    clear_pending_after_worker_failure();
                    return;
                }
                continue;
            }
            if (callback_event && source_voice_ != nullptr) {
                const unsigned queued = query_source_queue();
                if (queued == 0u &&
                    !mark_source_queue_empty("worker-observed-callback")) {
                    if (buffer != nullptr && endpoint_recovery_pending_.load(std::memory_order_acquire)) {
                        endpoint_unheard_buffers_.fetch_add(1u, std::memory_order_relaxed);
                    }
                    clear_pending_after_worker_failure();
                    return;
                }
            }
            if (buffer == nullptr) {
                continue;
            }
            if (!submit_pending_to_xaudio(buffer)) {
                if (buffer != nullptr && endpoint_recovery_pending_.load(std::memory_order_acquire)) {
                    endpoint_unheard_buffers_.fetch_add(1u, std::memory_order_relaxed);
                }
                clear_pending_after_worker_failure();
                return;
            }
        }
    }

    void clear_pending_after_worker_failure() {
        if (worker_stop_.load(std::memory_order_acquire) ||
            endpoint_recovery_pending_.load(std::memory_order_acquire)) {
            return;
        }
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_buffers_.clear();
        pending_queue_depth_.store(0, std::memory_order_release);
        pending_cv_.notify_all();
    }

    bool request_endpoint_recovery(HRESULT error) noexcept {
        if (!native_audio_endpoint_error_recoverable(
                static_cast<std::uint32_t>(error), strict_audio_proof_enabled())) return false;
        if (!shutting_down_.load(std::memory_order_acquire)) {
            endpoint_recovery_pending_.store(true, std::memory_order_release);
        }
        return true;
    }

    bool process_callback_failures() {
        if (endpoint_recovery_pending_.load(std::memory_order_acquire) &&
            !shutting_down_.load(std::memory_order_acquire)) return false;
        const std::uint64_t engine_errors =
            callback_ledger_.engine_critical_errors();
        if (engine_errors != handled_engine_critical_error_count_) {
            handled_engine_critical_error_count_ = engine_errors;
            const auto error = static_cast<HRESULT>(callback_ledger_.last_engine_critical_error());
            if (native_audio_endpoint_error_recoverable(
                    static_cast<std::uint32_t>(error), strict_audio_proof_enabled())) {
                if (!shutting_down_.load(std::memory_order_acquire)) {
                    std::cerr << "[audio] output endpoint unavailable hr=0x" << std::hex
                              << static_cast<unsigned long>(error) << std::dec
                              << "; scheduling device recovery\n";
                    endpoint_recovery_pending_.store(true, std::memory_order_release);
                    return false;
                }
            } else {
                strict_failure_.store(true, std::memory_order_release);
                set_strict_failure_reason("XAudio2 engine critical error");
                if (!engine_critical_error_logged_) {
                    engine_critical_error_logged_ = true;
                    std::cerr << "[audio] XAudio2 engine critical error hr=0x"
                              << std::hex << static_cast<unsigned long>(error)
                              << std::dec << '\n';
                }
                return false;
            }
        }
        const std::uint64_t voice_errors = callback_ledger_.voice_errors();
        if (voice_errors != handled_voice_error_count_) {
            handled_voice_error_count_ = voice_errors;
            const auto endpoint_error = static_cast<HRESULT>(callback_ledger_.last_voice_error());
            if (request_endpoint_recovery(endpoint_error)) {
                if (!shutting_down_.load(std::memory_order_acquire)) return false;
            } else {
                strict_failure_.store(true, std::memory_order_release);
                set_strict_failure_reason("XAudio2 voice error");
                if (!voice_error_logged_) {
                    voice_error_logged_ = true;
                    const auto error = static_cast<HRESULT>(
                        callback_ledger_.last_voice_error());
                    std::cerr << "[audio] XAudio2 voice error hr=0x" << std::hex
                              << static_cast<unsigned long>(error) << std::dec
                              << '\n';
                }
                return false;
            }
        }
        const std::uint64_t frequency_ratio_errors =
            callback_ledger_.frequency_ratio_apply_errors();
        if (frequency_ratio_errors !=
            handled_frequency_ratio_apply_error_count_) {
            handled_frequency_ratio_apply_error_count_ =
                frequency_ratio_errors;
            const auto endpoint_error = static_cast<HRESULT>(callback_ledger_.last_frequency_ratio_apply_error());
            if (request_endpoint_recovery(endpoint_error)) {
                if (!shutting_down_.load(std::memory_order_acquire)) return false;
            } else {
                strict_failure_.store(true, std::memory_order_release);
                set_strict_failure_reason(
                    "XAudio2 OnBufferStart frequency-ratio apply failed");
                if (!frequency_ratio_apply_error_logged_) {
                    frequency_ratio_apply_error_logged_ = true;
                    const auto error = static_cast<HRESULT>(
                        callback_ledger_.last_frequency_ratio_apply_error());
                    std::cerr
                        << "[audio] OnBufferStart SetFrequencyRatio failed hr=0x"
                        << std::hex << static_cast<unsigned long>(error)
                        << std::dec << '\n';
                }
                return false;
            }
        }
        const std::uint64_t callback_wake_errors =
            callback_ledger_.callback_wake_errors();
        if (callback_wake_errors != handled_callback_wake_error_count_) {
            handled_callback_wake_error_count_ = callback_wake_errors;
            strict_failure_.store(true, std::memory_order_release);
            set_strict_failure_reason("native audio callback wake failed");
            if (!callback_wake_error_logged_) {
                callback_wake_error_logged_ = true;
                const auto error = static_cast<HRESULT>(
                    callback_ledger_.last_callback_wake_error());
                std::cerr << "[audio] callback SetEvent failed hr=0x"
                          << std::hex << static_cast<unsigned long>(error)
                          << std::dec << '\n';
            }
            return false;
        }
        const std::uint64_t empty_boundaries =
            callback_ledger_.empty_buffer_end_boundaries();
        switch (empty_boundary_tracker_.observe(empty_boundaries)) {
        case NativeAudioEmptyBoundaryDisposition::None:
            return true;
        case NativeAudioEmptyBoundaryDisposition::RecoverUnderrun:
            return mark_source_queue_empty(
                "callback-buffer-boundary",
                last_callback_empty_boundary_us_.load(
                    std::memory_order_acquire));
        case NativeAudioEmptyBoundaryDisposition::FatalCounterDiscontinuity:
            submit_failure_events_.fetch_add(1u, std::memory_order_acq_rel);
            strict_failure_.store(true, std::memory_order_release);
            set_strict_failure_reason(
                "native audio empty-boundary ledger discontinuity");
            return false;
        }
        return false;
    }

    [[nodiscard]] bool account_started_and_reap_completed_buffers(
        bool voice_destroyed_after_flush = false) {
        for (auto it = in_flight_buffers_.begin();
             it != in_flight_buffers_.end();) {
            SubmittedBuffer& buffer = **it;
            const auto observation = observe_callback_buffer_state(
                [&] { return buffer.completed.load(std::memory_order_acquire); },
                [&] { return buffer.started.load(std::memory_order_acquire); });
            const bool completed = observation.completed;
            const bool started = observation.started;
            if (completed && !started && !voice_destroyed_after_flush) {
                submit_failure_events_.fetch_add(1u, std::memory_order_acq_rel);
                strict_failure_.store(true, std::memory_order_release);
                set_strict_failure_reason(
                    "XAudio2 completed a buffer without OnBufferStart");
                return false;
            }
            if (started && !buffer.started_accounted) {
                if (!record_measurement_window_start(
                        buffer.measurement_generation,
                        buffer.sample_rate,
                        buffer.bytes.size(),
                        buffer.ratio_apply_required,
                        buffer.ratio_apply_invoked,
                        buffer.requested_ratio,
                        buffer.active_ratio,
                        buffer.ratio_apply_succeeded,
                        buffer.ratio_inherited,
                        buffer.ratio_changed,
                        buffer.buffer_start_callback_core_work_ns)) {
                    submit_failure_events_.fetch_add(
                        1u, std::memory_order_acq_rel);
                    strict_failure_.store(true, std::memory_order_release);
                    set_strict_failure_reason(
                        "native audio generation-local start/ratio evidence is inconsistent");
                    return false;
                }
                buffer.started_accounted = true;
            }
            if (completed) {
                it = in_flight_buffers_.erase(it);
            } else {
                ++it;
            }
        }
        return true;
    }

    unsigned query_source_queue() {
        if (source_voice_ == nullptr) {
            queue_mirror_.publish_from_worker(0);
            return 0;
        }
        XAUDIO2_VOICE_STATE state{};
        source_voice_->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
        queue_mirror_.publish_from_worker(state.BuffersQueued);
        return state.BuffersQueued;
    }

    bool create_source_voice() {
        if (engine_ == nullptr || source_voice_ != nullptr) {
            submit_failure_events_.fetch_add(1, std::memory_order_acq_rel);
            strict_failure_.store(true, std::memory_order_release);
            set_strict_failure_reason("invalid native audio voice ownership");
            return false;
        }

        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = 2;
        format.nSamplesPerSec = kNativeAudioVoiceBaseSampleRate;
        format.wBitsPerSample = 16;
        format.nBlockAlign =
            static_cast<WORD>(format.nChannels * format.wBitsPerSample / 8);
        format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;

        const HRESULT hr = engine_->CreateSourceVoice(
            &source_voice_,
            &format,
            0,
            kNativeAudioMaxFrequencyRatio,
            this);
        if (FAILED(hr)) {
            source_voice_ = nullptr;
            if (native_audio_endpoint_error_recoverable(
                    static_cast<std::uint32_t>(hr), strict_audio_proof_enabled())) {
                endpoint_recovery_pending_.store(true, std::memory_order_release);
                return false;
            }
            submit_failure_events_.fetch_add(1, std::memory_order_acq_rel);
            strict_failure_.store(true, std::memory_order_release);
            set_strict_failure_reason("XAudio2 CreateSourceVoice failed");
            std::cerr << "[audio] CreateSourceVoice failed base-rate="
                      << kNativeAudioVoiceBaseSampleRate << " hr=0x" << std::hex
                      << static_cast<unsigned long>(hr) << std::dec << '\n';
            return false;
        }
        const std::uint64_t previous_creations =
            source_voice_creation_events_.fetch_add(
                1u, std::memory_order_acq_rel);
        if (previous_creations != 0u) {
            voice_recreation_events_.fetch_add(
                1u, std::memory_order_acq_rel);
        }
        callback_source_voice_.store(
            source_voice_, std::memory_order_release);
        // Zero is deliberate: the first callback in a voice lifetime must set the nominal 1.0 ratio
        // explicitly.
        callback_active_sample_rate_.store(0u, std::memory_order_release);
        playback_lifecycle_.reset();
        queue_mirror_.publish_from_worker(0);
        if (trace_host_startup() || trace_audio_queue()) {
            std::cout << "[audio] created single source voice: base-rate="
                      << kNativeAudioVoiceBaseSampleRate
                      << " Hz max-ratio="
                      << kNativeAudioMaxFrequencyRatio << '\n';
        }
        return true;
    }

    bool submit_pending_to_xaudio(std::unique_ptr<SubmittedBuffer>& buffer) {
        if (buffer == nullptr) {
            return true;
        }
        if (source_voice_ == nullptr) {
            submit_failure_events_.fetch_add(
                1, std::memory_order_acq_rel);
            strict_failure_.store(true, std::memory_order_release);
            set_strict_failure_reason(
                "native audio source voice unavailable");
            return false;
        }
        if (strict_failure_.load(std::memory_order_acquire)) {
            return false;
        }

        XAUDIO2_VOICE_STATE state{};
        bool waited_for_queue_space = false;
        auto queue_wait_started = std::chrono::steady_clock::now();
        std::uint32_t queue_wait_elapsed_ms = 0;
        // OnBufferEnd wakes the worker for freed space only while this is set. It is raised before each
        // queue observation, so no end is missed.
        const ScopeExit clear_space_wait([this]() noexcept {
            queue_space_wait_.store(false, std::memory_order_release);
        });
        for (;;) {
            if (worker_stop_.load(std::memory_order_acquire)) {
                return false;
            }
            if (!process_callback_failures()) return false;
            queue_space_wait_.store(true, std::memory_order_seq_cst);
            state.BuffersQueued = query_source_queue();
            if (state.BuffersQueued < queue_limit_) {
                queue_space_wait_.store(false, std::memory_order_release);
                break;
            }
            if (!waited_for_queue_space) {
                waited_for_queue_space = true;
                queue_wait_started = std::chrono::steady_clock::now();
            }
            const auto now = std::chrono::steady_clock::now();
            const auto elapsed_ms = std::chrono::duration_cast<
                std::chrono::milliseconds>(now - queue_wait_started).count();
            queue_wait_elapsed_ms = static_cast<std::uint32_t>(
                std::min<std::int64_t>(
                    std::max<std::int64_t>(elapsed_ms, 0),
                    std::numeric_limits<std::uint32_t>::max()));
            if (queue_wait_elapsed_ms >= queue_wait_ms_ &&
                !queue_full_wait_logged_) {
                queue_full_wait_logged_ = true;
                std::cout << "[audio] native worker queue stayed full for "
                          << queue_wait_elapsed_ms
                          << " ms; waiting without dropping AI buffer"
                          << " queued=" << state.BuffersQueued
                          << " limit=" << queue_limit_
                          << " pending="
                          << pending_queue_depth_.load(
                                  std::memory_order_acquire)
                          << '\n';
            }
            if (queue_wait_elapsed_ms >= queue_stuck_recovery_ms_) {
                ++queue_stuck_recovery_events_;
                if (!queue_stuck_recovery_logged_ ||
                    queue_stuck_recovery_events_ <= 8u) {
                    queue_stuck_recovery_logged_ = true;
                    std::cerr
                        << "[audio] native output queue stopped draining for "
                        << queue_wait_elapsed_ms
                        << " ms; stopping instead of discarding queued audio"
                        << " event=" << queue_stuck_recovery_events_
                        << " queued=" << state.BuffersQueued
                        << " limit=" << queue_limit_
                        << " pending="
                        << pending_queue_depth_.load(
                               std::memory_order_acquire)
                        << '\n';
                }
                submit_failure_events_.fetch_add(
                    1, std::memory_order_acq_rel);
                strict_failure_.store(true, std::memory_order_release);
                set_strict_failure_reason(
                    "native audio output queue stopped draining");
                return false;
            }
            const DWORD wait_ms = static_cast<DWORD>(
                std::max<std::uint32_t>(1u, queue_wait_ms_));
            const DWORD wait_result = buffer_end_event_ != nullptr
                ? WaitForSingleObject(buffer_end_event_, wait_ms)
                : (Sleep(wait_ms), WAIT_TIMEOUT);
            if (worker_stop_.load(std::memory_order_acquire)) {
                return false;
            }
            if (wait_result == WAIT_FAILED) {
                submit_failure_events_.fetch_add(
                    1, std::memory_order_acq_rel);
                strict_failure_.store(true, std::memory_order_release);
                set_strict_failure_reason(
                    "native audio queue wait failed");
                if (!queue_wait_failure_logged_) {
                    queue_wait_failure_logged_ = true;
                    std::cerr << "[audio] native worker queue wait failed"
                              << " error=" << GetLastError()
                              << " queued=" << state.BuffersQueued
                              << " limit=" << queue_limit_ << '\n';
                }
                return false;
            }
            if (!process_callback_failures()) {
                return false;
            }
            if (!account_started_and_reap_completed_buffers()) {
                return false;
            }
        }
        if (waited_for_queue_space) {
            const auto waited_ms_count = std::chrono::duration_cast<
                std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - queue_wait_started)
                                             .count();
            const std::uint32_t waited_ms = std::max<std::uint32_t>(
                1u,
                static_cast<std::uint32_t>(
                    std::min<std::int64_t>(
                        std::max<std::int64_t>(waited_ms_count, 0),
                        std::numeric_limits<std::uint32_t>::max())));
            queue_wait_events_.fetch_add(1, std::memory_order_acq_rel);
            total_queue_wait_ms_.fetch_add(waited_ms, std::memory_order_acq_rel);
            update_max_atomic(max_queue_wait_ms_, waited_ms);
            if (trace_audio_queue() &&
                (wait_log_count_ < 8 || waited_ms >= 10)) {
                ++wait_log_count_;
                std::cout << "[audio] worker waited " << waited_ms
                          << " ms for native queue space"
                          << " queued=" << state.BuffersQueued
                          << " pending="
                          << pending_queue_depth_.load(
                                 std::memory_order_acquire)
                          << '\n';
            }
        }

        const std::size_t buffer_size = buffer->bytes.size();
        const std::uint32_t buffer_sample_rate = buffer->sample_rate;
        const std::uint64_t buffer_measurement_generation =
            buffer->measurement_generation;
        if (resume_fade_pending_.exchange(false, std::memory_order_acq_rel)) {
            // The voice ran dry and stopped on silence, so the first buffer after restart rises from silence
            // over 4 ms instead of stepping.
            fade_in_pcm16_le_stereo(buffer->bytes);
        }
        wav_dump_.append(
            std::span<const std::byte>(buffer->bytes.data(), buffer_size),
            buffer_sample_rate);
        if (!process_callback_failures()) {
            return false;
        }
        const unsigned queued_before_submit = query_source_queue();
        if (queued_before_submit == 0u &&
            !mark_source_queue_empty("worker-pre-submit")) {
            return false;
        }
        // Reconcile starts after the final capacity observation and before adding one buffer. This keeps
        // the generation-local submitted-minus-started tail bounded by the finite XAudio queue, even if
        // callbacks drained a full queue during this submit.
        if (!account_started_and_reap_completed_buffers() ||
            !process_callback_failures()) {
            return false;
        }
        if (source_voice_ == nullptr) {
            submit_failure_events_.fetch_add(
                1, std::memory_order_acq_rel);
            strict_failure_.store(true, std::memory_order_release);
            set_strict_failure_reason(
                "native audio source voice disappeared before submit");
            return false;
        }

        XAUDIO2_BUFFER xa_buffer{};
        xa_buffer.AudioBytes = static_cast<UINT32>(buffer_size);
        xa_buffer.pAudioData =
            reinterpret_cast<const BYTE*>(buffer->bytes.data());
        xa_buffer.pContext = buffer.get();

        buffer->submit_begin_ns = host_time_ns(std::chrono::steady_clock::now());
        // Admits durable storage before XAudio can observe the PCM or context. An allocation failure here
        // leaves the engine untouched. Once accepted, worker failure keeps ownership until voice
        // destruction or reaping.
        auto* submitted_buffer = buffer.get();
        in_flight_buffers_.push_back(std::move(buffer));
        const HRESULT hr = source_voice_->SubmitSourceBuffer(&xa_buffer);
        if (FAILED(hr)) {
            buffer = std::move(in_flight_buffers_.back());
            in_flight_buffers_.pop_back();
        }
        if (FAILED(hr)) {
            if (native_audio_endpoint_error_recoverable(
                    static_cast<std::uint32_t>(hr), strict_audio_proof_enabled())) {
                endpoint_recovery_pending_.store(true, std::memory_order_release);
                return false;
            }
            submit_failure_events_.fetch_add(
                1, std::memory_order_acq_rel);
            strict_failure_.store(
                true, std::memory_order_release);
            set_strict_failure_reason("XAudio2 SubmitSourceBuffer failed");
            if (!submit_failure_logged_) {
                submit_failure_logged_ = true;
                std::cerr << "[audio] XAudio2 SubmitSourceBuffer failed hr=0x"
                          << std::hex << static_cast<unsigned long>(hr)
                          << std::dec << '\n';
            }
            return false;
        }
        const auto submit_time = std::chrono::steady_clock::now();
        double submit_delta_ms = 0.0;
        std::uint64_t submit_delta_us = 0u;
        std::uint64_t expected_us = 0u;
        if (has_last_submit_time_) {
            submit_delta_ms =
                std::chrono::duration<double, std::milli>(
                    submit_time - last_submit_time_).count();
            submit_delta_us =
                static_cast<std::uint64_t>(submit_delta_ms * 1000.0);
            submit_delta_events_.fetch_add(1, std::memory_order_acq_rel);
            total_submit_delta_us_.fetch_add(
                submit_delta_us, std::memory_order_acq_rel);
            std::uint64_t current_max =
                max_submit_delta_us_.load(std::memory_order_acquire);
            while (submit_delta_us > current_max &&
                   !max_submit_delta_us_.compare_exchange_weak(
                       current_max,
                       submit_delta_us,
                       std::memory_order_acq_rel,
                       std::memory_order_acquire)) {
            }
            const std::uint64_t frames =
                static_cast<std::uint64_t>(buffer_size) / 4u;
            expected_us =
                (frames * 1'000'000ull + buffer_sample_rate - 1u) /
                buffer_sample_rate;
            if (expected_us != 0u && submit_delta_us > expected_us) {
                submit_delta_over_expected_.fetch_add(
                    1, std::memory_order_acq_rel);
            }
            if (submit_delta_us > 25'000ull) {
                submit_delta_over_25_ms_.fetch_add(
                    1, std::memory_order_acq_rel);
            }
        }
        if (submitted_buffer->anomaly_sequence <= 3u || submit_delta_us > 25'000u ||
            empty_active_.load(std::memory_order_acquire)) {
            auto record = buffer_anomaly_record(
                NativeAudioAnomalyKind::Submitted, *submitted_buffer,
                host_time_ns(submit_time));
            record.previous_submit_ns = has_last_submit_time_
                ? host_time_ns(last_submit_time_) : 0u;
            record.queued = queued_before_submit;
            record.pending = pending_queue_depth_.load(std::memory_order_acquire);
            record.completed = callback_ledger_.completed_buffers();
            (void)anomaly_ledger_.record(record);
        }
        if (!record_measurement_window_submission(
                buffer_measurement_generation,
                buffer_sample_rate,
                buffer_size,
                last_submit_time_,
                submit_time,
                submit_delta_us,
                expected_us,
                last_submit_measurement_generation_,
                has_last_submit_time_)) {
            submit_failure_events_.fetch_add(1, std::memory_order_acq_rel);
            strict_failure_.store(true, std::memory_order_release);
            set_strict_failure_reason(
                "native audio coherent submission/cadence ledger failed");
            return false;
        }
        last_submit_time_ = submit_time;
        last_submit_measurement_generation_ = buffer_measurement_generation;
        has_last_submit_time_ = true;
        if (!process_callback_failures()) {
            return false;
        }
        const unsigned queued_after_submit = query_source_queue();
        const std::uint64_t submitted_buffers =
            submitted_buffers_.fetch_add(1, std::memory_order_acq_rel) + 1u;
        if (!start_source_voice_if_ready(queued_after_submit)) {
            return false;
        }
        const bool trace_queue = trace_audio_queue();
        if (trace_queue &&
            (submitted_buffers <= 8 || submitted_buffers % 1000 == 0)) {
            std::cout << "[audio] queued AI PCM buffer #" << submitted_buffers
                      << " bytes=" << buffer_size
                      << " rate=" << buffer_sample_rate
                      << " queued=" << queued_after_submit
                      << " pending="
                      << pending_queue_depth_.load(
                             std::memory_order_acquire)
                      << " started="
                      << (playback_lifecycle_.started() ? 1 : 0) << '\n';
        }
        if (trace_queue) {
            std::cout << "[audio-queue] submit #" << submitted_buffers
                      << " bytes=" << buffer_size
                      << " rate=" << buffer_sample_rate
                      << " delta-ms=" << submit_delta_ms
                      << " xa-before=" << queued_before_submit
                      << " xa-after=" << queued_after_submit
                      << " pending="
                      << pending_queue_depth_.load(
                             std::memory_order_acquire)
                      << " completed="
                      << callback_ledger_.completed_buffers()
                      << " empty-events="
                      << queue_empty_events_.load(std::memory_order_acquire)
                      << '\n';
        }
        return true;
    }

    bool start_source_voice_if_ready(unsigned queued_after_submit) {
        if (source_voice_ == nullptr) {
            return true;
        }
        const NativeAudioPlaybackAction start_action =
            playback_lifecycle_.start_action(
                queued_after_submit, prebuffer_buffers_);
        if (start_action != NativeAudioPlaybackAction::StartVoice) {
            return true;
        }
        const bool recovering_from_empty = playback_lifecycle_.recovering();
        const std::uint32_t required_buffers = prebuffer_buffers_;
        NativeAudioAnomalyRecord start_record{};
        start_record.kind = NativeAudioAnomalyKind::VoiceStart;
        start_record.host_ns = host_time_ns(std::chrono::steady_clock::now());
        start_record.queued = queued_after_submit;
        start_record.pending = pending_queue_depth_.load(std::memory_order_acquire);
        start_record.reason = recovering_from_empty ? 1u : 0u;
        (void)anomaly_ledger_.record(start_record);
        const HRESULT hr = source_voice_->Start(0);
        if (FAILED(hr)) {
            if (request_endpoint_recovery(hr)) return false;
            source_start_failure_events_.fetch_add(
                1, std::memory_order_acq_rel);
            strict_failure_.store(
                true, std::memory_order_release);
            set_strict_failure_reason("XAudio2 SourceVoice Start failed");
            if (!source_start_failure_logged_) {
                source_start_failure_logged_ = true;
                std::cerr << "[audio] SourceVoice Start failed hr=0x"
                          << std::hex << static_cast<unsigned long>(hr)
                          << std::dec << '\n';
            }
            return false;
        }
        playback_lifecycle_.mark_started();
        if (recovering_from_empty) {
            // Heard silence: from the empty boundary to the restarted voice. max-empty-us stops at the first
            // submit, before re-prebuffer.
            const std::uint64_t empty_since =
                last_empty_since_us_.load(std::memory_order_acquire);
            const std::uint64_t now_us =
                host_time_us(std::chrono::steady_clock::now());
            const std::uint64_t silent_us =
                now_us > empty_since ? now_us - empty_since : 0u;
            total_silent_us_.fetch_add(silent_us, std::memory_order_acq_rel);
            update_max_atomic(max_silent_us_, static_cast<std::uint32_t>(
                std::min<std::uint64_t>(silent_us, 0xFFFF'FFFFu)));
        }
        if (trace_audio_queue() &&
            (!prebuffer_start_logged_ ||
             (recovering_from_empty && empty_recovery_start_log_count_ < 8u))) {
            prebuffer_start_logged_ = true;
            if (recovering_from_empty) {
                ++empty_recovery_start_log_count_;
            }
            std::cout << "[audio] SourceVoice started"
                      << (recovering_from_empty
                              ? " after empty recovery"
                              : " after prebuffer")
                      << " queued=" << queued_after_submit
                      << " target=" << required_buffers << '\n';
        }
        return true;
    }

    static std::uint64_t host_time_us(
        std::chrono::steady_clock::time_point time) {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                time.time_since_epoch()).count());
    }

    static void update_max_atomic(
        std::atomic<std::uint32_t>& target,
        std::uint32_t value) {
        std::uint32_t current = target.load(std::memory_order_acquire);
        while (value > current &&
               !target.compare_exchange_weak(
                   current,
                   value,
                   std::memory_order_acq_rel,
                   std::memory_order_acquire)) {
        }
    }

    void record_empty_recovery(
        std::chrono::steady_clock::time_point submit_time,
        const SubmittedBuffer& buffer) {
        if (!empty_active_.exchange(false, std::memory_order_acq_rel)) {
            return;
        }
        const std::uint64_t empty_since =
            last_empty_since_us_.load(std::memory_order_acquire);
        const std::uint64_t submit_us = host_time_us(submit_time);
        const std::uint64_t empty_us =
            submit_us > empty_since ? submit_us - empty_since : 0u;
        auto record = buffer_anomaly_record(
            NativeAudioAnomalyKind::EmptyRecovered, buffer,
            host_time_ns(submit_time));
        record.empty_since_ns = empty_since * 1'000u;
        record.pending = pending_queue_depth_.load(std::memory_order_acquire);
        record.completed = callback_ledger_.completed_buffers();
        (void)anomaly_ledger_.record(record);
        empty_recovery_events_.fetch_add(1, std::memory_order_acq_rel);
        total_empty_us_.fetch_add(empty_us, std::memory_order_acq_rel);
        if (empty_us > 1000u) {
            empty_over_1_ms_.fetch_add(1, std::memory_order_acq_rel);
        }
        if (empty_us > 5000u) {
            empty_over_5_ms_.fetch_add(1, std::memory_order_acq_rel);
        }
        std::uint32_t current_max =
            max_empty_us_.load(std::memory_order_acquire);
        const auto capped_empty_us = static_cast<std::uint32_t>(
            std::min<std::uint64_t>(
                empty_us, std::numeric_limits<std::uint32_t>::max()));
        while (capped_empty_us > current_max &&
               !max_empty_us_.compare_exchange_weak(
                   current_max,
                   capped_empty_us,
                   std::memory_order_acq_rel,
                   std::memory_order_acquire)) {
        }
    }

    void log_drop(const char* reason) {
        const auto drops = dropped_buffers_.fetch_add(1u, std::memory_order_relaxed) + 1u;
        strict_failure_.store(true, std::memory_order_release);
        set_strict_failure_reason(reason);
        if (drops <= 8) {
            std::cerr << "[audio] dropped AI buffer: " << reason << '\n';
        }
    }

    bool mark_source_queue_empty(
        const char* reason,
        std::uint64_t observed_empty_us = 0u) {
        if (shutting_down_.load(std::memory_order_acquire)) {
            return true;
        }
        const NativeAudioPlaybackAction empty_action =
            playback_lifecycle_.observe_queue(0u);
        if (empty_action !=
            NativeAudioPlaybackAction::StopVoiceForRecovery) {
            return true;
        }
        if (source_voice_ == nullptr) {
            submit_failure_events_.fetch_add(1, std::memory_order_acq_rel);
            strict_failure_.store(true, std::memory_order_release);
            set_strict_failure_reason(
                "native audio recovery lost its source voice");
            return false;
        }
        const HRESULT stop_hr = source_voice_->Stop(0);
        if (FAILED(stop_hr)) {
            if (request_endpoint_recovery(stop_hr)) return false;
            submit_failure_events_.fetch_add(1, std::memory_order_acq_rel);
            strict_failure_.store(true, std::memory_order_release);
            set_strict_failure_reason(
                "XAudio2 source voice failed to stop for underrun recovery");
            std::cerr
                << "[audio] SourceVoice Stop for empty recovery failed hr=0x"
                << std::hex << static_cast<unsigned long>(stop_hr)
                << std::dec << '\n';
            return false;
        }
        if (empty_active_.exchange(true, std::memory_order_acq_rel)) {
            return true;
        }
        resume_fade_pending_.store(true, std::memory_order_release);

        last_empty_since_us_.store(
            observed_empty_us != 0u
                ? observed_empty_us
                : host_time_us(std::chrono::steady_clock::now()),
            std::memory_order_release);
        const std::uint64_t empty =
            queue_empty_events_.fetch_add(1, std::memory_order_acq_rel) + 1u;
        NativeAudioAnomalyRecord empty_record{};
        empty_record.kind = NativeAudioAnomalyKind::QueueEmpty;
        empty_record.host_ns = host_time_ns(std::chrono::steady_clock::now());
        empty_record.empty_since_ns =
            last_empty_since_us_.load(std::memory_order_acquire) * 1'000u;
        empty_record.pending = pending_queue_depth_.load(std::memory_order_acquire);
        empty_record.completed = callback_ledger_.completed_buffers();
        empty_record.previous_submit_ns = has_last_submit_time_
            ? host_time_ns(last_submit_time_) : 0u;
        // 1=callback boundary, 2=worker callback observation, 3=pre-submit.
        empty_record.reason = std::strcmp(reason, "callback-buffer-boundary") == 0
            ? 1u : std::strcmp(reason, "worker-observed-callback") == 0 ? 2u : 3u;
        (void)anomaly_ledger_.record(empty_record);
        // A scene transition can briefly stop AI production while XAudio2 drains the queued buffers. That
        // counts as an underrun; the next submit recovers the voice. Only strict proof mode makes
        // underruns fatal.
        if (strict_audio_proof_enabled()) {
            strict_failure_.store(true, std::memory_order_release);
            set_strict_failure_reason("XAudio2 source queue emptied");
        }
        if (trace_audio_queue() &&
            (empty <= 16u || empty % 256u == 0u)) {
            std::cout << "[audio-queue] source queue emptied"
                      << " event=" << empty
                      << " reason=" << reason
                      << " completed="
                      << callback_ledger_.completed_buffers()
                      << " submitted="
                      << submitted_buffers_.load(std::memory_order_acquire)
                      << '\n';
        }
        return true;
    }

    void STDMETHODCALLTYPE OnProcessingPassStart() noexcept override {}
    void STDMETHODCALLTYPE OnProcessingPassEnd() noexcept override {}
    void STDMETHODCALLTYPE OnCriticalError(HRESULT error) noexcept override {
        callback_ledger_.record_engine_critical_error(
            static_cast<std::int32_t>(error));
        signal_callback_event();
    }

    void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) noexcept override {}
    void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() noexcept override {}
    void STDMETHODCALLTYPE OnStreamEnd() noexcept override {}
    void STDMETHODCALLTYPE OnBufferStart(void* context) noexcept override {
        auto* const buffer = static_cast<SubmittedBuffer*>(context);
        if (buffer == nullptr) {
            callback_ledger_.record_voice_error(
                static_cast<std::int32_t>(E_POINTER));
            signal_callback_event();
            return;
        }
        const auto start_time = std::chrono::steady_clock::now();
        const std::uint32_t active_sample_rate =
            callback_active_sample_rate_.load(std::memory_order_acquire);
        const NativeAudioFrequencyRatioBoundaryDecision ratio =
            native_audio_frequency_ratio_at_boundary(
                active_sample_rate, buffer->sample_rate);
        const NativeAudioFrequencyRatioDecision active_before =
            native_audio_frequency_ratio_for_sample_rate(active_sample_rate);
        HRESULT ratio_hr = E_INVALIDARG;
        bool ratio_changed = false;
        bool ratio_apply_invoked = false;
        bool ratio_apply_succeeded = false;
        bool ratio_inherited = false;
        float active_ratio = active_before.ratio;
        if (ratio.valid && ratio.apply_required) {
            if (IXAudio2SourceVoice* const voice =
                    callback_source_voice_.load(std::memory_order_acquire);
                voice != nullptr) {
                ratio_apply_invoked = true;
                ratio_hr = voice->SetFrequencyRatio(
                    ratio.ratio, XAUDIO2_COMMIT_NOW);
                if (SUCCEEDED(ratio_hr)) {
                    ratio_apply_succeeded = true;
                    ratio_changed = true;
                    active_ratio = ratio.ratio;
                    callback_active_sample_rate_.store(
                        buffer->sample_rate, std::memory_order_release);
                }
            } else if (shutting_down_.load(std::memory_order_acquire)) {
                return;
            } else {
                ratio_hr = E_POINTER;
                callback_ledger_.record_voice_error(
                    static_cast<std::int32_t>(ratio_hr));
            }
        } else if (ratio.valid) {
            // The callback sequence owns this state. A same-rate buffer inherits the last successful exact
            // ratio without calling into XAudio's real-time engine thread.
            ratio_inherited = true;
            active_ratio = ratio.ratio;
        } else {
            callback_ledger_.record_voice_error(
                static_cast<std::int32_t>(ratio_hr));
        }
        buffer->ratio_apply_required = ratio.apply_required;
        buffer->ratio_apply_invoked = ratio_apply_invoked;
        buffer->requested_ratio = ratio.ratio;
        buffer->active_ratio = active_ratio;
        buffer->ratio_apply_succeeded = ratio_apply_succeeded;
        buffer->ratio_inherited = ratio_inherited;
        buffer->ratio_changed = ratio_changed;
        if (ratio_apply_invoked) {
            callback_ledger_.record_frequency_ratio_apply(
                ratio_changed,
                ratio_apply_succeeded,
                static_cast<std::int32_t>(ratio_hr));
        }
        if (empty_active_.load(std::memory_order_acquire)) {
            record_empty_recovery(start_time, *buffer);
        } else if (buffer->anomaly_sequence <= 3u) {
            const auto record = buffer_anomaly_record(
                NativeAudioAnomalyKind::PlaybackStarted, *buffer,
                host_time_ns(start_time));
            (void)anomaly_ledger_.record(record);
        }
        // Measures the callback's variable core work from complete evidence. The release publication and
        // final SetEvent handoff below are excluded, as the proof contract documents.
        const auto callback_core_work_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - start_time)
                .count();
        buffer->buffer_start_callback_core_work_ns =
            callback_core_work_ns > 0
            ? static_cast<std::uint64_t>(callback_core_work_ns)
            : 0u;
        // Publishes only after every plain evidence field above is complete. The worker's acquire load
        // makes this one exact per-buffer transaction. No wake: the worker accounts starts at its next
        // submit or idle timeout; only failures, empty boundaries and freed queue space wake.
        buffer->started.store(true, std::memory_order_release);
        if (ratio_apply_invoked && !ratio_apply_succeeded) {
            signal_callback_event();
        }
    }
    void STDMETHODCALLTYPE OnLoopEnd(void*) noexcept override {}
    void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT error) noexcept override {
        callback_ledger_.record_voice_error(
            static_cast<std::int32_t>(error));
        signal_callback_event();
    }
    void STDMETHODCALLTYPE OnBufferEnd(void* context) noexcept override {
        auto* const buffer = static_cast<SubmittedBuffer*>(context);
        if (buffer == nullptr) {
            callback_ledger_.record_voice_error(
                static_cast<std::int32_t>(E_POINTER));
            signal_callback_event();
            return;
        }
        NativeAudioBufferEndQueueEvidence queue_evidence =
            NativeAudioBufferEndQueueEvidence::Unobserved;
        if (IXAudio2SourceVoice* const voice =
                callback_source_voice_.load(std::memory_order_acquire);
            voice != nullptr) {
            XAUDIO2_VOICE_STATE callback_state{};
            voice->GetState(
                &callback_state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
            if (callback_state.BuffersQueued == 0u) {
                queue_evidence =
                    NativeAudioBufferEndQueueEvidence::EmptyAtBoundary;
                last_callback_empty_boundary_us_.store(
                    host_time_us(std::chrono::steady_clock::now()),
                    std::memory_order_release);
            } else {
                queue_evidence =
                    NativeAudioBufferEndQueueEvidence::SuccessorQueued;
            }
        }
        // Final access to the context in this callback. The worker keeps and later destroys the allocation.
        buffer->completed.store(true, std::memory_order_release);
        callback_ledger_.record_buffer_end(queue_evidence);
        if (queue_evidence != NativeAudioBufferEndQueueEvidence::SuccessorQueued ||
            queue_space_wait_.load(std::memory_order_seq_cst)) {
            signal_callback_event();
        }
    }

    void signal_callback_event() noexcept {
        HRESULT wake_error = E_HANDLE;
        if (buffer_end_event_ != nullptr) {
            if (SetEvent(buffer_end_event_)) {
                return;
            }
            const DWORD error = GetLastError();
            wake_error = error != ERROR_SUCCESS
                ? HRESULT_FROM_WIN32(error)
                : E_FAIL;
        }
        callback_ledger_.record_callback_wake_error(
            static_cast<std::int32_t>(wake_error));
    }

    IXAudio2* engine_{};
    IXAudio2MasteringVoice* mastering_voice_{};
    IXAudio2SourceVoice* source_voice_{};
    std::atomic<IXAudio2SourceVoice*> callback_source_voice_{nullptr};
    std::atomic<std::uint32_t> callback_active_sample_rate_{0u};
    HANDLE buffer_end_event_{};
    NativeAudioCallbackLedger callback_ledger_;
    NativeAudioAnomalyLedger<256u> anomaly_ledger_;
    // Simulation-thread sequence. Immutable buffer evidence passes through the pending mutex and
    // XAudio2 buffer publication.
    std::uint64_t accepted_sequence_{};
    NativeAudioQueueMirror queue_mirror_;
    std::atomic_uint64_t queue_empty_events_{0};
    std::atomic_uint64_t submitted_buffers_{0};
    std::atomic_uint64_t queue_wait_events_{0};
    std::atomic_uint64_t total_queue_wait_ms_{0};
    std::atomic<std::uint32_t> max_queue_wait_ms_{0};
    std::atomic_uint64_t submit_delta_events_{0};
    std::atomic_uint64_t total_submit_delta_us_{0};
    std::atomic_uint64_t max_submit_delta_us_{0};
    mutable std::mutex measurement_window_mutex_;
    std::uint64_t measurement_window_accepted_buffers_{};
    NativeAudioRateLedger measurement_window_accepted_rate_ledger_;
    NativeAudioSubmissionWindowLedger measurement_window_submission_ledger_;
    NativeAudioStartedWindowLedger measurement_window_started_ledger_;
    std::uint64_t measurement_window_voice_recreation_start_{};
    std::uint64_t measurement_window_empty_boundary_start_{};
    std::uint64_t measurement_window_unobserved_boundary_start_{};
    std::uint64_t measurement_window_engine_glitch_start_{};
    std::uint64_t measurement_window_generation_{};
    std::chrono::steady_clock::time_point measurement_window_started_at_{};
    bool measurement_window_active_{};
    std::atomic_uint64_t submit_delta_over_expected_{0};
    std::atomic_uint64_t submit_delta_over_25_ms_{0};
    std::atomic_uint64_t submit_failure_events_{0};
    std::atomic_uint64_t source_start_failure_events_{0};
    std::atomic_uint64_t source_voice_creation_events_{0};
    std::atomic_uint64_t voice_recreation_events_{0};
    std::atomic_uint64_t empty_recovery_events_{0};
    std::atomic_uint64_t total_empty_us_{0};
    std::atomic_uint64_t empty_over_1_ms_{0};
    std::atomic_uint64_t empty_over_5_ms_{0};
    std::atomic_uint64_t last_empty_since_us_{0};
    std::atomic<std::uint32_t> max_empty_us_{0};
    std::atomic_bool queue_space_wait_{false};
    std::atomic_uint64_t total_silent_us_{0};
    std::atomic<std::uint32_t> max_silent_us_{0};
    std::atomic_uint64_t dropped_buffers_{0};
    std::uint64_t handled_voice_error_count_{};
    std::uint64_t handled_engine_critical_error_count_{};
    std::uint64_t handled_frequency_ratio_apply_error_count_{};
    std::uint64_t handled_callback_wake_error_count_{};
    std::uint64_t strict_engine_glitch_baseline_{};
    std::atomic_uint64_t last_callback_empty_boundary_us_{0};
    NativeAudioEmptyBoundaryTracker empty_boundary_tracker_;
    std::uint32_t queue_limit_{8};
    std::uint32_t queue_wait_ms_{20};
    std::uint32_t queue_stuck_recovery_ms_{750};
    std::uint32_t prebuffer_buffers_{4};
    std::uint32_t pending_queue_limit_{256};
    std::uint32_t pending_queue_timeout_ms_{250};
    std::uint32_t wait_log_count_{};
    AudioWavDump wav_dump_;
    std::chrono::steady_clock::time_point last_submit_time_{};
    std::uint64_t last_submit_measurement_generation_{};
    NativeAudioInitializationGate initialization_gate_;
    std::atomic_uint64_t preinitialize_elapsed_us_{0};
    std::atomic_uint64_t first_submit_initialization_wait_us_{0};
    std::atomic<std::uint32_t> initialization_thread_id_{0};
    std::atomic<std::uint32_t> first_submit_thread_id_{0};
    std::atomic_bool first_submit_observed_{false};
    std::atomic_bool first_submit_initialized_backend_{false};
    bool disabled_{};
    bool initialized_{};
    bool endpoint_unavailable_{};
    bool endpoint_unavailable_logged_{};
    // The callback and queue worker publish requests. Recovery exclusively owns the graph while
    // active; main-owner observers use the immutable snapshot.
    NativeAudioRecoveryTask endpoint_recovery_task_;
#if defined(GALAXY_NATIVE_HOST_TEST_SUPPORT)
    std::function<void()> recovery_gate_for_test_;
    std::function<void()> pending_wait_for_test_;
#endif
    AudioSinkStats endpoint_recovery_snapshot_{};
    std::atomic_bool endpoint_recovery_pending_{false};
    std::atomic_uint64_t endpoint_unheard_buffers_{0};
    bool endpoint_offline_{};
    bool endpoint_recovery_teardown_{};
    std::uint64_t endpoint_last_attempt_ns_{};
    std::uint64_t endpoint_retry_attempts_{};
    std::uint64_t engine_glitches_before_restart_{};
    bool com_initialized_{};
    bool has_last_submit_time_{};
    NativeAudioPlaybackLifecycle playback_lifecycle_;
    bool prebuffer_start_logged_{};
    std::uint32_t empty_recovery_start_log_count_{};
    bool source_start_failure_logged_{};
    std::atomic_bool empty_active_{false};
    std::atomic_bool resume_fade_pending_{false};
    std::atomic_bool shutting_down_{false};
    std::atomic_bool strict_failure_{false};
    bool submit_failure_logged_{};
    bool voice_error_logged_{};
    bool engine_critical_error_logged_{};
    bool frequency_ratio_apply_error_logged_{};
    bool callback_wake_error_logged_{};
    bool queue_full_wait_logged_{};
    bool queue_wait_failure_logged_{};
    bool queue_stuck_recovery_logged_{};
    bool pending_overflow_logged_{};
    std::uint64_t queue_stuck_recovery_events_{};
    std::deque<std::unique_ptr<SubmittedBuffer>> pending_buffers_;
    std::deque<std::unique_ptr<SubmittedBuffer>> in_flight_buffers_;
    std::thread worker_thread_;
    mutable std::mutex pending_mutex_;
    std::condition_variable pending_cv_;
    std::atomic_bool worker_stop_{false};
    std::atomic<std::uint32_t> pending_queue_depth_{0};
    std::atomic<std::uint32_t> max_pending_queue_depth_{0};
    std::atomic_uint64_t pending_overflow_events_{0};
    bool engine_callback_registered_{};
    NativeAudioFailureNotifier failure_notifier_;

    void set_strict_failure_reason(const char* reason) noexcept {
        bool notify = false;
        try {
            std::lock_guard<std::mutex> lock(strict_failure_mutex_);
            if (strict_failure_reason_.front() == '\0') {
                (void)strncpy_s(strict_failure_reason_.data(),
                    strict_failure_reason_.size(),
                    reason != nullptr ? reason : "unknown", _TRUNCATE);
                notify = true;
            }
        } catch (...) {
            // Notification still reaches the simulation owner if reporting the original failure cannot
            // acquire its diagnostic mutex.
            notify = true;
        }
        if (notify) failure_notifier_.publish();
    }

public:

private:
    mutable std::mutex strict_failure_mutex_;
    std::array<char, 256> strict_failure_reason_{};
};

#if defined(GALAXY_NATIVE_HOST_TEST_SUPPORT)
bool native_host_test_audio_recovery() {
    bool result = false;
    std::thread owner([&] {
        try {
            NativeAudioSink sink;
            result = sink.test_recovery_ownership();
        } catch (...) { result = false; }
    });
    owner.join();
    return result;
}

bool native_host_test_audio_recovery_backpressure(unsigned test_case) {
    NativeAudioSink sink;
    return sink.test_recovery_backpressure(test_case);
}
#endif

static std::atomic<HostPointerProvider> g_host_pointer_provider{nullptr};
static std::atomic<HostPointerTransitionProvider>
    g_host_pointer_transition_provider{nullptr};
static std::atomic<HostPointerClock> g_host_pointer_clock{nullptr};
static std::atomic<NativeKeyboardCaptureProvider>
    g_native_keyboard_capture_provider{nullptr};
static std::atomic<NativeSettingsVisibleProvider> g_native_settings_visible{nullptr};
static std::atomic<NativeSettingsKeySink> g_native_settings_key_sink{nullptr};
static galaxy::input::NativeWindowKeyPressLatch g_window_key_presses;
static galaxy::input::NativeWindowKeyHeldState g_window_keys_held;
// Source of the latest window message per key and for the mouse: true when it came from SendInput
// rather than a device (GetCurrentInputMessageSource).
static std::array<std::atomic<bool>, 256> g_window_key_injected{};
static std::atomic<bool> g_pointer_move_injected{false};
static std::atomic<bool> g_pointer_button_injected{false};

// Controller binding capture (overlay CONTROLS page). State: 0 idle, else
// 1 + scheme while armed. The result is (1 << 16) | kind << 8 | code.
static std::atomic<std::uint32_t> g_binding_capture_armed{0u};
static std::atomic<std::uint32_t> g_binding_capture_result{0u};
// XInput pads (bit per user index) seen by the input path, and when it last looked.
static std::atomic<std::uint32_t> g_xinput_mask{0u};
static std::atomic<std::uint64_t> g_xinput_looked_ms{0u};
// The device that last gave input, for the CONTROLS page: (scheme << 8) | slot.
constexpr std::uint32_t kLastInputNone = 0xFFFFFFFFu;
constexpr std::uint32_t kLastInputKeyboard = 0xFFFFFFFEu;
static std::atomic<std::uint32_t> g_last_input{kLastInputNone};

// An armed capture holds the scheme as 1 + scheme in its low byte and the
// device slot above it.
constexpr input::ControlScheme capture_scheme_of(std::uint32_t armed) noexcept {
    return static_cast<input::ControlScheme>((std::max<std::uint32_t>(armed & 0xffu, 1u)) - 1u);
}

void arm_controller_binding_capture(input::ControlScheme scheme, std::uint8_t slot) noexcept {
    g_binding_capture_result.store(0u, std::memory_order_release);
    g_binding_capture_armed.store((static_cast<std::uint32_t>(slot) << 8u) | (1u + static_cast<std::uint32_t>(scheme)),
                                  std::memory_order_release);
    if (input::scheme_uses_hid(scheme)) {
        ensure_controller_hid_started();
    }
}

void cancel_controller_binding_capture() noexcept {
    g_binding_capture_armed.store(0u, std::memory_order_release);
}

std::optional<input::InputSource> take_controller_binding_capture() noexcept {
    const std::uint32_t result = g_binding_capture_result.exchange(0u, std::memory_order_acq_rel);
    if ((result & 0x10000u) == 0u) {
        return std::nullopt;
    }
    return input::InputSource{static_cast<input::SourceKind>((result >> 8u) & 0xffu),
                              static_cast<std::uint8_t>(result & 0xffu)};
}

bool controller_present_now() noexcept {
    for (DWORD slot = 0; slot < XUSER_MAX_COUNT; ++slot) {
        XINPUT_STATE state{};
        if (XInputGetState(slot, &state) == ERROR_SUCCESS) {
            return true;
        }
    }
    return hid_controller_present_now();
}

std::size_t connected_controllers(std::span<input::ControllerInfo> out) noexcept {
    std::uint32_t xinput = g_xinput_mask.load(std::memory_order_acquire);
    const std::uint64_t now = GetTickCount64();
    if (now - g_xinput_looked_ms.load(std::memory_order_relaxed) > 1000u) {
        xinput = 0u;
        for (DWORD slot = 0; slot < XUSER_MAX_COUNT; ++slot) {
            XINPUT_STATE state{};
            if (XInputGetState(slot, &state) == ERROR_SUCCESS) xinput |= 1u << slot;
        }
        g_xinput_mask.store(xinput, std::memory_order_release);
        g_xinput_looked_ms.store(now, std::memory_order_relaxed);
    }
    std::size_t count = 0;
    for (DWORD slot = 0; slot < XUSER_MAX_COUNT && count < out.size(); ++slot) {
        if ((xinput & (1u << slot)) == 0u) continue;
        input::ControllerInfo& info = out[count++];
        info = {};
        info.scheme = input::ControlScheme::Xbox;
        info.slot = static_cast<std::uint8_t>(slot);
        info.model = "Xbox controller";
    }
    return count + hid_controllers(out.subspan(count));
}

std::optional<input::DeviceKey> last_input_device() noexcept {
    const std::uint32_t key = g_last_input.load(std::memory_order_relaxed);
    if (key == kLastInputNone) return std::nullopt;
    if (key == kLastInputKeyboard) return input::DeviceKey{input::ControlScheme::Keyboard, input::kKeyboardSlot};
    return input::DeviceKey{static_cast<input::ControlScheme>((key >> 8u) & 0xffu),
                            static_cast<std::uint8_t>(key & 0xffu)};
}

void note_keyboard_input() noexcept {
    g_last_input.store(kLastInputKeyboard, std::memory_order_relaxed);
}

void publish_native_window_key_state(std::uint32_t key, bool down) noexcept {
    g_window_keys_held.set(key, down);
}

void publish_native_window_input_origin(std::uint32_t key, bool injected) noexcept {
    if (key < g_window_key_injected.size()) {
        g_window_key_injected[key].store(injected, std::memory_order_relaxed);
    }
    if (!injected) note_keyboard_input();
}

void publish_native_pointer_input_origin(bool button, bool injected) noexcept {
    (button ? g_pointer_button_injected : g_pointer_move_injected)
        .store(injected, std::memory_order_relaxed);
    if (button && !injected) note_keyboard_input();
}

bool native_window_key_injected(std::uint32_t key) noexcept {
    return key < g_window_key_injected.size() &&
           g_window_key_injected[key].load(std::memory_order_relaxed);
}

bool native_pointer_input_injected(bool button) noexcept {
    return (button ? g_pointer_button_injected : g_pointer_move_injected)
        .load(std::memory_order_relaxed);
}

void publish_native_window_key_press(std::uint32_t key) noexcept {
    g_window_key_presses.publish(key);
    if (trace_boot_input()) {
        static std::uint32_t records = 0u;
        if (++records <= 16u) {
            std::cerr << "[window-key-press] key=" << key << '\n';
        }
    }
}

void clear_native_window_key_presses() noexcept {
    g_window_key_presses.clear();
    g_window_keys_held.clear();
}

struct HostPointerTransitionConsumerState {
    std::uint64_t cursor{};
    std::uint64_t focus_epoch{1};
    bool left_button{};
    bool right_button{};
};

static std::mutex g_host_pointer_transition_mutex;
static HostPointerTransitionConsumerState g_host_pointer_transition_state{};

void install_host_pointer_provider(HostPointerProvider provider) noexcept {
    g_host_pointer_provider.store(provider, std::memory_order_release);
}

void install_host_pointer_transition_provider(
    HostPointerTransitionProvider provider) noexcept {
    const std::lock_guard<std::mutex> lock(g_host_pointer_transition_mutex);
    g_host_pointer_transition_provider.store(provider, std::memory_order_release);
    g_host_pointer_transition_state = {};
}

void install_native_keyboard_capture_provider(
    NativeKeyboardCaptureProvider provider) noexcept {
    g_native_keyboard_capture_provider.store(
        provider,
        std::memory_order_release);
}

bool native_keyboard_capture_active() noexcept {
    const NativeKeyboardCaptureProvider provider =
        g_native_keyboard_capture_provider.load(std::memory_order_acquire);
    return provider != nullptr && provider();
}

void install_native_settings_pad_hooks(
    NativeSettingsVisibleProvider visible,
    NativeSettingsKeySink sink) noexcept {
    g_native_settings_visible.store(visible, std::memory_order_release);
    g_native_settings_key_sink.store(sink, std::memory_order_release);
}

namespace {

std::uint64_t host_pointer_now_ms() noexcept {
    const HostPointerClock clock =
        g_host_pointer_clock.load(std::memory_order_acquire);
    return clock != nullptr ? clock()
                            : static_cast<std::uint64_t>(GetTickCount64());
}

struct HostPointerButtonSample {
    bool left_button{};
    bool right_button{};
    bool has_transition_coordinates{};
    HostPointerCoordinates transition_coordinates{};
    std::uint64_t transition_sequence{};
    std::uint64_t transition_absolute_acquired_ms{};
};

HostPointerButtonSample consume_host_pointer_button_transitions(
    const HostPointerState& current,
    bool preserve_each_edge) {
    const std::lock_guard<std::mutex> lock(g_host_pointer_transition_mutex);
    const HostPointerTransitionProvider provider =
        g_host_pointer_transition_provider.load(std::memory_order_acquire);
    if (provider == nullptr) {
        return HostPointerButtonSample{
            current.left_button,
            current.right_button,
            false,
            {},
            0,
            0};
    }

    HostPointerButtonSample sample{};
    constexpr std::size_t kMaximumDrain =
        kHostPointerTransitionCapacity + 1u;
    bool drain_completed = false;
    for (std::size_t drained = 0; drained < kMaximumDrain; ++drained) {
        const HostPointerTransitionPoll poll =
            provider(g_host_pointer_transition_state.cursor);
        if (poll.current_focus_epoch < g_host_pointer_transition_state.focus_epoch) {
            throw std::runtime_error("host pointer focus epoch regressed");
        }
        if (poll.current_focus_epoch > g_host_pointer_transition_state.focus_epoch) {
            // Focus cancellation is authoritative even while older edges are queued. Those edges retire
            // without replaying a click after focus has changed; every current-epoch edge is kept.
            g_host_pointer_transition_state.focus_epoch = poll.current_focus_epoch;
            g_host_pointer_transition_state.left_button = false;
            g_host_pointer_transition_state.right_button = false;
            sample = {};
        }
        if (poll.result == HostPointerTransitionReadResult::Overflow) {
            std::ostringstream message;
            message << "host pointer transition overflow: cursor="
                    << g_host_pointer_transition_state.cursor
                    << " latest=" << poll.latest_sequence;
            throw std::runtime_error(message.str());
        }
        if (poll.result == HostPointerTransitionReadResult::Empty) {
            drain_completed = true;
            break;
        }
        if (poll.result != HostPointerTransitionReadResult::Ready) {
            throw std::runtime_error(
                "host pointer transition provider returned an invalid result");
        }
        const HostPointerTransition& transition = poll.transition;
        if (transition.sequence !=
                g_host_pointer_transition_state.cursor + 1u ||
            transition.focus_epoch > poll.current_focus_epoch) {
            throw std::runtime_error(
                "host pointer transition sequence/epoch invariant failed");
        }

        g_host_pointer_transition_state.cursor = transition.sequence;
        if (transition.focus_epoch < g_host_pointer_transition_state.focus_epoch) {
            continue;
        }
        switch (transition.kind) {
        case HostPointerTransitionKind::LeftDown:
            g_host_pointer_transition_state.left_button = true;
            break;
        case HostPointerTransitionKind::LeftUp:
            g_host_pointer_transition_state.left_button = false;
            break;
        case HostPointerTransitionKind::RightDown:
            g_host_pointer_transition_state.right_button = true;
            break;
        case HostPointerTransitionKind::RightUp:
            g_host_pointer_transition_state.right_button = false;
            break;
        case HostPointerTransitionKind::CancelAll:
            g_host_pointer_transition_state.left_button = false;
            g_host_pointer_transition_state.right_button = false;
            break;
        }
        sample.has_transition_coordinates =
            transition.coordinates.absolute_valid;
        sample.transition_coordinates = transition.coordinates;
        sample.transition_sequence = transition.sequence;
        sample.transition_absolute_acquired_ms =
            transition.absolute_acquired_ms;
        if (preserve_each_edge) {
            drain_completed = true;
            break;
        }
    }
    if (!drain_completed) {
        throw std::runtime_error(
            "host pointer transition provider did not terminate a bounded drain");
    }

    sample.left_button = g_host_pointer_transition_state.left_button;
    sample.right_button = g_host_pointer_transition_state.right_button;
    return sample;
}

struct NativeHidControllerState {
    bool connected = false;
    DWORD slot = XUSER_MAX_COUNT;
    WORD buttons = 0;
    BYTE left_trigger = 0;
    BYTE right_trigger = 0;
    float left_x = 0.0f;
    float left_y = 0.0f;
    float right_x = 0.0f;
    float right_y = 0.0f;
};

// Runs the settings-overlay controller chord and navigation on one pad sample (live or replayed).
// `buttons` is what the game sees; true means sticks and triggers must read neutral too.
bool route_native_settings_pad(std::uint16_t& buttons, float stick_x, float stick_y) {
    const NativeSettingsVisibleProvider visible =
        g_native_settings_visible.load(std::memory_order_acquire);
    const NativeSettingsKeySink sink =
        g_native_settings_key_sink.load(std::memory_order_acquire);
    if (visible == nullptr || sink == nullptr) {
        return false;
    }
    static galaxy::input::NativeSettingsPadState s_state{};
    const galaxy::input::NativeSettingsPadStep step =
        galaxy::input::native_settings_pad_step(
            s_state, buttons, stick_x, stick_y, visible(), GetTickCount64());
    for (std::size_t i = 0; i < step.key_count; ++i) {
        sink(step.keys[i]);
    }
    buttons = step.game_buttons;
    return step.game_neutral;
}

// Every connected XInput pad. Sticks are normalised with `left_deadzone` on the left stick and
// none on the right; the pointer applies a radial dead zone.
std::size_t poll_native_xinput_pads(
    std::int16_t left_deadzone,
    std::array<NativeHidControllerState, XUSER_MAX_COUNT>& out) {
    static ULONGLONG s_slot_retry_at[XUSER_MAX_COUNT]{};
    const ULONGLONG now_ms = GetTickCount64();
    std::size_t count = 0;
    for (DWORD slot = 0; slot < XUSER_MAX_COUNT; ++slot) {
        if (now_ms < s_slot_retry_at[slot]) {
            continue;
        }
        XINPUT_STATE state{};
        if (XInputGetState(slot, &state) != ERROR_SUCCESS) {
            s_slot_retry_at[slot] = now_ms + 2000u;
            continue;
        }
        s_slot_retry_at[slot] = 0u;
        NativeHidControllerState& controller = out[count++];
        controller = {};
        controller.connected = true;
        controller.slot = slot;
        controller.buttons = state.Gamepad.wButtons;
        controller.left_trigger = state.Gamepad.bLeftTrigger;
        controller.right_trigger = state.Gamepad.bRightTrigger;
        controller.left_x = galaxy::input::normalize_xinput_axis(state.Gamepad.sThumbLX, left_deadzone);
        controller.left_y = galaxy::input::normalize_xinput_axis(state.Gamepad.sThumbLY, left_deadzone);
        controller.right_x = galaxy::input::normalize_xinput_axis(state.Gamepad.sThumbRX, 0);
        controller.right_y = galaxy::input::normalize_xinput_axis(state.Gamepad.sThumbRY, 0);
    }
    return count;
}

// The remappable controls as the input consumer sees them: a cached copy
// refreshed when the published generation changes (one atomic load per
// report), plus the keyboard keys the active keyboard table reads.
struct LiveControls {
    galaxy::input::ControlsConfig config = galaxy::input::default_controls_config();
    galaxy::input::KeyList keyboard_keys{};
    std::uint64_t generation = 0;
};

const LiveControls& live_controls() {
    static LiveControls s_live{};
    const std::uint64_t generation = galaxy::input::controls_config_generation();
    if (generation != s_live.generation) {
        s_live.config = galaxy::input::get_controls_config();
        s_live.keyboard_keys = galaxy::input::referenced_keys(
            s_live.config.table(galaxy::input::ControlScheme::Keyboard));
        s_live.generation = generation;
    }
    return s_live;
}

std::int16_t deadzone_counts(float fraction) {
    return static_cast<std::int16_t>(std::clamp(
        std::lround(static_cast<double>(fraction) * 32767.0), 0l, 32767l));
}

// One controller this report: unified pad sample plus its motion output.
struct LivePad {
    galaxy::input::PadSample sample{};
    bool shake_remote = false;
    bool shake_nunchuk = false;
    bool imu_live = false;
    float gyro_x = 0.0f;
    float gyro_y = 0.0f;
    // Controller up (gravity reaction, SDL frame) when the IMU knows it.
    bool gravity_valid = false;
    galaxy::input::tilt::Vec3 gravity{0.0f, 1.0f, 0.0f};
    // XInput user index or Switch worker slot (with the scheme: identity).
    std::uint8_t device_slot = 0xFF;
};

// Vibration (controller_hid.h): the controller in use, its strength setting and the window focus.
// The live input path sets these each report and the game's motor state reads them when it is
// published. Emulation thread only; the replay path leaves it empty (no controller vibrates).
struct RumbleHost {
    RumbleTarget target{};
    std::uint8_t strength = 0;
    bool focused = false;
};
RumbleHost g_rumble_host{};
void publish_rumble_for_mode(bool motor_on, std::optional<std::uint32_t> star_pointer_mode) noexcept;

// `right_deadzone` is radial (pointer_control.h); the right stick must arrive
// without a per-axis dead zone.
galaxy::input::PadSample pad_sample_from_xinput(const NativeHidControllerState& controller,
                                                float right_deadzone) {
    galaxy::input::PadSample sample{};
    sample.connected = controller.connected;
    sample.scheme = galaxy::input::ControlScheme::Xbox;
    sample.buttons = controller.buttons;
    sample.axes[0] = controller.left_x;
    sample.axes[1] = controller.left_y;
    const galaxy::input::StickVector right =
        galaxy::input::apply_radial_deadzone(controller.right_x, controller.right_y, right_deadzone);
    sample.axes[2] = right.x;
    sample.axes[3] = right.y;
    sample.axes[4] = static_cast<float>(controller.left_trigger) / 255.0f;
    sample.axes[5] = static_cast<float>(controller.right_trigger) / 255.0f;
    return sample;
}

std::size_t collect_hid_pads(const galaxy::input::ControlsConfig& config,
                              std::array<LivePad, kMaxHidPads>& out) {
    std::array<HidPadState, kMaxHidPads> pads{};
    const std::size_t count = read_hid_pads(pads, true);
    const std::uint64_t now_us = controller_hid_now_us();
    for (std::size_t i = 0; i < count; ++i) {
        const galaxy::input::SchemeTuning& tuning = config.tune(pads[i].scheme);
        LivePad& live = out[i];
        live = {};
        live.sample = galaxy::input::switch_hid::pad_sample_from(
            pads[i].decoded, pads[i].scheme, pads[i].left, pads[i].right,
            tuning.stick_deadzone, 0.0f);
        const galaxy::input::StickVector right = galaxy::input::apply_radial_deadzone(
            live.sample.axes[2], live.sample.axes[3], tuning.pointer_deadzone);
        live.sample.axes[2] = right.x;
        live.sample.axes[3] = right.y;
        live.imu_live = pads[i].imu_live;
        const bool shaking = pads[i].shake_until_us != 0u && now_us < pads[i].shake_until_us;
        live.shake_remote = shaking && tuning.motion_shake == galaxy::input::MotionShake::Remote;
        live.shake_nunchuk = shaking && tuning.motion_shake == galaxy::input::MotionShake::Nunchuk;
        live.gyro_x = pads[i].gyro_x;
        live.gyro_y = pads[i].gyro_y;
        live.gravity_valid = pads[i].gravity_valid && pads[i].imu_live;
        live.gravity = pads[i].gravity;
        live.device_slot = pads[i].slot;
    }
    return count;
}

bool pad_has_input(const galaxy::input::PadSample& sample) {
    if (!sample.connected) {
        return false;
    }
    if (sample.buttons != 0u) {
        return true;
    }
    for (std::size_t i = 0; i < 4u; ++i) {
        if (std::fabs(sample.axes[i]) > 0.001f) {
            return true;
        }
    }
    return sample.axes[4] > 30.0f / 255.0f || sample.axes[5] > 30.0f / 255.0f;
}

// Runs the overlay chord and navigation over every connected pad at once and
// applies its hidden/neutral decision back to each pad.
bool route_native_settings_pads(std::span<LivePad> pads, bool capture_finished) {
    static std::uint16_t s_capture_hold = 0u;
    std::uint16_t word = 0u;
    float stick_x = 0.0f;
    float stick_y = 0.0f;
    for (const LivePad& pad : pads) {
        if (!pad.sample.connected) {
            continue;
        }
        word = static_cast<std::uint16_t>(
            word | galaxy::input::settings_pad_buttons(pad.sample.scheme, pad.sample.buttons));
        if (std::fabs(pad.sample.axes[0]) > std::fabs(stick_x)) stick_x = pad.sample.axes[0];
        if (std::fabs(pad.sample.axes[1]) > std::fabs(stick_y)) stick_y = pad.sample.axes[1];
    }
    // A capture's press stays away from the overlay's navigation until it is
    // released, and while capturing the pad reaches neither overlay nor game.
    if (capture_finished || g_binding_capture_armed.load(std::memory_order_acquire) != 0u) {
        s_capture_hold = word;
        for (LivePad& pad : pads) {
            pad.sample.buttons = 0u;
            pad.sample.axes = {};
        }
        return true;
    }
    s_capture_hold = static_cast<std::uint16_t>(s_capture_hold & word);
    word = static_cast<std::uint16_t>(word & ~s_capture_hold);
    const bool hold_neutral = s_capture_hold != 0u;
    const bool any_connected = std::any_of(pads.begin(), pads.end(),
        [](const LivePad& pad) { return pad.sample.connected; });
    if (!any_connected) {
        return false;
    }
    std::uint16_t game_word = word;
    const bool neutral = route_native_settings_pad(game_word, stick_x, stick_y) || hold_neutral;
    for (LivePad& pad : pads) {
        if (!pad.sample.connected) {
            continue;
        }
        const std::uint16_t own =
            galaxy::input::settings_pad_buttons(pad.sample.scheme, pad.sample.buttons);
        pad.sample.buttons = galaxy::input::unified_from_settings_pad(
            pad.sample.scheme, static_cast<std::uint16_t>(own & game_word),
            neutral ? 0u : pad.sample.buttons);
        if (neutral) {
            pad.sample.axes = {};
            pad.shake_remote = pad.shake_nunchuk = false;
            pad.gyro_x = pad.gyro_y = 0.0f;
        }
    }
    return neutral;
}

// Returns true on the report that completes a capture.
bool service_binding_capture(std::span<const LivePad> pads) {
    static galaxy::input::PadSample s_baseline{};
    static std::uint32_t s_baseline_for = 0u;
    const std::uint32_t armed = g_binding_capture_armed.load(std::memory_order_acquire);
    if (armed == 0u) {
        s_baseline_for = 0u;
        return false;
    }
    const auto scheme = capture_scheme_of(armed);
    const auto slot = static_cast<std::uint8_t>(armed >> 8u);
    const LivePad* match = nullptr;
    for (const LivePad& pad : pads) {
        if (pad.sample.connected && pad.sample.scheme == scheme &&
            (slot == galaxy::input::kKeyboardSlot || pad.device_slot == slot)) {
            match = &pad;
            break;
        }
    }
    if (match == nullptr) {
        return false;
    }
    if (s_baseline_for != armed) {
        s_baseline = match->sample;
        s_baseline_for = armed;
        return false;
    }
    const auto source = galaxy::input::detect_pad_capture(s_baseline, match->sample);
    s_baseline = match->sample;
    if (!source) {
        return false;
    }
    std::uint32_t expected = armed;
    if (!g_binding_capture_armed.compare_exchange_strong(expected, 0u, std::memory_order_acq_rel)) {
        return false;
    }
    g_binding_capture_result.store(
        0x10000u | (static_cast<std::uint32_t>(source->kind) << 8u) | source->code,
        std::memory_order_release);
    s_baseline_for = 0u;
    const NativeSettingsKeySink sink = g_native_settings_key_sink.load(std::memory_order_acquire);
    if (sink != nullptr) {
        sink(kBindingCaptureKey);
    }
    return true;
}

bool advance_nunchuk_shake_accel(
    galaxy::input::WiimoteInputSnapshot& snapshot,
    bool pressed,
    bool& previous_pressed,
    std::uint8_t& remaining_frames) {
    if (pressed && !previous_pressed) {
        remaining_frames = 12;
    }
    previous_pressed = pressed;
    if (remaining_frames == 0) {
        return false;
    }
    const bool phase = (remaining_frames & 1u) != 0u;
    snapshot.nunchuk.accel.x = phase ? 0x02f0u : 0x0110u;
    snapshot.nunchuk.accel.y = 0x0200u;
    snapshot.nunchuk.accel.z = phase ? 0x0130u : 0x02d0u;
    --remaining_frames;
    return true;
}

std::uint16_t wiimote_trace_button_bits(
    const galaxy::input::WiimoteButtons& buttons) {
    std::uint8_t byte0 = 0;
    std::uint8_t byte1 = 0;
    if (buttons.left) byte0 |= 0x01u;
    if (buttons.right) byte0 |= 0x02u;
    if (buttons.down) byte0 |= 0x04u;
    if (buttons.up) byte0 |= 0x08u;
    if (buttons.plus) byte0 |= 0x10u;
    if (buttons.two) byte1 |= 0x01u;
    if (buttons.one) byte1 |= 0x02u;
    if (buttons.b) byte1 |= 0x04u;
    if (buttons.a) byte1 |= 0x08u;
    if (buttons.minus) byte1 |= 0x10u;
    if (buttons.home) byte1 |= 0x80u;
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(byte0) << 8u) | byte1);
}

bool foreground_process_window(HWND& window) {
    window = GetForegroundWindow();
    if (window == nullptr || !IsWindow(window) || !IsWindowVisible(window)) {
        return false;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    return pid == GetCurrentProcessId();
}

bool current_process_window_under_cursor(POINT& cursor, HWND& window) {
    if (!GetCursorPos(&cursor)) {
        window = nullptr;
        return false;
    }
    HWND hovered = WindowFromPoint(cursor);
    if (hovered == nullptr) {
        window = nullptr;
        return false;
    }
    if (HWND root = GetAncestor(hovered, GA_ROOT); root != nullptr) {
        hovered = root;
    }
    if (!IsWindow(hovered) || !IsWindowVisible(hovered)) {
        window = nullptr;
        return false;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(hovered, &pid);
    if (pid != GetCurrentProcessId()) {
        window = nullptr;
        return false;
    }
    window = hovered;
    return true;
}

bool current_process_runtime_window(HWND& window) {
    struct SearchState {
        DWORD pid = GetCurrentProcessId();
        HWND window = nullptr;
        HWND fallback_window = nullptr;
        std::int64_t fallback_area = 0;
    } state{};

    EnumWindows(
        [](HWND candidate, LPARAM user) -> BOOL {
            auto* search = reinterpret_cast<SearchState*>(user);
            if (candidate == nullptr || !IsWindow(candidate)) {
                return TRUE;
            }
            DWORD pid = 0;
            GetWindowThreadProcessId(candidate, &pid);
            if (pid != search->pid) {
                return TRUE;
            }
            if (!IsWindowVisible(candidate)) {
                return TRUE;
            }
            RECT client{};
            if (!GetClientRect(candidate, &client) ||
                client.right <= client.left ||
                client.bottom <= client.top) {
                return TRUE;
            }
            const std::int64_t area =
                static_cast<std::int64_t>(client.right - client.left) *
                static_cast<std::int64_t>(client.bottom - client.top);
            if (area > search->fallback_area) {
                search->fallback_window = candidate;
                search->fallback_area = area;
            }
            wchar_t class_name[64]{};
            if (GetClassNameW(candidate, class_name,
                              static_cast<int>(
                                  sizeof(class_name) /
                                  sizeof(class_name[0]))) == 0 ||
                lstrcmpW(class_name, L"NebulaRuntimeWndR") != 0) {
                return TRUE;
            }
            search->window = candidate;
            return FALSE;
        },
        reinterpret_cast<LPARAM>(&state));

    window = state.window != nullptr ? state.window : state.fallback_window;
    return window != nullptr;
}

struct NativeInputScriptOverlay {
    galaxy::input::WiimoteButtons buttons{};
    bool nunchuk_c = false;
    bool nunchuk_z = false;
    bool has_stick = false;
    float stick_x = 0.0f;
    float stick_y = 0.0f;
    bool has_pointer = false;
    float pointer_x = 0.0f;
    float pointer_y = 0.0f;
    bool shake = false;
    std::uint32_t trace_mask = 0;
    // The long-form button script's contribution is kept separate from the independent one-shot
    // GALAXY_INPUT_AUTOPRESS_A* controls. The live Mario-control stop contract applies to this source
    // specifically, and its terminal guard must prove it contributed no A/B bits at or after the
    // published marker.
    std::uint32_t autopress_script_trace_mask = 0;
    bool autopress_script_stopped_at_mario_control = false;
    std::uint64_t autopress_script_stop_marker_vi = 0;
};

struct NativeInputReplaySample {
    std::uint64_t elapsed_ms = 0;
    bool game_focused = false;
    int mouse_client_x = 0;
    int mouse_client_y = 0;
    int client_w = 0;
    int client_h = 0;
    std::uint64_t keys_hold = 0;
    bool xinput_connected = false;
    std::uint16_t xinput_buttons = 0;
    std::uint8_t left_trigger = 0;
    std::uint8_t right_trigger = 0;
    std::int16_t thumb_lx = 0;
    std::int16_t thumb_ly = 0;
    std::int16_t thumb_rx = 0;
    std::int16_t thumb_ry = 0;
};

struct NativeInputReplayLog {
    std::string path;
    std::vector<NativeInputReplaySample> samples;
};

struct NativeInputReplayColumns {
    std::size_t elapsed_ms = 0;
    std::size_t game_focused = 0;
    std::size_t mouse_client_x = 0;
    std::size_t mouse_client_y = 0;
    std::size_t client_w = 0;
    std::size_t client_h = 0;
    std::size_t keys_hold_hex = 0;
    std::size_t xinput_connected = 0;
    std::size_t xinput_buttons_hex = 0;
    std::size_t left_trigger = 0;
    std::size_t right_trigger = 0;
    std::size_t thumb_lx = 0;
    std::size_t thumb_ly = 0;
    std::size_t thumb_rx = 0;
    std::size_t thumb_ry = 0;
};

std::optional<std::string> read_env_string_uncached(const char* name) {
    // Short values use stack storage; oversized values retain the checked
    // heap fallback.
    constexpr std::size_t kStackEnvironmentValueLength = 512u;
    // Read only the initialized length returned by getenv_s on success.
    // Unset/dynamic script values need no 512-byte zero-fill per query.
    char stack_buffer[kStackEnvironmentValueLength];
    std::size_t length = 0;
    const errno_t result =
        getenv_s(&length, stack_buffer, sizeof(stack_buffer), name);
    if (result == 0) {
        if (length <= 1u) {
            return std::nullopt;
        }
        return std::string(stack_buffer, length - 1u);
    }
    // The value exists but does not fit the stack buffer. getenv_s has already reported its size in
    // `length`, so one exactly sized allocation is made. A failure here can only mean "too long".
    std::vector<char> buffer(length + 1u);
    if (getenv_s(&length, buffer.data(), buffer.size(), name) != 0) {
        throw std::runtime_error(
            std::string("environment variable is too long: ") + name);
    }
    if (length <= 1u) {
        return std::nullopt;
    }
    return std::string(buffer.data(), length - 1u);
}

// The input poll reads its replay, script and stick configuration through this function several
// times per report, and getenv_s scans the whole environment block each time. Once the runtime
// has finished configuring (the same switch as the flag cache; tests keep live values because
// they never set it), a value is read once per literal name and served from this table.
struct EnvStringCacheSlot {
    std::atomic<const char*> name{nullptr};
    std::atomic<int> state{0};  // 0 filling, 1 absent, 2 present
    std::string value;
};
EnvStringCacheSlot g_env_string_cache[128];

std::optional<std::string> read_env_string(const char* name) {
    if (!g_env_flag_cache_enabled.load(std::memory_order_acquire)) {
        return read_env_string_uncached(name);
    }
    const auto key = reinterpret_cast<std::uintptr_t>(name);
    std::size_t index = static_cast<std::size_t>((key >> 3u) ^ (key >> 11u)) & 127u;
    for (std::size_t probe = 0; probe < 8u; ++probe, index = (index + 1u) & 127u) {
        EnvStringCacheSlot& slot = g_env_string_cache[index];
        const char* const owner = slot.name.load(std::memory_order_acquire);
        if (owner == name) {
            const int state = slot.state.load(std::memory_order_acquire);
            if (state == 1) return std::nullopt;
            if (state == 2) return slot.value;
            break;
        }
        if (owner == nullptr) {
            const char* expected = nullptr;
            if (!slot.name.compare_exchange_strong(expected, name, std::memory_order_acq_rel)) {
                if (expected == name) break;
                continue;
            }
            std::optional<std::string> value = read_env_string_uncached(name);
            if (value.has_value()) slot.value = *value;
            slot.state.store(value.has_value() ? 2 : 1, std::memory_order_release);
            return value;
        }
    }
    return read_env_string_uncached(name);
}

std::optional<std::uint64_t> read_env_u64_optional(const char* name) {
    const std::optional<std::string> value = read_env_string(name);
    if (!value.has_value()) {
        return std::nullopt;
    }
    std::uint64_t parsed = 0u;
    if (!galaxy::input::parse_native_input_script_u64(*value, parsed)) {
        throw std::runtime_error(
            std::string("invalid unsigned environment value for ") + name);
    }
    return parsed;
}

std::uint64_t parse_script_u64(
    const std::string& field,
    const char* script_name) {
    std::uint64_t parsed = 0u;
    if (!galaxy::input::parse_native_input_script_u64(field, parsed)) {
        throw std::runtime_error(
            std::string("invalid native input script field in ") +
            script_name);
    }
    return parsed;
}

float parse_script_float(const std::string& field, const char* script_name) {
    if (field.empty()) {
        throw std::runtime_error(
            std::string("invalid native input script field in ") +
            script_name);
    }
    char* end = nullptr;
    const float parsed = std::strtof(field.c_str(), &end);
    if (end == field.c_str() || *end != '\0' || !std::isfinite(parsed)) {
        throw std::runtime_error(
            std::string("invalid native input script field in ") +
            script_name);
    }
    return std::clamp(parsed, -1.0f, 1.0f);
}

std::optional<float> read_env_float_optional(const char* name) {
    const std::optional<std::string> value = read_env_string(name);
    if (!value.has_value()) {
        return std::nullopt;
    }
    return parse_script_float(*value, name);
}

float read_env_scale_or_default(const char* name, float default_value) {
    const std::optional<float> value = read_env_float_optional(name);
    if (!value.has_value()) {
        return default_value;
    }
    return std::clamp(std::fabs(*value), 0.05f, 1.0f);
}

float mouse_ir_absolute_x_scale() {
    static const float scale = read_env_scale_or_default(
        "GALAXY_MOUSE_IR_ABSOLUTE_X_SCALE", 1.0f);
    return scale;
}

float mouse_ir_absolute_y_scale() {
    static const float scale = read_env_scale_or_default(
        "GALAXY_MOUSE_IR_ABSOLUTE_Y_SCALE", 1.0f);
    return scale;
}

galaxy::input::NativeMousePointerProjection project_client_mouse_pointer(
    bool window_focused,
    bool absolute_valid,
    bool inside_client,
    int client_x,
    int client_y,
    int client_width,
    int client_height) {
    if (galaxy::experimental_dynamic_aspect_requested()) {
        const auto actual = galaxy::gx::current_host_client_extent();
        if (actual.width > 1 && actual.height > 1 &&
            (actual.width != client_width ||
             actual.height != client_height)) {
            // A pre-resize mouse snapshot must not be inverted through the
            // next window geometry while its VI aspect is being latched.
            return {};
        }
    }
    const auto content=galaxy::g_presentation_content.read();
    if (!content || content->surface_width!=client_width ||
        content->surface_height!=client_height) return {};
    const auto viewport=content->viewport;
    return galaxy::input::project_native_mouse_pointer({
        window_focused,
        absolute_valid,
        inside_client,
        client_x,
        client_y,
        client_width,
        client_height,
        viewport,
        mouse_ir_absolute_x_scale(),
        mouse_ir_absolute_y_scale(),
    });
}

std::vector<std::string> split_script_fields(
    const std::string& text,
    char delimiter) {
    std::vector<std::string> fields;
    std::stringstream stream(text);
    std::string field;
    while (std::getline(stream, field, delimiter)) {
        fields.push_back(field);
    }
    if (!text.empty() && text.back() == delimiter) {
        fields.emplace_back();
    }
    return fields;
}

constexpr std::string_view kNativeInputReplayKeyOrder =
    "LButton,RButton,Space,Enter,Shift,LShift,RShift,"
    "Left,Up,Right,Down,1,2,A,B,C,D,E,I,J,K,L,Q,R,S,W,Z,"
    "Ctrl,LCtrl,RCtrl";

std::size_t require_replay_column(
    const std::vector<std::string>& header,
    const char* name,
    const std::string& path) {
    const auto it = std::find(header.begin(), header.end(), name);
    if (it == header.end()) {
        throw std::runtime_error(
            "native input replay log is missing column '" +
            std::string(name) + "': " + path);
    }
    return static_cast<std::size_t>(std::distance(header.begin(), it));
}

NativeInputReplayColumns parse_replay_columns(
    const std::vector<std::string>& header,
    const std::string& path) {
    NativeInputReplayColumns columns{};
    columns.elapsed_ms = require_replay_column(header, "elapsed_ms", path);
    columns.game_focused =
        require_replay_column(header, "game_focused", path);
    columns.mouse_client_x =
        require_replay_column(header, "mouse_client_x", path);
    columns.mouse_client_y =
        require_replay_column(header, "mouse_client_y", path);
    columns.client_w = require_replay_column(header, "client_w", path);
    columns.client_h = require_replay_column(header, "client_h", path);
    columns.keys_hold_hex =
        require_replay_column(header, "keys_hold_hex", path);
    columns.xinput_connected =
        require_replay_column(header, "xinput_connected", path);
    columns.xinput_buttons_hex =
        require_replay_column(header, "xinput_buttons_hex", path);
    columns.left_trigger =
        require_replay_column(header, "left_trigger", path);
    columns.right_trigger =
        require_replay_column(header, "right_trigger", path);
    columns.thumb_lx = require_replay_column(header, "thumb_lx", path);
    columns.thumb_ly = require_replay_column(header, "thumb_ly", path);
    columns.thumb_rx = require_replay_column(header, "thumb_rx", path);
    columns.thumb_ry = require_replay_column(header, "thumb_ry", path);
    return columns;
}

void require_replay_field(
    const std::vector<std::string>& fields,
    std::size_t index,
    const std::string& path,
    std::size_t line_number) {
    if (index >= fields.size()) {
        std::ostringstream message;
        message << "native input replay log row " << line_number
                << " is missing fields: " << path;
        throw std::runtime_error(message.str());
    }
}

std::uint64_t parse_replay_u64(
    const std::string& field,
    const std::string& name,
    const std::string& path,
    std::size_t line_number,
    int base = 10) {
    if (field.empty() || field.find_first_not_of(" \t\r\n\v\f") == std::string::npos ||
        field[field.find_first_not_of(" \t\r\n\v\f")] == '-') {
        std::ostringstream message;
        message << "invalid native input replay " << name << " at "
                << path << ':' << line_number;
        throw std::runtime_error(message.str());
    }
    char* end = nullptr;
    errno = 0;
    const unsigned long long parsed =
        std::strtoull(field.c_str(), &end, base);
    if (errno == ERANGE || end == field.c_str() || *end != '\0') {
        std::ostringstream message;
        message << "invalid native input replay " << name << " at "
                << path << ':' << line_number;
        throw std::runtime_error(message.str());
    }
    return static_cast<std::uint64_t>(parsed);
}

std::int64_t parse_replay_i64(
    const std::string& field,
    const std::string& name,
    const std::string& path,
    std::size_t line_number) {
    if (field.empty()) {
        std::ostringstream message;
        message << "invalid native input replay " << name << " at "
                << path << ':' << line_number;
        throw std::runtime_error(message.str());
    }
    char* end = nullptr;
    errno = 0;
    const long long parsed = std::strtoll(field.c_str(), &end, 10);
    if (errno == ERANGE || end == field.c_str() || *end != '\0') {
        std::ostringstream message;
        message << "invalid native input replay " << name << " at "
                << path << ':' << line_number;
        throw std::runtime_error(message.str());
    }
    return static_cast<std::int64_t>(parsed);
}

int parse_replay_int(
    const std::string& field, const std::string& name,
    const std::string& path, std::size_t line_number) {
    const auto value = parse_replay_i64(field, name, path, line_number);
    if (value < std::numeric_limits<int>::min() ||
        value > std::numeric_limits<int>::max()) {
        throw std::runtime_error("native input replay " + name +
            " is out of range at " + path + ':' + std::to_string(line_number));
    }
    return static_cast<int>(value);
}

std::uint8_t parse_replay_u8(
    const std::string& field,
    const std::string& name,
    const std::string& path,
    std::size_t line_number) {
    const std::uint64_t parsed =
        parse_replay_u64(field, name, path, line_number);
    if (parsed > 0xffu) {
        std::ostringstream message;
        message << "native input replay " << name << " is out of range at "
                << path << ':' << line_number;
        throw std::runtime_error(message.str());
    }
    return static_cast<std::uint8_t>(parsed);
}

std::int16_t parse_replay_i16(
    const std::string& field,
    const std::string& name,
    const std::string& path,
    std::size_t line_number) {
    const std::int64_t parsed =
        parse_replay_i64(field, name, path, line_number);
    if (parsed < std::numeric_limits<std::int16_t>::min() ||
        parsed > std::numeric_limits<std::int16_t>::max()) {
        std::ostringstream message;
        message << "native input replay " << name << " is out of range at "
                << path << ':' << line_number;
        throw std::runtime_error(message.str());
    }
    return static_cast<std::int16_t>(parsed);
}

bool parse_replay_bool(
    const std::string& field,
    const std::string& name,
    const std::string& path,
    std::size_t line_number) {
    const std::uint64_t parsed =
        parse_replay_u64(field, name, path, line_number);
    if (parsed > 1u) {
        std::ostringstream message;
        message << "native input replay " << name << " must be 0 or 1 at "
                << path << ':' << line_number;
        throw std::runtime_error(message.str());
    }
    return parsed != 0;
}

std::string replay_field(
    const std::vector<std::string>& fields,
    std::size_t index,
    const std::string& path,
    std::size_t line_number) {
    require_replay_field(fields, index, path, line_number);
    return fields[index];
}

NativeInputReplaySample parse_replay_sample(
    const std::vector<std::string>& fields,
    const NativeInputReplayColumns& columns,
    const std::string& path,
    std::size_t line_number) {
    NativeInputReplaySample sample{};
    sample.elapsed_ms = parse_replay_u64(
        replay_field(fields, columns.elapsed_ms, path, line_number),
        "elapsed_ms",
        path,
        line_number);
    sample.game_focused = parse_replay_bool(
        replay_field(fields, columns.game_focused, path, line_number),
        "game_focused",
        path,
        line_number);
    sample.mouse_client_x = parse_replay_int(
        replay_field(fields, columns.mouse_client_x, path, line_number),
        "mouse_client_x",
        path,
        line_number);
    sample.mouse_client_y = parse_replay_int(
        replay_field(fields, columns.mouse_client_y, path, line_number),
        "mouse_client_y",
        path,
        line_number);
    sample.client_w = parse_replay_int(
        replay_field(fields, columns.client_w, path, line_number),
        "client_w",
        path,
        line_number);
    sample.client_h = parse_replay_int(
        replay_field(fields, columns.client_h, path, line_number),
        "client_h",
        path,
        line_number);
    sample.keys_hold = parse_replay_u64(
        replay_field(fields, columns.keys_hold_hex, path, line_number),
        "keys_hold_hex",
        path,
        line_number,
        0);
    sample.xinput_connected = parse_replay_bool(
        replay_field(fields, columns.xinput_connected, path, line_number),
        "xinput_connected",
        path,
        line_number);
    const std::uint64_t buttons = parse_replay_u64(
        replay_field(fields, columns.xinput_buttons_hex, path, line_number),
        "xinput_buttons_hex",
        path,
        line_number,
        0);
    if (buttons > 0xffffu) {
        std::ostringstream message;
        message << "native input replay xinput_buttons_hex is out of range at "
                << path << ':' << line_number;
        throw std::runtime_error(message.str());
    }
    sample.xinput_buttons = static_cast<std::uint16_t>(buttons);
    sample.left_trigger = parse_replay_u8(
        replay_field(fields, columns.left_trigger, path, line_number),
        "left_trigger",
        path,
        line_number);
    sample.right_trigger = parse_replay_u8(
        replay_field(fields, columns.right_trigger, path, line_number),
        "right_trigger",
        path,
        line_number);
    sample.thumb_lx = parse_replay_i16(
        replay_field(fields, columns.thumb_lx, path, line_number),
        "thumb_lx",
        path,
        line_number);
    sample.thumb_ly = parse_replay_i16(
        replay_field(fields, columns.thumb_ly, path, line_number),
        "thumb_ly",
        path,
        line_number);
    sample.thumb_rx = parse_replay_i16(
        replay_field(fields, columns.thumb_rx, path, line_number),
        "thumb_rx",
        path,
        line_number);
    sample.thumb_ry = parse_replay_i16(
        replay_field(fields, columns.thumb_ry, path, line_number),
        "thumb_ry",
        path,
        line_number);
    return sample;
}

NativeInputReplayLog load_native_input_replay_log(const std::string& path) {
    std::ifstream stream(path);
    if (!stream) {
        throw std::runtime_error(
            "failed to open native input replay log: " + path);
    }

    NativeInputReplayLog log{};
    log.path = path;
    bool saw_key_order = false;
    bool saw_header = false;
    NativeInputReplayColumns columns{};
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(stream, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line_number == 1 && line.size() >= 3u &&
            static_cast<unsigned char>(line[0]) == 0xefu &&
            static_cast<unsigned char>(line[1]) == 0xbbu &&
            static_cast<unsigned char>(line[2]) == 0xbfu) {
            line.erase(0, 3);
        }
        if (line.empty()) {
            continue;
        }
        const std::vector<std::string> fields = split_script_fields(line, '\t');
        if (!fields.empty() && fields[0] == "meta") {
            if (fields.size() >= 3u && fields[1] == "key_order") {
                if (fields[2] != kNativeInputReplayKeyOrder) {
                    throw std::runtime_error(
                        "native input replay log key_order is unsupported: " +
                        path);
                }
                saw_key_order = true;
            }
            continue;
        }
        if (!saw_header) {
            columns = parse_replay_columns(fields, path);
            saw_header = true;
            continue;
        }
        NativeInputReplaySample sample =
            parse_replay_sample(fields, columns, path, line_number);
        if (!log.samples.empty() &&
            sample.elapsed_ms < log.samples.back().elapsed_ms) {
            std::ostringstream message;
            message << "native input replay log elapsed_ms regressed at "
                    << path << ':' << line_number;
            throw std::runtime_error(message.str());
        }
        log.samples.push_back(sample);
    }
    if (!saw_key_order) {
        throw std::runtime_error(
            "native input replay log is missing key_order metadata: " + path);
    }
    if (!saw_header) {
        throw std::runtime_error(
            "native input replay log is missing a header row: " + path);
    }
    if (log.samples.empty()) {
        throw std::runtime_error(
            "native input replay log has no samples: " + path);
    }
    return log;
}

const NativeInputReplayLog* active_native_input_replay_log() {
    const std::optional<std::string> path =
        read_env_string("GALAXY_INPUT_REPLAY_LOG");
    static std::optional<std::string> s_loaded_path;
    static std::unique_ptr<NativeInputReplayLog> s_log;
    if (!path.has_value()) {
        s_loaded_path.reset();
        s_log.reset();
        return nullptr;
    }
    if (!s_loaded_path.has_value() || *s_loaded_path != *path) {
        NativeInputReplayLog loaded = load_native_input_replay_log(*path);
        s_log = std::make_unique<NativeInputReplayLog>(std::move(loaded));
        s_loaded_path = *path;
    }
    return s_log.get();
}

const NativeInputReplaySample* native_input_replay_sample_for_vi(
    std::uint64_t current_vi) {
    const NativeInputReplayLog* log = active_native_input_replay_log();
    if (log == nullptr) {
        return nullptr;
    }
    const auto seconds = current_vi / 60u;
    const auto fraction = (current_vi % 60u) * 1000u / 60u;
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    const std::uint64_t target_ms = seconds > (maximum - fraction) / 1000u
        ? maximum : seconds * 1000u + fraction;
    const auto it = std::upper_bound(
        log->samples.begin(),
        log->samples.end(),
        target_ms,
        [](std::uint64_t value, const NativeInputReplaySample& sample) {
            return value < sample.elapsed_ms;
        });
    if (it == log->samples.begin()) {
        return &log->samples.front();
    }
    return &*(it - 1);
}

bool replay_key_held(const NativeInputReplaySample& sample, int index) {
    return (sample.keys_hold & (std::uint64_t{1} << index)) != 0;
}

bool replay_pad_held(
    const NativeInputReplaySample& sample,
    std::uint16_t mask) {
    return sample.xinput_connected && (sample.xinput_buttons & mask) != 0u;
}

float replay_key_axis(
    const NativeInputReplaySample& sample,
    int positive,
    int negative) {
    float value = 0.0f;
    if (sample.game_focused && replay_key_held(sample, positive)) value += 1.0f;
    if (sample.game_focused && replay_key_held(sample, negative)) value -= 1.0f;
    return value;
}

bool replay_xinput_has_input(const NativeInputReplaySample& sample) {
    if (!sample.xinput_connected) {
        return false;
    }
    const std::uint8_t trigger_threshold = xinput_trigger_threshold();
    const std::int16_t left_deadzone = xinput_left_deadzone();
    const std::int16_t pointer_deadzone = xinput_pointer_deadzone();
    return sample.xinput_buttons != 0u ||
           sample.left_trigger >= trigger_threshold ||
           sample.right_trigger >= trigger_threshold ||
           std::fabs(galaxy::input::normalize_xinput_axis(
               sample.thumb_lx, left_deadzone)) >
               0.001f ||
           std::fabs(galaxy::input::normalize_xinput_axis(
               sample.thumb_ly, left_deadzone)) >
               0.001f ||
           std::fabs(galaxy::input::normalize_xinput_axis(
               sample.thumb_rx, pointer_deadzone)) >
               0.001f ||
           std::fabs(galaxy::input::normalize_xinput_axis(
               sample.thumb_ry, pointer_deadzone)) >
               0.001f;
}

ULONGLONG mouse_pointer_transient_loss_hold_ms() {
    const std::optional<std::uint64_t> override_ms =
        read_env_u64_optional("GALAXY_MOUSE_IR_HOLD_MS");
    if (!override_ms.has_value()) {
        return 5000;
    }
    return static_cast<ULONGLONG>(
        std::min<std::uint64_t>(*override_ms, 60000u));
}

ULONGLONG script_pointer_live_mouse_grace_ms() {
    const std::optional<std::uint64_t> override_ms =
        read_env_u64_optional("GALAXY_INPUT_SCRIPT_POINTER_LIVE_MOUSE_MS");
    if (!override_ms.has_value()) {
        return 250;
    }
    return static_cast<ULONGLONG>(
        std::min<std::uint64_t>(*override_ms, 5000u));
}

bool script_pointer_force_enabled() {
    return read_env_flag("GALAXY_INPUT_SCRIPT_POINTER_FORCE");
}

void apply_script_buttons_to_snapshot(
    galaxy::input::WiimoteInputSnapshot& snapshot,
    const NativeInputScriptOverlay& script) {
    snapshot.buttons.left = snapshot.buttons.left || script.buttons.left;
    snapshot.buttons.right = snapshot.buttons.right || script.buttons.right;
    snapshot.buttons.down = snapshot.buttons.down || script.buttons.down;
    snapshot.buttons.up = snapshot.buttons.up || script.buttons.up;
    snapshot.buttons.plus = snapshot.buttons.plus || script.buttons.plus;
    snapshot.buttons.minus = snapshot.buttons.minus || script.buttons.minus;
    snapshot.buttons.one = snapshot.buttons.one || script.buttons.one;
    snapshot.buttons.two = snapshot.buttons.two || script.buttons.two;
    snapshot.buttons.home = snapshot.buttons.home || script.buttons.home;
    snapshot.buttons.a = snapshot.buttons.a || script.buttons.a;
    snapshot.buttons.b = snapshot.buttons.b || script.buttons.b;
    snapshot.nunchuk.c = snapshot.nunchuk.c || script.nunchuk_c;
    snapshot.nunchuk.z = snapshot.nunchuk.z || script.nunchuk_z;
}

std::string uppercase_ascii(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(
            std::toupper(static_cast<unsigned char>(ch)));
    }
    return value;
}

bool vi_in_script_window(
    std::uint64_t vi,
    std::uint64_t start,
    std::uint64_t duration) {
    return duration != 0 && vi >= start && vi - start < duration;
}

std::uint32_t apply_script_button_token(
    NativeInputScriptOverlay& overlay,
    const std::string& token,
    const char* script_name) {
    const std::string name = uppercase_ascii(token);
    if (name == "A") {
        overlay.buttons.a = true;
        overlay.trace_mask |= 0x0001u;
        return 0x0001u;
    } else if (name == "B") {
        overlay.buttons.b = true;
        overlay.trace_mask |= 0x0002u;
        return 0x0002u;
    } else if (name == "PLUS" || name == "START") {
        overlay.buttons.plus = true;
        overlay.trace_mask |= 0x0004u;
        return 0x0004u;
    } else if (name == "MINUS" || name == "SELECT" || name == "BACK") {
        overlay.buttons.minus = true;
        overlay.trace_mask |= 0x0008u;
        return 0x0008u;
    } else if (name == "ONE" || name == "1") {
        overlay.buttons.one = true;
        overlay.trace_mask |= 0x0010u;
        return 0x0010u;
    } else if (name == "TWO" || name == "2") {
        overlay.buttons.two = true;
        overlay.trace_mask |= 0x0020u;
        return 0x0020u;
    } else if (name == "HOME") {
        overlay.buttons.home = true;
        overlay.trace_mask |= 0x0040u;
        return 0x0040u;
    } else if (name == "LEFT") {
        overlay.buttons.left = true;
        overlay.trace_mask |= 0x0080u;
        return 0x0080u;
    } else if (name == "RIGHT") {
        overlay.buttons.right = true;
        overlay.trace_mask |= 0x0100u;
        return 0x0100u;
    } else if (name == "UP") {
        overlay.buttons.up = true;
        overlay.trace_mask |= 0x0200u;
        return 0x0200u;
    } else if (name == "DOWN") {
        overlay.buttons.down = true;
        overlay.trace_mask |= 0x0400u;
        return 0x0400u;
    } else if (name == "C") {
        overlay.nunchuk_c = true;
        overlay.trace_mask |= 0x0800u;
        return 0x0800u;
    } else if (name == "Z") {
        overlay.nunchuk_z = true;
        overlay.trace_mask |= 0x1000u;
        return 0x1000u;
    } else if (name == "SHAKE") {
        overlay.shake = true;
        overlay.trace_mask |= 0x2000u;
        return 0x2000u;
    } else {
        throw std::runtime_error(
            std::string("unsupported native input script button '") + token +
            "' in " + script_name);
    }
}

void apply_script_buttons(
    NativeInputScriptOverlay& overlay,
    const std::string& text,
    const char* script_name,
    std::uint32_t* source_trace_mask = nullptr) {
    const std::vector<std::string> tokens = split_script_fields(text, '+');
    if (tokens.empty()) {
        throw std::runtime_error(
            std::string("empty native input script button list in ") +
            script_name);
    }
    for (const std::string& token : tokens) {
        if (token.empty()) {
            throw std::runtime_error(
                std::string("empty native input script button in ") +
                script_name);
        }
        const std::uint32_t applied_mask =
            apply_script_button_token(overlay, token, script_name);
        if (source_trace_mask != nullptr) {
            *source_trace_mask |= applied_mask;
        }
    }
}

void apply_autopress_env(
    NativeInputScriptOverlay& overlay,
    std::uint64_t current_vi,
    const char* env_name,
    const char* buttons,
    std::uint64_t duration) {
    const std::optional<std::uint64_t> start = read_env_u64_optional(env_name);
    if (start.has_value() &&
        vi_in_script_window(current_vi, *start, duration)) {
        apply_script_buttons(overlay, buttons, env_name);
    }
}

void apply_button_script_env(
    NativeInputScriptOverlay& overlay,
    std::uint64_t current_vi,
    std::optional<std::uint64_t> first_mario_control_vi,
    const char* env_name) {
    const std::optional<std::string> script = read_env_string(env_name);
    if (!script.has_value()) {
        return;
    }
    if (read_env_flag(
            "GALAXY_INPUT_AUTOPRESS_SCRIPT_STOP_AT_MARIO_CONTROL")) {
        // The translated Mario-control boundary is authoritative once it crosses the release/acquire
        // handoff. The absolute configured VI is only a pre-marker fallback; a stale or malformed fallback
        // is never parsed once the live marker exists.
        if (first_mario_control_vi.has_value()) {
            if (current_vi >= *first_mario_control_vi) {
                overlay.autopress_script_stopped_at_mario_control = true;
                overlay.autopress_script_stop_marker_vi =
                    *first_mario_control_vi;
                return;
            }
        } else {
            const std::optional<std::uint64_t> fallback_vi =
                read_env_u64_optional(
                    "GALAXY_INPUT_AUTOPRESS_SCRIPT_STOP_FALLBACK_VI");
            if (fallback_vi.has_value() && current_vi >= *fallback_vi) {
                return;
            }
        }
    }
    for (const std::string& segment :
         split_script_fields(*script, ';')) {
        if (segment.empty()) {
            continue;
        }
        const std::vector<std::string> fields =
            split_script_fields(segment, ':');
        if (fields.size() != 3u) {
            throw std::runtime_error(
                std::string("invalid native input button script segment in ") +
                env_name);
        }
        const std::uint64_t start = parse_script_u64(fields[0], env_name);
        const std::uint64_t duration = parse_script_u64(fields[1], env_name);
        if (vi_in_script_window(current_vi, start, duration)) {
            apply_script_buttons(
                overlay,
                fields[2],
                env_name,
                &overlay.autopress_script_trace_mask);
        }
    }
}

void apply_stick_script_env(
    NativeInputScriptOverlay& overlay,
    std::uint64_t current_vi,
    std::optional<std::uint64_t> first_mario_control_vi,
    const char* env_name) {
    const std::optional<std::string> script = read_env_string(env_name);
    if (!script.has_value()) {
        return;
    }
    std::uint64_t anchor = 0;
    if (read_env_flag("GALAXY_INPUT_STICK_SCRIPT_RELATIVE_MARIO_CONTROL")) {
        // The live route marker is authoritative once it crosses the release/acquire boundary. The
        // configured absolute VI is only a pre-marker fallback; it must never move a script back to an
        // older anchor after translated Mario-control code has been seen.
        if (first_mario_control_vi.has_value()) {
            anchor = *first_mario_control_vi;
        } else {
            anchor = read_env_u64_optional(
                         "GALAXY_INPUT_STICK_SCRIPT_RELATIVE_FALLBACK_VI")
                         .value_or(0);
        }
    }
    for (const std::string& segment :
         split_script_fields(*script, ';')) {
        if (segment.empty()) {
            continue;
        }
        const std::vector<std::string> fields =
            split_script_fields(segment, ':');
        if (fields.size() != 4u) {
            throw std::runtime_error(
                std::string("invalid native input stick script segment in ") +
                env_name);
        }
        std::uint64_t start = 0u;
        if (!galaxy::input::resolve_native_input_script_start_vi(
                anchor, fields[0], start)) {
            throw std::runtime_error(
                std::string(
                    "invalid or overflowing native input stick script start in ") +
                env_name);
        }
        const std::uint64_t duration = parse_script_u64(fields[1], env_name);
        if (vi_in_script_window(current_vi, start, duration)) {
            overlay.has_stick = true;
            overlay.stick_x = parse_script_float(fields[2], env_name);
            overlay.stick_y = parse_script_float(fields[3], env_name);
            overlay.trace_mask |= 0x4000u;
        }
    }
}

// Only explicitly enabled diagnostic runs perform file I/O. A file command contributes through the
// existing script overlay; physical/window ownership, raw HID transport, cadence, KPAD conversion
// and guest state are unchanged.
void apply_diagnostic_stick_command_file(NativeInputScriptOverlay& overlay) {
    static const std::optional<std::string> path =
        read_env_string("GALAXY_INPUT_STICK_COMMAND_FILE");
    if (!path.has_value()) return;

    static galaxy::input::DiagnosticStickLease lease;
    static bool polled = false;
    static std::uint64_t last_poll_ms = 0u;
    std::uint64_t now_ms = static_cast<std::uint64_t>(GetTickCount64());
    if (polled && now_ms < last_poll_ms) {
        lease.cancel();
        return;
    }
    if (!polled || now_ms - last_poll_ms >= 100u) {
        const auto file_started_ns = s_input_poll_breakdown_active ? input_poll_steady_ns() : 0u;
        polled = true;
        last_poll_ms = now_ms;
        const HANDLE file = CreateFileA(
            path->c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            lease.cancel();
        } else {
            // A bounded read also rejects a larger or partially written command. The parent publishes by
            // atomic replacement, never by truncation.
            std::array<char, 129> text{};
            DWORD count = 0u;
            const BOOL read = ReadFile(
                file, text.data(), static_cast<DWORD>(text.size()),
                &count, nullptr);
            CloseHandle(file);
            now_ms = static_cast<std::uint64_t>(GetTickCount64());
            if (read == FALSE || count == text.size()) {
                lease.cancel();
            } else {
                lease.observe(
                    std::string_view(text.data(), count), now_ms);
            }
        }
        if (file_started_ns != 0u) {
            const auto end = input_poll_steady_ns();
            if (end >= file_started_ns) s_input_poll_breakdown.diagnostic_file_ns += end - file_started_ns;
        }
    }
    if (const auto active = lease.active(now_ms); active.has_value()) {
        overlay.has_stick = true;
        overlay.stick_x = active->x;
        overlay.stick_y = active->y;
        overlay.trace_mask |= 0x4000u;
        overlay.buttons.a = overlay.buttons.a || active->a;
        if (active->a) overlay.trace_mask |= 0x0001u;
    }
}

void apply_shake_script_env(
    NativeInputScriptOverlay& overlay,
    std::uint64_t current_vi,
    const char* env_name) {
    const std::optional<std::string> script = read_env_string(env_name);
    if (!script.has_value()) {
        return;
    }
    for (const std::string& segment :
         split_script_fields(*script, ';')) {
        if (segment.empty()) {
            continue;
        }
        const std::vector<std::string> fields =
            split_script_fields(segment, ':');
        if (fields.size() != 2u) {
            throw std::runtime_error(
                std::string("invalid native input shake script segment in ") +
                env_name);
        }
        const std::uint64_t start = parse_script_u64(fields[0], env_name);
        const std::uint64_t duration = parse_script_u64(fields[1], env_name);
        if (vi_in_script_window(current_vi, start, duration)) {
            overlay.shake = true;
            overlay.trace_mask |= 0x2000u;
        }
    }
}

void apply_pointer_pair_env(
    NativeInputScriptOverlay& overlay,
    std::uint64_t current_vi,
    const char* x_env,
    const char* y_env,
    const char* vi_env,
    const char* duration_env = nullptr) {
    const std::optional<float> x = read_env_float_optional(x_env);
    const std::optional<float> y = read_env_float_optional(y_env);
    if (x.has_value() != y.has_value()) {
        throw std::runtime_error(
            std::string("native input pointer env pair is incomplete: ") +
            x_env + "/" + y_env);
    }
    if (!x.has_value()) {
        return;
    }
    if (vi_env != nullptr) {
        const std::optional<std::uint64_t> vi = read_env_u64_optional(vi_env);
        if (!vi.has_value()) {
            throw std::runtime_error(
                std::string("native input pointer waypoint is missing ") +
                vi_env);
        }
        if (current_vi < *vi) {
            return;
        }
        if (duration_env != nullptr) {
            const std::optional<std::uint64_t> duration =
                read_env_u64_optional(duration_env);
            if (duration.has_value() &&
                !vi_in_script_window(current_vi, *vi, *duration)) {
                return;
            }
        }
    }
    overlay.has_pointer = true;
    overlay.pointer_x = *x;
    overlay.pointer_y = *y;
    overlay.trace_mask |= 0x8000u;
}

void apply_pointer_sweep_env(
    NativeInputScriptOverlay& overlay,
    std::uint64_t current_vi) {
    const std::optional<std::uint64_t> start =
        read_env_u64_optional("GALAXY_INPUT_POINTER_SWEEP_VI");
    const std::optional<std::uint64_t> duration =
        read_env_u64_optional("GALAXY_INPUT_POINTER_SWEEP_DURATION");
    if (start.has_value() != duration.has_value()) {
        throw std::runtime_error(
            "native input pointer sweep requires both VI and duration");
    }
    if (!start.has_value()) {
        return;
    }
    if (!vi_in_script_window(current_vi, *start, *duration)) {
        return;
    }
    const float phase =
        *duration <= 1u
            ? 0.5f
            : static_cast<float>(current_vi - *start) /
                  static_cast<float>(*duration - 1u);
    overlay.has_pointer = true;
    overlay.pointer_x = std::clamp(phase * 2.0f - 1.0f, -1.0f, 1.0f);
    overlay.pointer_y = 0.0f;
    overlay.trace_mask |= 0x8000u;
}

void apply_pointer_script_envs(
    NativeInputScriptOverlay& overlay,
    std::uint64_t current_vi) {
    apply_pointer_pair_env(
        overlay,
        current_vi,
        "GALAXY_INPUT_POINTER_X",
        "GALAXY_INPUT_POINTER_Y",
        nullptr);
    // Only the names are immutable. Values are read at every sample, since diagnostic tools and tests
    // may update them; inactive waypoints keep their deferred duration validation and ordered
    // precedence.
    static constexpr std::array<std::array<const char*, 4>, 5> kWaypoints{{
        {"GALAXY_INPUT_POINTER2_X", "GALAXY_INPUT_POINTER2_Y",
         "GALAXY_INPUT_POINTER2_VI", "GALAXY_INPUT_POINTER2_DURATION"},
        {"GALAXY_INPUT_POINTER3_X", "GALAXY_INPUT_POINTER3_Y",
         "GALAXY_INPUT_POINTER3_VI", "GALAXY_INPUT_POINTER3_DURATION"},
        {"GALAXY_INPUT_POINTER4_X", "GALAXY_INPUT_POINTER4_Y",
         "GALAXY_INPUT_POINTER4_VI", "GALAXY_INPUT_POINTER4_DURATION"},
        {"GALAXY_INPUT_POINTER5_X", "GALAXY_INPUT_POINTER5_Y",
         "GALAXY_INPUT_POINTER5_VI", "GALAXY_INPUT_POINTER5_DURATION"},
        {"GALAXY_INPUT_POINTER6_X", "GALAXY_INPUT_POINTER6_Y",
         "GALAXY_INPUT_POINTER6_VI", "GALAXY_INPUT_POINTER6_DURATION"},
    }};
    for (const auto& waypoint : kWaypoints) {
        apply_pointer_pair_env(
            overlay, current_vi, waypoint[0], waypoint[1],
            waypoint[2], waypoint[3]);
    }
    apply_pointer_sweep_env(overlay, current_vi);
}

NativeInputScriptOverlay build_native_input_script_overlay(
    std::uint64_t current_vi,
    std::optional<std::uint64_t> first_mario_control_vi) {
    NativeInputScriptOverlay overlay{};
    const std::uint64_t a_duration =
        read_env_u64_optional("GALAXY_INPUT_AUTOPRESS_A_DURATION")
            .value_or(12);
    apply_autopress_env(
        overlay, current_vi, "GALAXY_INPUT_AUTOPRESS_AB", "A+B", a_duration);
    constexpr std::array<const char*, 8> kAWindows{
        "GALAXY_INPUT_AUTOPRESS_A",
        "GALAXY_INPUT_AUTOPRESS_A2",
        "GALAXY_INPUT_AUTOPRESS_A3",
        "GALAXY_INPUT_AUTOPRESS_A4",
        "GALAXY_INPUT_AUTOPRESS_A5",
        "GALAXY_INPUT_AUTOPRESS_A6",
        "GALAXY_INPUT_AUTOPRESS_A7",
        "GALAXY_INPUT_AUTOPRESS_A8"};
    for (const char* name : kAWindows) {
        apply_autopress_env(overlay, current_vi, name, "A", a_duration);
    }
    apply_button_script_env(
        overlay,
        current_vi,
        first_mario_control_vi,
        "GALAXY_INPUT_AUTOPRESS_SCRIPT");
    apply_stick_script_env(
        overlay,
        current_vi,
        first_mario_control_vi,
        "GALAXY_INPUT_STICK_SCRIPT");
    apply_diagnostic_stick_command_file(overlay);
    apply_shake_script_env(
        overlay, current_vi, "GALAXY_INPUT_SHAKE_SCRIPT");
    apply_pointer_script_envs(overlay, current_vi);
    return overlay;
}

// In a tilt section (tilt.h) the virtual remote's accelerometer reports the
// controller's tilt or the stick's; elsewhere it keeps its rest reading. A
// shake pulse applied afterwards still overrides it.
// GALAXY_TRACE_TILT=1 logs section changes and the reading twice a second.
void apply_tilt_accel(galaxy::input::WiimoteInputSnapshot& snapshot,
                      galaxy::input::tilt::Tilt& state,
                      galaxy::input::tilt::Section section,
                      const galaxy::input::tilt::Vec3* controller_up,
                      float stick_x,
                      float stick_y,
                      bool recenter,
                      std::uint64_t current_vi) {
    const galaxy::input::tilt::Section previous = state.section;
    const galaxy::input::tilt::Tilt::Output tilt =
        state.update(section, controller_up, stick_x, stick_y, recenter);
    static const bool trace = read_env_flag("GALAXY_TRACE_TILT");
    if (trace && (previous != state.section || (tilt.active && current_vi % 30u == 0u))) {
        std::cerr << "[tilt] vi=" << current_vi << " section=" << galaxy::input::tilt::section_name(state.section)
                  << " source=" << (tilt.from_stick ? "stick" : "motion") << " game=(" << tilt.game[0] << ','
                  << tilt.game[1] << ',' << tilt.game[2] << ")\n";
    }
    if (!tilt.active) {
        return;
    }
    const galaxy::input::tilt::Counts counts = galaxy::input::tilt::accel_counts(tilt.game);
    snapshot.accel.x = counts.x;
    snapshot.accel.y = counts.y;
    snapshot.accel.z = counts.z;
}

galaxy::input::WiimoteInputSnapshot build_replayed_hid_input_snapshot(
    std::uint8_t report_mode,
    std::uint64_t current_vi,
    const NativeInputReplaySample& replay,
    const NativeInputScriptOverlay& script,
    galaxy::RuntimeInputMode input_mode,
    bool ir_input_enabled,
    float& final_pointer_x,
    float& final_pointer_y,
    bool& final_pointer_active,
    galaxy::input::tilt::Section tilt_section) {
    galaxy::input::WiimoteInputSnapshot snapshot{};
    const bool replay_controller_active = replay_xinput_has_input(replay);
    const bool force_controller =
        input_mode == galaxy::RuntimeInputMode::Controller;
    const bool use_controller = force_controller ||
        (input_mode == galaxy::RuntimeInputMode::Auto &&
         replay_controller_active);
    const bool allow_absolute_pointer =
        input_mode != galaxy::RuntimeInputMode::RealWiimote;
    const char* input_source = use_controller ? "controller" : "keyboard_mouse";
    const std::uint8_t trigger_threshold = xinput_trigger_threshold();
    const std::int16_t left_deadzone = xinput_left_deadzone();
    const std::int16_t pointer_deadzone = xinput_pointer_deadzone();

    const auto pad = [&](std::uint16_t mask) {
        return replay_pad_held(replay, mask);
    };
    const auto key = [&](int index) {
        return replay.game_focused && replay_key_held(replay, index);
    };

    snapshot.buttons.left =
        use_controller ? pad(XINPUT_GAMEPAD_DPAD_LEFT) : key(7);
    snapshot.buttons.right =
        use_controller ? pad(XINPUT_GAMEPAD_DPAD_RIGHT) : key(9);
    snapshot.buttons.down =
        use_controller ? pad(XINPUT_GAMEPAD_DPAD_DOWN) : key(10);
    snapshot.buttons.up =
        use_controller ? pad(XINPUT_GAMEPAD_DPAD_UP) : key(8);
    snapshot.buttons.plus =
        use_controller ? pad(XINPUT_GAMEPAD_START) : false;
    snapshot.buttons.minus =
        use_controller ? pad(XINPUT_GAMEPAD_BACK) : false;
    snapshot.buttons.one = use_controller ? false : key(11);
    snapshot.buttons.two = use_controller ? false : key(12);
    snapshot.buttons.home = false;
    snapshot.buttons.a =
        use_controller ? pad(XINPUT_GAMEPAD_A) : (key(0) || key(2) || key(3));
    snapshot.buttons.b =
        use_controller
            ? (pad(XINPUT_GAMEPAD_B) ||
               replay.right_trigger >= trigger_threshold)
            : (key(1) || key(5) || key(6));

    float stick_x =
        use_controller
            ? galaxy::input::normalize_xinput_axis(
                  replay.thumb_lx, left_deadzone)
            : replay_key_axis(replay, 16, 13);
    float stick_y =
        use_controller
            ? galaxy::input::normalize_xinput_axis(
                  replay.thumb_ly, left_deadzone)
            : replay_key_axis(replay, 25, 24);
    if (script.has_stick) {
        stick_x = script.stick_x;
        stick_y = script.stick_y;
    }
    snapshot.nunchuk.stick_x =
        galaxy::input::native_stick_axis_byte(stick_x, 0x15, 0x7b, 0xe0);
    snapshot.nunchuk.stick_y =
        galaxy::input::native_stick_axis_byte(stick_y, 0x1d, 0x81, 0xe6);
    snapshot.nunchuk.c =
        use_controller ? pad(XINPUT_GAMEPAD_LEFT_THUMB) : key(15);
    snapshot.nunchuk.z =
        use_controller
            ? (pad(XINPUT_GAMEPAD_LEFT_SHOULDER) ||
               replay.left_trigger >= trigger_threshold)
            : (key(26) || key(27) || key(28) || key(29));

    apply_script_buttons_to_snapshot(snapshot, script);

    // The live pointer model (pointer_control.h) on the replay timeline: the
    // last moved source owns the pointer and dt comes from the VI clock, so a
    // recorded stick moves the pointer at the same speed at any report rate.
    static float s_replay_pointer_x = 0.0f;
    static float s_replay_pointer_y = 0.0f;
    static galaxy::input::PointerOwner s_replay_owner{};
    static std::uint64_t s_replay_pointer_vi = 0u;
    static int s_replay_mouse_x = -1;
    static int s_replay_mouse_y = -1;
    static galaxy::input::RecenterHold s_replay_recenter{};
    // Each replay log starts from a fresh pointer.
    static const void* s_replay_log_identity = nullptr;
    if (const void* log = active_native_input_replay_log(); log != s_replay_log_identity) {
        s_replay_log_identity = log;
        s_replay_pointer_x = s_replay_pointer_y = 0.0f;
        s_replay_owner = {};
        s_replay_pointer_vi = 0u;
        s_replay_mouse_x = s_replay_mouse_y = -1;
        s_replay_recenter = {};
    }
    const float dt = s_replay_pointer_vi == 0u || current_vi <= s_replay_pointer_vi
        ? 0.0f
        : std::min(static_cast<float>(current_vi - s_replay_pointer_vi) / 60.0f, 0.05f);
    s_replay_pointer_vi = current_vi;
    const std::uint64_t now_us = current_vi * 1'000'000ull / 60u;
    const bool keys_allowed = !force_controller;
    const bool pad_allowed = replay.xinput_connected &&
                             input_mode != galaxy::RuntimeInputMode::KeyboardMouse &&
                             input_mode != galaxy::RuntimeInputMode::RealWiimote;
    bool pointer_active = false;
    const char* pointer_source = "none";
    const bool replay_inside_client =
        replay.mouse_client_x >= 0 && replay.mouse_client_y >= 0 &&
        replay.mouse_client_x < replay.client_w &&
        replay.mouse_client_y < replay.client_h;
    const galaxy::input::NativeMousePointerProjection replay_pointer =
        project_client_mouse_pointer(
            replay.game_focused,
            replay.client_w > 1 && replay.client_h > 1,
            replay_inside_client,
            replay.mouse_client_x,
            replay.mouse_client_y,
            replay.client_w,
            replay.client_h);
    // A recorded mouse is replayed in every mode, as recorded.
    const bool mouse_visible = allow_absolute_pointer && replay_pointer.visible;
    const bool mouse_moved = mouse_visible &&
        (replay.mouse_client_x != s_replay_mouse_x || replay.mouse_client_y != s_replay_mouse_y);
    s_replay_mouse_x = replay.mouse_client_x;
    s_replay_mouse_y = replay.mouse_client_y;
    const galaxy::input::StickVector pointer_axes = pad_allowed
        ? galaxy::input::apply_radial_deadzone(
              galaxy::input::normalize_xinput_axis(replay.thumb_rx, 0),
              galaxy::input::normalize_xinput_axis(replay.thumb_ry, 0),
              static_cast<float>(pointer_deadzone) / 32767.0f)
        : galaxy::input::StickVector{};
    const galaxy::input::StickVector stick_velocity =
        galaxy::input::stick_pointer_velocity(pointer_axes, xinput_pointer_speed());
    const float key_x = keys_allowed ? replay_key_axis(replay, 21, 19) : 0.0f;
    const float key_y = keys_allowed ? replay_key_axis(replay, 18, 20) : 0.0f;
    galaxy::input::PointerActivity activity{};
    activity.mouse = mouse_moved;
    activity.keys = key_x != 0.0f || key_y != 0.0f;
    activity.stick = stick_velocity.x != 0.0f || stick_velocity.y != 0.0f;
    const bool controller_recenter =
        pad_allowed && replay_pad_held(replay, XINPUT_GAMEPAD_RIGHT_THUMB);
    const bool recenter_held = (keys_allowed && key(23)) || controller_recenter;
    const galaxy::input::RecenterHold::Output recenter = s_replay_recenter.update(recenter_held);
    if (recenter.pressed) {
        s_replay_owner.owner = controller_recenter ? galaxy::input::PointerSource::Stick
                                                   : galaxy::input::PointerSource::Keys;
    }
    if (recenter.pinned) {
        s_replay_owner.last_active_us = now_us;
    }
    switch (recenter.pinned ? s_replay_owner.owner : s_replay_owner.update(activity, now_us)) {
    case galaxy::input::PointerSource::Mouse:
        if (mouse_visible) {
            s_replay_pointer_x = replay_pointer.x;
            s_replay_pointer_y = replay_pointer.y;
            pointer_active = true;
            pointer_source = "replay";
        }
        break;
    case galaxy::input::PointerSource::Keys: {
        const float speed = keyboard_pointer_speed();
        s_replay_pointer_x = std::clamp(s_replay_pointer_x + key_x * speed * dt, -1.0f, 1.0f);
        s_replay_pointer_y = std::clamp(s_replay_pointer_y - key_y * speed * dt, -1.0f, 1.0f);
        pointer_active = true;
        pointer_source = "replay";
        break;
    }
    case galaxy::input::PointerSource::Stick:
        s_replay_pointer_x = std::clamp(s_replay_pointer_x + stick_velocity.x * dt, -1.0f, 1.0f);
        s_replay_pointer_y = std::clamp(s_replay_pointer_y - stick_velocity.y * dt, -1.0f, 1.0f);
        pointer_active = true;
        pointer_source = "controller";
        break;
    case galaxy::input::PointerSource::Gyro:
    case galaxy::input::PointerSource::None:
        if (pad_allowed) {
            pointer_active = true;
            pointer_source = "controller";
        }
        break;
    }
    if (recenter.pinned) {
        // Held recenter: the pointer stays at the centre (pointer_control.h).
        s_replay_pointer_x = 0.0f;
        s_replay_pointer_y = 0.0f;
        pointer_active = true;
        pointer_source = controller_recenter ? "controller" : "replay";
    }
    if (script.has_pointer) {
        s_replay_pointer_x = script.pointer_x;
        s_replay_pointer_y = script.pointer_y;
        pointer_active = true;
        pointer_source = "script";
    }
    const bool ir_active = pointer_active && ir_input_enabled;
    galaxy::input::publish_wiimote_ir_dots(
        snapshot, s_replay_pointer_x, s_replay_pointer_y, ir_active);
    final_pointer_x = s_replay_pointer_x;
    final_pointer_y = s_replay_pointer_y;
    final_pointer_active = pointer_active;

    // Tilt sections steer with the replayed stick (tilt.h).
    static galaxy::input::tilt::Tilt s_replay_tilt{};
    apply_tilt_accel(snapshot, s_replay_tilt, tilt_section, nullptr, stick_x, stick_y,
                     recenter.pressed, current_vi);

    const bool shake_pressed =
        script.shake ||
        (use_controller
             ? (pad(XINPUT_GAMEPAD_X) || pad(XINPUT_GAMEPAD_Y) ||
                pad(XINPUT_GAMEPAD_RIGHT_SHOULDER))
             : (key(17) || key(22)));
    static bool s_replay_previous_shake = false;
    static std::uint8_t s_replay_shake_frames = 0;
    const bool shake_active = galaxy::input::advance_wiimote_shake_accel(
        snapshot,
        shake_pressed,
        s_replay_previous_shake,
        s_replay_shake_frames);

    const std::uint16_t buttons = wiimote_trace_button_bits(snapshot.buttons);
    const bool trace_interesting =
        buttons != 0u ||
        script.trace_mask != 0u ||
        shake_active ||
        stick_x != 0.0f ||
        stick_y != 0.0f ||
        pointer_axes.x != 0.0f ||
        pointer_axes.y != 0.0f;
    if (trace_boot_input_sample_at(current_vi, trace_interesting)) {
        static std::uint64_t s_replay_input_trace_sample = 0;
        ++s_replay_input_trace_sample;
        std::ostringstream trace_line;
        trace_line << "[input-trace] boundary=bt-hid sample="
                  << s_replay_input_trace_sample
                  << " source=" << input_source
                  << " replay=1 replay-ms=" << replay.elapsed_ms
                  << " vi=" << current_vi
                  << " xinput=" << (replay.xinput_connected ? 1 : 0)
                  << " slot=" << (replay.xinput_connected ? 0 : -1)
                  << " report=0x" << std::hex
                  << static_cast<unsigned>(report_mode)
                  << " buttons=0x" << buttons
                  << " hold=0x" << buttons
                  << " script=0x" << script.trace_mask << std::dec
                  << " stick=(" << stick_x << ',' << stick_y << ')'
                  << " rstick=(" << pointer_axes.x << ',' << pointer_axes.y
                  << ')'
                  << " pointer=(" << s_replay_pointer_x << ','
                  << s_replay_pointer_y << ')'
                  << " ptrsrc=" << pointer_source
                  << " irdots=("
                  << (snapshot.ir[0].visible ? 1 : 0) << ','
                  << snapshot.ir[0].x << ',' << snapshot.ir[0].y << ','
                  << static_cast<unsigned>(snapshot.ir[0].size) << ';'
                  << (snapshot.ir[1].visible ? 1 : 0) << ','
                  << snapshot.ir[1].x << ',' << snapshot.ir[1].y << ','
                  << static_cast<unsigned>(snapshot.ir[1].size) << ')'
                  << " ir=" << (ir_active ? 1 : 0)
                  << " shake=" << (shake_active ? 1 : 0)
                  << " nunchuk=("
                  << static_cast<unsigned>(snapshot.nunchuk.stick_x) << ','
                  << static_cast<unsigned>(snapshot.nunchuk.stick_y) << ')'
                  << '\n';
        std::cerr << trace_line.str();
    }

    return snapshot;
}

struct NativeHidHostPointerMetadata {
    std::uint64_t input_mode_generation{};
    std::uint64_t sequence{};
    std::uint64_t acquired_ms{};
    bool sampled{};
    // Forces the source and encoded-wire records for one newly selected local cursor-poll publication,
    // even when compact tracing's periodic interval would miss it. Diagnostic only: report contents
    // and delivery cadence are unchanged.
    bool trace_selected_cursor_poll_event{};
    HostPointerSequenceDomain sequence_domain =
        HostPointerSequenceDomain::None;
    // Replay is an explicit diagnostic source, not host-window input. The origin is retained only for
    // the optional benchmark-end transport accounting.
    bool replay_source{};
    std::uint32_t autopress_script_trace_mask{};
    bool autopress_script_stopped_at_mario_control{};
    std::uint64_t autopress_script_stop_marker_vi{};
    // Final normalized ([-1,1]) pointer position this sample published into
    // the IR dots via publish_wiimote_ir_dots, and whether it is currently
    // treated as active (on-screen / hold-visible). A KPADRead host
    // intercept reads these directly instead of reconstructing a position
    // from the synthesized IR dot geometry.
    float pointer_x{};
    float pointer_y{};
    bool pointer_active{};
    bool refresh_mouse_allowed{};
    SyntheticKpadPointerOrigin pointer_origin{SyntheticKpadPointerOrigin::None};
    std::uint32_t owned_mouse_buttons{};
    std::uint32_t non_mouse_buttons{};
};

galaxy::input::WiimoteInputSnapshot build_native_hid_input_snapshot(
    std::uint8_t report_mode,
    std::uint64_t current_vi,
    const galaxy::RuntimeInputModeState& input_mode_state,
    std::optional<std::uint64_t> first_mario_control_vi,
    bool ir_input_enabled = true,
    NativeHidHostPointerMetadata* host_pointer_metadata = nullptr,
    galaxy::input::PlayContext play_context = galaxy::input::PlayContext::Play,
    galaxy::input::tilt::Section tilt_section = galaxy::input::tilt::Section::None) {
    if (host_pointer_metadata != nullptr) {
        *host_pointer_metadata = {};
        host_pointer_metadata->input_mode_generation =
            input_mode_state.generation;
    }
    galaxy::input::WiimoteInputSnapshot snapshot{};
    input_poll_diagnostic_stage(NativeInputPollStage::Script);
    const NativeInputScriptOverlay script =
        build_native_input_script_overlay(
            current_vi, first_mario_control_vi);
    if (host_pointer_metadata != nullptr) {
        host_pointer_metadata->autopress_script_trace_mask =
            script.autopress_script_trace_mask;
        host_pointer_metadata->autopress_script_stopped_at_mario_control =
            script.autopress_script_stopped_at_mario_control;
        host_pointer_metadata->autopress_script_stop_marker_vi =
            script.autopress_script_stop_marker_vi;
    }
    if (const NativeInputReplaySample* replay =
            native_input_replay_sample_for_vi(current_vi);
        replay != nullptr) {
        float replay_pointer_x = 0.0f;
        float replay_pointer_y = 0.0f;
        bool replay_pointer_active = false;
        // Replayed pads reach the settings overlay the way live pads do.
        NativeInputReplaySample routed = *replay;
        if (routed.xinput_connected &&
            route_native_settings_pad(
                routed.xinput_buttons,
                galaxy::input::normalize_xinput_axis(
                    routed.thumb_lx, xinput_left_deadzone()),
                galaxy::input::normalize_xinput_axis(
                    routed.thumb_ly, xinput_left_deadzone()))) {
            routed.left_trigger = routed.right_trigger = 0u;
            routed.thumb_lx = routed.thumb_ly = 0;
            routed.thumb_rx = routed.thumb_ry = 0;
        }
        const auto replay_snapshot = build_replayed_hid_input_snapshot(
            report_mode,
            current_vi,
            routed,
            script,
            input_mode_state.mode,
            ir_input_enabled,
            replay_pointer_x,
            replay_pointer_y,
            replay_pointer_active,
            tilt_section);
        if (host_pointer_metadata != nullptr) {
            host_pointer_metadata->replay_source = true;
            host_pointer_metadata->pointer_origin = SyntheticKpadPointerOrigin::Replay;
            host_pointer_metadata->pointer_x = replay_pointer_x;
            host_pointer_metadata->pointer_y = replay_pointer_y;
            host_pointer_metadata->pointer_active = replay_pointer_active;
            // Replay owns this point. Physical acquisition identity stays unset and refresh_mouse_allowed
            // false, so KPAD cannot replace it with a later live cursor sample. The HID boundary supplies
            // guest ticks.
        }
        return replay_snapshot;
    }

    input_poll_diagnostic_stage(NativeInputPollStage::Focus);
    // A hidden diagnostic window cannot be an interactive keyboard/mouse source. Controller, replay and
    // script acquisition stay intact, but no User32 focus, key or cursor calls are issued on the
    // real-time HID consumer. The wrapper treats GALAXY_HIDE_WINDOW as immutable process policy.
    static const bool host_window_input_enabled =
        !read_env_flag("GALAXY_HIDE_WINDOW");
    HWND focused_window = nullptr;
    const bool window_focused =
        host_window_input_enabled &&
        foreground_process_window(focused_window);
    const bool native_keyboard_captured =
        host_window_input_enabled && native_keyboard_capture_active();
    if (!window_focused || native_keyboard_captured) {
        clear_native_window_key_presses();
    }
    input_poll_diagnostic_stage(NativeInputPollStage::Keys);
    const LiveControls& controls = live_controls();
    const galaxy::input::BindingTable& keyboard_table =
        controls.config.table(galaxy::input::ControlScheme::Keyboard);
    galaxy::input::KeyboardMouseLevels keyboard_levels{};
    if (window_focused && !native_keyboard_captured) {
        for (std::size_t index = 0; index < controls.keyboard_keys.count; ++index) {
            const std::uint8_t vk = controls.keyboard_keys.keys[index];
            const bool pressed = g_window_key_presses.consume(vk);
            keyboard_levels.set_key(vk, pressed || g_window_keys_held.held(vk));
        }
        // Middle and side mouse buttons arrive as window key levels.
        const auto mouse_key = [](std::uint32_t vk) {
            return g_window_key_presses.consume(vk) || g_window_keys_held.held(vk);
        };
        if (mouse_key(VK_MBUTTON)) keyboard_levels.mouse |= 1u << 2u;
        if (mouse_key(VK_XBUTTON1)) keyboard_levels.mouse |= 1u << 3u;
        if (mouse_key(VK_XBUTTON2)) keyboard_levels.mouse |= 1u << 4u;
    }

    const galaxy::RuntimeInputMode input_mode = input_mode_state.mode;
    // Auto: keyboard, mouse and controllers together. The two overrides keep
    // one source; the real Wii Remote mode uses neither.
    const bool use_keyboard = input_mode == galaxy::RuntimeInputMode::Auto ||
                              input_mode == galaxy::RuntimeInputMode::KeyboardMouse;
    input_poll_diagnostic_stage(NativeInputPollStage::Controller);
    const std::uint32_t capture_armed = g_binding_capture_armed.load(std::memory_order_acquire);
    const galaxy::input::ControlScheme capture_scheme = capture_scheme_of(capture_armed);
    const bool capturing = capture_armed != 0u;
    const bool controller_mode_polls =
        (input_mode == galaxy::RuntimeInputMode::Controller ||
         input_mode == galaxy::RuntimeInputMode::Auto) &&
        (!xinput_requires_focus() || window_focused);
    const bool poll_controller =
        (controller_mode_polls || (capturing && capture_scheme == galaxy::input::ControlScheme::Xbox)) &&
        !xinput_polling_disabled();
    const galaxy::input::SchemeTuning& xbox_tuning =
        controls.config.tune(galaxy::input::ControlScheme::Xbox);
    // Every XInput pad is read; the right stick arrives raw and the pointer
    // takes a radial dead zone.
    std::array<NativeHidControllerState, XUSER_MAX_COUNT> xinput_pads{};
    const std::size_t xinput_count = poll_controller
        ? poll_native_xinput_pads(
              static_cast<std::int16_t>(read_env_int_setting(
                  "GALAXY_XINPUT_LEFT_DEADZONE", deadzone_counts(xbox_tuning.stick_deadzone), 0, 32767)),
              xinput_pads)
        : 0u;
    if (poll_controller) {
        std::uint32_t xinput_mask = 0u;
        for (std::size_t i = 0; i < xinput_count; ++i) xinput_mask |= 1u << xinput_pads[i].slot;
        g_xinput_mask.store(xinput_mask, std::memory_order_release);
        g_xinput_looked_ms.store(GetTickCount64(), std::memory_order_relaxed);
    }
    NativeHidControllerState controller = xinput_count != 0u ? xinput_pads[0] : NativeHidControllerState{};
    const float xinput_pointer_deadzone_fraction =
        static_cast<float>(read_env_int_setting(
            "GALAXY_XINPUT_POINTER_DEADZONE", deadzone_counts(xbox_tuning.pointer_deadzone), 0, 32767)) /
        32767.0f;
    std::array<LivePad, XUSER_MAX_COUNT + kMaxHidPads> pads{};
    std::size_t pad_count = 0;
    for (std::size_t i = 0; i < std::max<std::size_t>(xinput_count, 1u); ++i) {
        pads[pad_count].device_slot =
            i < xinput_count ? static_cast<std::uint8_t>(xinput_pads[i].slot) : std::uint8_t{0xFF};
        pads[pad_count++].sample = pad_sample_from_xinput(
            i < xinput_count ? xinput_pads[i] : NativeHidControllerState{},
            xinput_pointer_deadzone_fraction);
    }
    if (controller_mode_polls || (capturing && galaxy::input::scheme_uses_hid(capture_scheme))) {
        ensure_controller_hid_started();
        std::array<LivePad, kMaxHidPads> hid_pads{};
        const std::size_t hid_count = collect_hid_pads(controls.config, hid_pads);
        for (std::size_t i = 0; i < hid_count; ++i) {
            pads[pad_count++] = hid_pads[i];
        }
    }
    const std::span<LivePad> live_pads(pads.data(), pad_count);
    // Before the overlay takes the pads: the controller the player is using, for the CONTROLS page.
    for (const LivePad& pad : live_pads) {
        if (!pad.sample.connected) continue;
        if (pad_has_input(pad.sample) || pad.shake_remote || pad.shake_nunchuk ||
            std::fabs(pad.gyro_x) + std::fabs(pad.gyro_y) > 0.003f) {
            g_last_input.store((static_cast<std::uint32_t>(pad.sample.scheme) << 8u) | pad.device_slot,
                               std::memory_order_relaxed);
        }
    }
    const bool capture_finished = service_binding_capture(live_pads);
    (void)route_native_settings_pads(live_pads, capture_finished);
    if (!controller_mode_polls) {
        for (LivePad& pad : live_pads) {
            pad = {};
        }
    }
    controller.connected = std::any_of(live_pads.begin(), live_pads.end(),
        [](const LivePad& pad) { return pad.sample.connected; });
    const bool use_pads = controller_mode_polls && controller.connected;
    // With a controller attached, keys and mouse input synthesised by SendInput would double every
    // press and drag the pointer: Steam's desktop controller layout turns the same buttons and right
    // stick into keys and mouse motion. Only physical keyboard and mouse input counts then.
    const bool drop_injected = use_pads && input_mode == galaxy::RuntimeInputMode::Auto;
    if (drop_injected) {
        for (std::size_t index = 0; index < controls.keyboard_keys.count; ++index) {
            const std::uint8_t vk = controls.keyboard_keys.keys[index];
            if (native_window_key_injected(vk)) keyboard_levels.set_key(vk, false);
        }
        for (const std::uint32_t vk : {VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2}) {
            if (native_window_key_injected(vk)) {
                keyboard_levels.mouse &= static_cast<std::uint8_t>(
                    ~(1u << (vk == VK_MBUTTON ? 2u : vk == VK_XBUTTON1 ? 3u : 4u)));
            }
        }
    }
    const bool allow_absolute_pointer =
        host_window_input_enabled && use_keyboard &&
        input_mode != galaxy::RuntimeInputMode::RealWiimote;
    const char* input_source = "keyboard_mouse";
    if (input_mode == galaxy::RuntimeInputMode::Controller) {
        input_source =
            controller.connected ? "controller" : "controller_disconnected";
    } else if (use_pads) {
        input_source = "auto";
    }
    const float trigger_threshold =
        static_cast<float>(xinput_trigger_threshold()) / 255.0f;

    // Each scheme's bindings turn its physical inputs into Wii targets. Pad
    // and keyboard pointer axes stay apart: they are different pointer
    // sources (velocity from the stick, keys at the keyboard speed).
    galaxy::input::LogicalInput logic{};
    galaxy::input::LogicalInput pad_logic_all{};
    bool motion_shake_remote = false;
    bool motion_shake_nunchuk = false;
    float gyro_x = 0.0f;
    float gyro_y = 0.0f;
    float pad_pointer_speed = 0.0f;
    bool pad_gyro_pointing = false;
    // Rest-hide tuning of the pad that points (gyro first, then the stick).
    float pointer_hide_seconds = 0.0f;
    float pointer_hide_radius = galaxy::input::kDefaultPointerHideRadius;
    static std::array<std::uint32_t, XUSER_MAX_COUNT + kMaxHidPads> s_pad_previous_pressed{};
    // The controller in use: the last one with any input (buttons, sticks, triggers, a gyro turn, a
    // shake). A key or mouse button bound to a Wii input hands control to the keyboard. In the tilt
    // sections (tilt.h) its tilt steers when it has motion sensors (Pro Controller 1/2); otherwise the
    // stick steers (Xbox, keyboard). Vibration goes to it.
    constexpr std::uint32_t kDrivingNone = 0xFFFFFFFFu;
    constexpr std::uint32_t kDrivingKeyboard = 0xFFFFFFFEu;
    static std::uint32_t s_driving_pad = kDrivingNone;  // (scheme << 8) | device slot
    const auto pad_key = [](const LivePad& pad) {
        return (static_cast<std::uint32_t>(pad.sample.scheme) << 8u) | pad.device_slot;
    };
    galaxy::input::tilt::Vec3 tilt_up_value{0.0f, 1.0f, 0.0f};
    const galaxy::input::tilt::Vec3* tilt_up = nullptr;
    if (use_pads) {
        const LivePad* driving = nullptr;
        const LivePad* first_connected = nullptr;
        for (const LivePad& pad : live_pads) {
            if (!pad.sample.connected) continue;
            if (first_connected == nullptr) first_connected = &pad;
            const bool moved = pad_has_input(pad.sample) || pad.shake_remote || pad.shake_nunchuk ||
                               std::fabs(pad.gyro_x) + std::fabs(pad.gyro_y) > 0.003f;
            if (moved) s_driving_pad = pad_key(pad);
            if (pad_key(pad) == s_driving_pad) driving = &pad;
        }
        if (driving == nullptr && s_driving_pad != kDrivingKeyboard) driving = first_connected;
        if (driving != nullptr && driving->gravity_valid) {
            tilt_up_value = driving->gravity;
            tilt_up = &tilt_up_value;
        }
        for (std::size_t index = 0; index < live_pads.size(); ++index) {
            const LivePad& pad = live_pads[index];
            if (!pad.sample.connected) {
                s_pad_previous_pressed[index] = 0u;
                continue;
            }
            const galaxy::input::SchemeTuning& tuning = controls.config.tune(pad.sample.scheme);
            const bool gyro_pointing = tuning.gyro_pointer && pad.imu_live;
            galaxy::input::LogicalInput pad_logic = galaxy::input::evaluate_pad_bindings(
                controls.config.table(pad.sample.scheme), pad.sample, trigger_threshold,
                s_pad_previous_pressed[index], gyro_pointing);
            s_pad_previous_pressed[index] = pad_logic.pressed;
            const float speed = pad.sample.scheme == galaxy::input::ControlScheme::Xbox
                ? xinput_pointer_speed_or(tuning.pointer_speed) : tuning.pointer_speed;
            if (tuning.invert_pointer_y) pad_logic.pointer_y = -pad_logic.pointer_y;
            if (std::fabs(pad_logic.pointer_x) + std::fabs(pad_logic.pointer_y) >
                std::fabs(pad_logic_all.pointer_x) + std::fabs(pad_logic_all.pointer_y)) {
                pad_pointer_speed = speed;
            }
            if (!pad_gyro_pointing) {
                pointer_hide_seconds = tuning.pointer_hide_seconds;
                pointer_hide_radius = tuning.pointer_hide_radius;
            }
            pad_logic_all = galaxy::input::merge_logical_input(pad_logic_all, pad_logic);
            motion_shake_remote = motion_shake_remote || pad.shake_remote;
            motion_shake_nunchuk = motion_shake_nunchuk || pad.shake_nunchuk;
            if (gyro_pointing) {
                pad_gyro_pointing = true;
                pointer_hide_seconds = tuning.pointer_hide_seconds;
                pointer_hide_radius = tuning.pointer_hide_radius;
                gyro_x += pad.gyro_x;
                gyro_y += tuning.invert_pointer_y ? -pad.gyro_y : pad.gyro_y;
            }
        }
    } else {
        s_pad_previous_pressed = {};
    }
    galaxy::input::LogicalInput keyboard_logic{};
    if (use_keyboard) {
        keyboard_logic = galaxy::input::evaluate_keyboard_bindings(keyboard_table, keyboard_levels);
        if (controls.config.tune(galaxy::input::ControlScheme::Keyboard).invert_pointer_y) {
            keyboard_logic.pointer_y = -keyboard_logic.pointer_y;
        }
        if (keyboard_logic.pressed != 0u || keyboard_logic.stick_x != 0.0f || keyboard_logic.stick_y != 0.0f) {
            s_driving_pad = kDrivingKeyboard;
        }
    }
    // Vibration: target and strength, published with the game's motor bit.
    g_rumble_host = {};
    g_rumble_host.focused = window_focused;
    if (use_pads && s_driving_pad != kDrivingNone && s_driving_pad != kDrivingKeyboard) {
        const auto scheme = static_cast<galaxy::input::ControlScheme>((s_driving_pad >> 8u) & 0xFFu);
        const auto slot = static_cast<std::uint8_t>(s_driving_pad & 0xFFu);
        if (scheme == galaxy::input::ControlScheme::Xbox && slot < XUSER_MAX_COUNT) {
            g_rumble_host.target = {RumbleTarget::Kind::XInput, slot};
        } else if (galaxy::input::scheme_uses_hid(scheme) && slot < kMaxHidPads) {
            g_rumble_host.target = {RumbleTarget::Kind::Hid, slot, scheme};
        }
        if (static_cast<std::size_t>(scheme) < galaxy::input::kControlSchemeCount) {
            const galaxy::input::SchemeTuning& tuning = controls.config.tune(scheme);
            g_rumble_host.strength = tuning.vibration ? tuning.rumble : std::uint8_t{0};
        }
    }
    logic = galaxy::input::merge_logical_input(pad_logic_all, keyboard_logic);
    const float pad_pointer_x = pad_logic_all.pointer_x;
    const float pad_pointer_y = pad_logic_all.pointer_y;
    const float keyboard_pointer_x = keyboard_logic.pointer_x;
    const float keyboard_pointer_y = keyboard_logic.pointer_y;
    const auto held = [&logic](galaxy::input::WiiTarget target) {
        return logic.held(target);
    };
    using galaxy::input::WiiTarget;
    snapshot.buttons.left = held(WiiTarget::DpadLeft) || script.buttons.left;
    snapshot.buttons.right = held(WiiTarget::DpadRight) || script.buttons.right;
    snapshot.buttons.down = held(WiiTarget::DpadDown) || script.buttons.down;
    snapshot.buttons.up = held(WiiTarget::DpadUp) || script.buttons.up;
    snapshot.buttons.plus = held(WiiTarget::Plus) || script.buttons.plus;
    snapshot.buttons.minus = held(WiiTarget::Minus) || script.buttons.minus;
    snapshot.buttons.one = held(WiiTarget::One) || script.buttons.one;
    snapshot.buttons.two = held(WiiTarget::Two) || script.buttons.two;
    snapshot.buttons.home = held(WiiTarget::Home) || script.buttons.home;
    // B on the controllers: jump while Mario is under control, back in menus.
    static galaxy::input::JumpBackLatch s_jump_back{};
    const galaxy::input::JumpBackLatch::Output jump_back =
        s_jump_back.update(held(WiiTarget::JumpBack), play_context);
    snapshot.buttons.a = held(WiiTarget::A) || jump_back.a || script.buttons.a;
    snapshot.buttons.b = held(WiiTarget::B) || jump_back.b || script.buttons.b;

    float stick_x = logic.stick_x;
    float stick_y = logic.stick_y;
    if (script.has_stick) {
        stick_x = script.stick_x;
        stick_y = script.stick_y;
    }
    snapshot.nunchuk.stick_x =
        galaxy::input::native_stick_axis_byte(stick_x, 0x15, 0x7b, 0xe0);
    snapshot.nunchuk.stick_y =
        galaxy::input::native_stick_axis_byte(stick_y, 0x1d, 0x81, 0xe6);
    snapshot.nunchuk.c = held(WiiTarget::C) || script.nunchuk_c;
    snapshot.nunchuk.z = held(WiiTarget::Z) || script.nunchuk_z;

    if (host_pointer_metadata != nullptr) {
        host_pointer_metadata->non_mouse_buttons =
            galaxy::input::synthetic_kpad_button_mask(snapshot);
    }

    static float s_pointer_x = 0.0f;
    static float s_pointer_y = 0.0f;
    static float s_mouse_x = 0.0f;
    static float s_mouse_y = 0.0f;
    static ULONGLONG s_last_mouse_inside_tick = 0;
    static ULONGLONG s_last_live_mouse_tick = 0;
    static std::uint64_t s_last_host_pointer_absolute_sequence = 0;
    static std::uint64_t s_last_host_pointer_transition_sequence = 0;
    static bool s_mouse_pointer_seen_inside = false;
    const ULONGLONG mouse_pointer_hold_ms =
        mouse_pointer_transient_loss_hold_ms();
    const ULONGLONG live_mouse_script_grace_ms =
        script_pointer_live_mouse_grace_ms();
    const std::uint64_t now = host_pointer_now_ms();

    bool pointer_active = false;
    bool live_mouse_recent = false;
    const char* pointer_source = "none";
    // The mouse this report: where it points, whether that is on the picture
    // and whether it moved (a new physical sample inside the client).
    bool mouse_visible = false;
    bool mouse_moved = false;
    bool mouse_button_held = false;
    const char* mouse_source = "mouse";
    const bool drop_injected_mouse = drop_injected && native_pointer_input_injected(false);
    input_poll_diagnostic_stage(NativeInputPollStage::Pointer);
    HostPointerState traced_host_pointer{};
    bool traced_host_pointer_polled = false;
    if (allow_absolute_pointer) {
        const HostPointerProvider pointer_provider =
            g_host_pointer_provider.load(std::memory_order_acquire);
        if (pointer_provider != nullptr) {
            HostPointerState host_pointer = pointer_provider();
            const HostPointerButtonSample button_sample =
                consume_host_pointer_button_transitions(
                    host_pointer,
                    use_keyboard &&
                        !(drop_injected && native_pointer_input_injected(true)));
            std::uint64_t selected_pointer_sequence =
                host_pointer.absolute_sequence;
            std::uint64_t selected_pointer_acquired_ms =
                host_pointer.absolute_acquired_ms;
            HostPointerSequenceDomain selected_pointer_sequence_domain =
                host_pointer.absolute_sequence != 0u
                ? HostPointerSequenceDomain::AbsolutePosition
                : HostPointerSequenceDomain::None;
            host_pointer.left_button = button_sample.left_button;
            host_pointer.right_button = button_sample.right_button;
            if (button_sample.has_transition_coordinates) {
                host_pointer.absolute_valid =
                    button_sample.transition_coordinates.absolute_valid;
                host_pointer.inside_client =
                    button_sample.transition_coordinates.inside_client;
                host_pointer.client_x =
                    button_sample.transition_coordinates.client_x;
                host_pointer.client_y =
                    button_sample.transition_coordinates.client_y;
                host_pointer.client_width =
                    button_sample.transition_coordinates.client_width;
                host_pointer.client_height =
                    button_sample.transition_coordinates.client_height;
                host_pointer.absolute_acquired_ms =
                    button_sample.transition_absolute_acquired_ms;
                selected_pointer_sequence =
                    button_sample.transition_sequence;
                selected_pointer_acquired_ms =
                    button_sample.transition_absolute_acquired_ms;
                selected_pointer_sequence_domain =
                    HostPointerSequenceDomain::ButtonTransition;
                host_pointer.debug_flags |= 1u << 15u;
            }
            traced_host_pointer = host_pointer;
            traced_host_pointer_polled = true;
            if (host_pointer_metadata != nullptr) {
                host_pointer_metadata->sequence =
                    selected_pointer_sequence;
                host_pointer_metadata->acquired_ms =
                    selected_pointer_acquired_ms;
                host_pointer_metadata->sampled = true;
                host_pointer_metadata->sequence_domain =
                    selected_pointer_sequence_domain;
            }
            const HostPointerIrAvailabilityDecision pointer_availability =
                select_host_pointer_ir_availability(
                    host_pointer.window_focused,
                    host_pointer.absolute_valid,
                    host_pointer.inside_client);
            const galaxy::input::NativeMousePointerProjection
                projected_host_pointer = project_client_mouse_pointer(
                    host_pointer.window_focused,
                    host_pointer.absolute_valid,
                    host_pointer.inside_client,
                    host_pointer.client_x,
                    host_pointer.client_y,
                    host_pointer.client_width,
                    host_pointer.client_height);
            if (use_keyboard &&
                (host_pointer.left_button || host_pointer.right_button)) {
                POINT cursor{};
                HWND hovered_window = nullptr;
                HWND game_window = reinterpret_cast<HWND>(
                    host_pointer.debug_window);
                if (game_window != nullptr) {
                    if (HWND root = GetAncestor(game_window, GA_ROOT);
                        root != nullptr) {
                        game_window = root;
                    }
                }
                const bool game_window_under_cursor =
                    game_window != nullptr &&
                    current_process_window_under_cursor(
                        cursor, hovered_window) &&
                    hovered_window == game_window;
                if (select_host_pointer_mouse_button_delivery(
                        host_pointer.window_focused,
                        host_pointer.absolute_valid,
                        host_pointer.inside_client,
                        projected_host_pointer.visible,
                        game_window_under_cursor)) {
                    const std::uint32_t mouse_targets = galaxy::input::mouse_bound_targets(
                        keyboard_table,
                        static_cast<std::uint8_t>((host_pointer.left_button ? 1u : 0u) |
                                                  (host_pointer.right_button ? 2u : 0u)));
                    galaxy::input::WiimoteInputSnapshot mouse_only{};
                    const auto mouse = [mouse_targets](WiiTarget target) {
                        return (mouse_targets & galaxy::input::wii_target_bit(target)) != 0u;
                    };
                    mouse_only.buttons.a = mouse(WiiTarget::A);
                    mouse_only.buttons.b = mouse(WiiTarget::B);
                    mouse_only.buttons.one = mouse(WiiTarget::One);
                    mouse_only.buttons.two = mouse(WiiTarget::Two);
                    mouse_only.buttons.plus = mouse(WiiTarget::Plus);
                    mouse_only.buttons.minus = mouse(WiiTarget::Minus);
                    mouse_only.buttons.home = mouse(WiiTarget::Home);
                    mouse_only.buttons.up = mouse(WiiTarget::DpadUp);
                    mouse_only.buttons.down = mouse(WiiTarget::DpadDown);
                    mouse_only.buttons.left = mouse(WiiTarget::DpadLeft);
                    mouse_only.buttons.right = mouse(WiiTarget::DpadRight);
                    mouse_only.nunchuk.c = mouse(WiiTarget::C);
                    mouse_only.nunchuk.z = mouse(WiiTarget::Z);
                    snapshot.buttons.a = snapshot.buttons.a || mouse_only.buttons.a;
                    snapshot.buttons.b = snapshot.buttons.b || mouse_only.buttons.b;
                    snapshot.buttons.one = snapshot.buttons.one || mouse_only.buttons.one;
                    snapshot.buttons.two = snapshot.buttons.two || mouse_only.buttons.two;
                    snapshot.buttons.plus = snapshot.buttons.plus || mouse_only.buttons.plus;
                    snapshot.buttons.minus = snapshot.buttons.minus || mouse_only.buttons.minus;
                    snapshot.buttons.home = snapshot.buttons.home || mouse_only.buttons.home;
                    snapshot.buttons.up = snapshot.buttons.up || mouse_only.buttons.up;
                    snapshot.buttons.down = snapshot.buttons.down || mouse_only.buttons.down;
                    snapshot.buttons.left = snapshot.buttons.left || mouse_only.buttons.left;
                    snapshot.buttons.right = snapshot.buttons.right || mouse_only.buttons.right;
                    snapshot.nunchuk.c = snapshot.nunchuk.c || mouse_only.nunchuk.c;
                    snapshot.nunchuk.z = snapshot.nunchuk.z || mouse_only.nunchuk.z;
                    logic.pressed |= mouse_targets;
                    if (host_pointer_metadata != nullptr) {
                        host_pointer_metadata->owned_mouse_buttons =
                            galaxy::input::synthetic_kpad_button_mask(mouse_only);
                    }
                }
            }
            const bool host_pointer_inside_client =
                pointer_availability.acquired_inside_available &&
                projected_host_pointer.visible;
            const bool host_pointer_absolute_sequence_changed =
                host_pointer.absolute_sequence != 0u &&
                host_pointer.absolute_sequence !=
                    s_last_host_pointer_absolute_sequence;
            const bool host_pointer_transition_sequence_changed =
                button_sample.has_transition_coordinates &&
                button_sample.transition_sequence != 0u &&
                button_sample.transition_sequence !=
                    s_last_host_pointer_transition_sequence;
            const bool host_pointer_has_fresh_coordinates =
                button_sample.has_transition_coordinates
                ? host_pointer_transition_sequence_changed
                : host_pointer_absolute_sequence_changed;
            // Fresh physical movement inside the client takes precedence over a non-forced script, even when
            // the game window is unfocused. Button ownership stays guarded separately by window focus.
            mouse_button_held = host_pointer.window_focused &&
                                (host_pointer.left_button || host_pointer.right_button);
            if ((host_pointer_inside_client &&
                 host_pointer_has_fresh_coordinates) ||
                mouse_button_held) {
                s_last_live_mouse_tick = now;
            }
            if (button_sample.has_transition_coordinates &&
                button_sample.transition_sequence != 0u) {
                s_last_host_pointer_transition_sequence =
                    button_sample.transition_sequence;
            } else if (host_pointer.absolute_sequence != 0u) {
                s_last_host_pointer_absolute_sequence =
                    host_pointer.absolute_sequence;
            }
            live_mouse_recent =
                s_last_live_mouse_tick != 0 && now >= s_last_live_mouse_tick &&
                now - s_last_live_mouse_tick <= live_mouse_script_grace_ms;
            if (host_pointer_inside_client) {
                s_mouse_x = projected_host_pointer.x;
                s_mouse_y = projected_host_pointer.y;
                // Do not extend the invalid-sample grace on every HID poll of the same coordinate. Only a genuinely
                // new inside sample, or a queued button edge carrying its own coordinates, may re-anchor bounded
                // uncertainty.
                if (host_pointer_has_fresh_coordinates) {
                    s_last_mouse_inside_tick = now;
                    s_mouse_pointer_seen_inside = true;
                    mouse_moved = !drop_injected_mouse;
                }
                mouse_visible = true;
                mouse_source = "mouse";
            } else if (pointer_availability.invalid_sample_hold_allowed &&
                       s_mouse_pointer_seen_inside &&
                       now >= s_last_mouse_inside_tick &&
                       now - s_last_mouse_inside_tick <=
                           mouse_pointer_hold_ms) {
                // Remote Desktop can transiently fail absolute cursor sampling while the visual pointer is still
                // over the client. An invalid host sample counts as uncertainty, not as sensor-bar loss.
                mouse_visible = true;
                mouse_source = "mouse-hold-invalid";
            }
        }
    }
    if (allow_absolute_pointer && !mouse_visible &&
        !traced_host_pointer_polled) {
        POINT cursor{};
        HWND pointer_window = focused_window;
        bool pointer_window_active =
            window_focused && pointer_window != nullptr;
        if (!pointer_window_active) {
            pointer_window_active =
                current_process_window_under_cursor(cursor, pointer_window);
        } else if (!GetCursorPos(&cursor)) {
            pointer_window_active = false;
        }
        if (!pointer_window_active &&
            current_process_runtime_window(pointer_window) &&
            GetCursorPos(&cursor)) {
            pointer_window_active = true;
        }
        RECT client{};
        if (pointer_window_active &&
            ScreenToClient(pointer_window, &cursor) &&
            GetClientRect(pointer_window, &client)) {
            const int width = client.right - client.left;
            const int height = client.bottom - client.top;
            if (width > 1 && height > 1) {
                // The wiimote IR pointer tracks only while the host mouse is over the app's client area. In
                // fullscreen the client rect spans the screen, so this is always true. In a window the pointer
                // drops out when the cursor leaves, as a real wiimote does when it loses the sensor bar.
                const bool inside_client =
                    cursor.x >= 0 && cursor.y >= 0 &&
                    cursor.x < width && cursor.y < height;
                if (!inside_client) {
                    pointer_window_active = false;
                }
            }
            const galaxy::input::NativeMousePointerProjection
                projected_cursor = project_client_mouse_pointer(
                    pointer_window_active,
                    pointer_window_active && width > 1 && height > 1,
                    pointer_window_active,
                    static_cast<int>(cursor.x),
                    static_cast<int>(cursor.y),
                    width,
                    height);
            if (projected_cursor.visible) {
                mouse_moved = projected_cursor.x != s_mouse_x || projected_cursor.y != s_mouse_y;
                s_mouse_x = projected_cursor.x;
                s_mouse_y = projected_cursor.y;
                mouse_visible = true;
                mouse_source = "mouse";
            }
        }
    }

    input_poll_diagnostic_stage(NativeInputPollStage::PointerRemainder);
    // One pointer: the source that moved last owns it (pointer_control.h).
    static galaxy::input::PointerOwner s_pointer_owner{};
    static std::uint64_t s_pointer_clock_us = 0u;
    const std::uint64_t now_us = controller_hid_now_us();
    const float dt = s_pointer_clock_us == 0u || now_us <= s_pointer_clock_us
        ? 0.0f
        : std::min(static_cast<float>(now_us - s_pointer_clock_us) * 1e-6f, 0.05f);
    s_pointer_clock_us = now_us;
    const galaxy::input::StickVector stick_velocity = galaxy::input::stick_pointer_velocity(
        {pad_pointer_x, pad_pointer_y}, pad_pointer_speed);
    const float keyboard_speed = keyboard_pointer_speed_or(
        controls.config.tune(galaxy::input::ControlScheme::Keyboard).pointer_speed);
    // The gyro counts as moving above about 5 degrees per second, so hand
    // tremor and rest noise never take the pointer from the mouse or stick.
    constexpr float kGyroActiveUnitsPerSecond = 0.25f;
    const float gyro_motion = std::sqrt(gyro_x * gyro_x + gyro_y * gyro_y);
    galaxy::input::PointerActivity activity{};
    activity.mouse = mouse_moved;
    activity.keys = std::fabs(keyboard_pointer_x) > 0.001f || std::fabs(keyboard_pointer_y) > 0.001f;
    activity.stick = stick_velocity.x != 0.0f || stick_velocity.y != 0.0f;
    activity.gyro = pad_gyro_pointing && dt > 0.0f &&
                    gyro_motion > kGyroActiveUnitsPerSecond * dt;
    // Recenter (pointer_control.h RecenterHold): the press gives the pointer to the recentering device
    // at the picture centre. While the button is held, the pointer stays there and that device keeps it.
    const bool recenter_held = held(WiiTarget::PointerRecenter);
    static galaxy::input::RecenterHold s_recenter{};
    const galaxy::input::RecenterHold::Output recenter = s_recenter.update(recenter_held);
    if (recenter.pressed) {
        const bool pad_recenter = pad_logic_all.held(WiiTarget::PointerRecenter);
        s_pointer_owner.owner = pad_recenter
            ? (pad_gyro_pointing ? galaxy::input::PointerSource::Gyro
                                 : galaxy::input::PointerSource::Stick)
            : galaxy::input::PointerSource::Keys;
    }
    if (recenter.pinned) {
        s_pointer_owner.last_active_us = now_us;
    }
    const galaxy::input::PointerSource owner = recenter.pinned
        ? s_pointer_owner.owner
        : s_pointer_owner.update(activity, now_us);
    switch (owner) {
    case galaxy::input::PointerSource::Mouse:
        if (mouse_visible) {
            s_pointer_x = s_mouse_x;
            s_pointer_y = s_mouse_y;
            pointer_active = true;
            pointer_source = mouse_source;
        }
        break;
    case galaxy::input::PointerSource::Keys:
        s_pointer_x = std::clamp(s_pointer_x + keyboard_pointer_x * keyboard_speed * dt,
                                 -galaxy::input::kStickPointerLimit, galaxy::input::kStickPointerLimit);
        s_pointer_y = std::clamp(s_pointer_y - keyboard_pointer_y * keyboard_speed * dt,
                                 -galaxy::input::kStickPointerLimit, galaxy::input::kStickPointerLimit);
        pointer_active = true;
        pointer_source = "keyboard_mouse";
        break;
    case galaxy::input::PointerSource::Stick:
        s_pointer_x = std::clamp(s_pointer_x + stick_velocity.x * dt,
                                 -galaxy::input::kStickPointerLimit, galaxy::input::kStickPointerLimit);
        s_pointer_y = std::clamp(s_pointer_y - stick_velocity.y * dt,
                                 -galaxy::input::kStickPointerLimit, galaxy::input::kStickPointerLimit);
        pointer_active = true;
        pointer_source = "controller";
        break;
    case galaxy::input::PointerSource::Gyro:
        s_pointer_x = std::clamp(s_pointer_x + gyro_x,
                                 -galaxy::input::kGyroPointerLimit, galaxy::input::kGyroPointerLimit);
        s_pointer_y = std::clamp(s_pointer_y - gyro_y,
                                 -galaxy::input::kGyroPointerLimit, galaxy::input::kGyroPointerLimit);
        // Pointed past the picture the game hides the pointer, as with a
        // Wii Remote that loses the sensor bar.
        pointer_active = std::fabs(s_pointer_x) <= galaxy::input::kPointerVisibleLimit &&
                         std::fabs(s_pointer_y) <= galaxy::input::kPointerVisibleLimit;
        pointer_source = "controller";
        break;
    case galaxy::input::PointerSource::None:
        break;
    }
    if (recenter.pinned) {
        // Gyro and stick motion while the button is held is discarded.
        s_pointer_x = 0.0f;
        s_pointer_y = 0.0f;
        pointer_active = true;
        pointer_source = owner == galaxy::input::PointerSource::Keys ? "keyboard_mouse" : "controller";
    }
    // A connected controller keeps the pointer on screen until the mouse
    // owns it.
    if (!pointer_active && use_pads && owner != galaxy::input::PointerSource::Mouse &&
        owner != galaxy::input::PointerSource::Gyro) {
        pointer_active = true;
        pointer_source = "controller";
    }
    // Still-pointer hiding (pointer_control.h PointerRestHide): a hidden gyro
    // pointer comes back only with recenter; a stick pointer also with a push
    // or a move out of the radius.
    static galaxy::input::PointerRestHide s_rest_hide{};
    const bool controller_owned = owner == galaxy::input::PointerSource::Stick ||
                                  owner == galaxy::input::PointerSource::Gyro ||
                                  (owner == galaxy::input::PointerSource::None && use_pads);
    const bool rest_hidden = s_rest_hide.update(
        s_pointer_x, s_pointer_y, now_us, controller_owned,
        recenter.pinned || recenter.released ||
            (owner == galaxy::input::PointerSource::Stick && activity.stick),
        owner == galaxy::input::PointerSource::Gyro,
        pointer_hide_seconds, pointer_hide_radius);
    if (rest_hidden) {
        pointer_active = false;
        pointer_source = "controller-rest-hidden";
    }
    // In menus a key press acts on the pointer's position, so a resting mouse pointer stays.
    static galaxy::input::MouseRestHide s_mouse_rest_hide{};
    if (s_mouse_rest_hide.update(owner == galaxy::input::PointerSource::Mouse && pointer_active && window_focused &&
                                     play_context == galaxy::input::PlayContext::Play,
                                 mouse_moved || mouse_button_held, now_us)) {
        pointer_active = false;
        pointer_source = "mouse-rest-hidden";
    }
    if (script.has_pointer &&
        (script_pointer_force_enabled() || !live_mouse_recent ||
         !pointer_active)) {
        s_pointer_x = script.pointer_x;
        s_pointer_y = script.pointer_y;
        pointer_active = true;
        pointer_source = "script";
    }
    const bool ir_active = pointer_active && ir_input_enabled;
    galaxy::input::publish_wiimote_ir_dots(
        snapshot, s_pointer_x, s_pointer_y, ir_active);
    if (host_pointer_metadata != nullptr) {
        host_pointer_metadata->pointer_x = s_pointer_x;
        host_pointer_metadata->pointer_y = s_pointer_y;
        host_pointer_metadata->pointer_active = pointer_active;
        if (std::strcmp(pointer_source, "mouse") == 0 ||
            std::strcmp(pointer_source, "mouse-hold-invalid") == 0) {
            host_pointer_metadata->pointer_origin = SyntheticKpadPointerOrigin::PhysicalMouse;
        } else if (std::strcmp(pointer_source, "keyboard_mouse") == 0) {
            host_pointer_metadata->pointer_origin = SyntheticKpadPointerOrigin::KeyboardPointer;
        } else if (std::strcmp(pointer_source, "controller") == 0) {
            host_pointer_metadata->pointer_origin = SyntheticKpadPointerOrigin::Controller;
        } else if (std::strcmp(pointer_source, "script") == 0) {
            host_pointer_metadata->pointer_origin = SyntheticKpadPointerOrigin::Script;
        }
        // No configured pointer script may lose authority at this new seam,
        // even if recent physical movement won its existing producer policy.
        host_pointer_metadata->refresh_mouse_allowed =
            use_keyboard && allow_absolute_pointer && !script.has_pointer &&
            owner == galaxy::input::PointerSource::Mouse && !recenter_held &&
            (host_pointer_metadata->pointer_origin == SyntheticKpadPointerOrigin::PhysicalMouse ||
             host_pointer_metadata->pointer_origin == SyntheticKpadPointerOrigin::None);
    }
    const galaxy::input::StickVector pointer_axes{
        owner == galaxy::input::PointerSource::Keys ? keyboard_pointer_x : pad_pointer_x,
        owner == galaxy::input::PointerSource::Keys ? keyboard_pointer_y : pad_pointer_y};

    static galaxy::input::tilt::Tilt s_tilt{};
    apply_tilt_accel(snapshot, s_tilt, tilt_section, tilt_up, stick_x, stick_y, recenter.pressed, current_vi);

    const bool shake_pressed =
        script.shake || held(WiiTarget::Shake) || motion_shake_remote;
    static bool s_previous_shake = false;
    static std::uint8_t s_shake_frames = 0;
    if (shake_pressed && !s_previous_shake) {
        // Each shake the virtual remote reports, by its source (the session log beside [controller-motion]).
        std::cerr << "[input-shake] vi=" << current_vi << " source="
                  << (motion_shake_remote ? "motion" : held(WiiTarget::Shake) ? "binding" : "script")
                  << " tilt-section=" << galaxy::input::tilt::section_name(tilt_section) << '\n';
    }
    const bool shake_active = galaxy::input::advance_wiimote_shake_accel(
        snapshot, shake_pressed, s_previous_shake, s_shake_frames);
    static bool s_previous_nunchuk_shake = false;
    static std::uint8_t s_nunchuk_shake_frames = 0;
    (void)advance_nunchuk_shake_accel(
        snapshot, held(WiiTarget::NunchukShake) || motion_shake_nunchuk,
        s_previous_nunchuk_shake, s_nunchuk_shake_frames);

    const std::uint16_t buttons = wiimote_trace_button_bits(snapshot.buttons);
    const bool trace_interesting =
        buttons != 0u ||
        script.trace_mask != 0u ||
        shake_active ||
        stick_x != 0.0f ||
        stick_y != 0.0f ||
        pointer_axes.x != 0.0f ||
        pointer_axes.y != 0.0f ||
        // A renderer-owned pointer sample is useful evidence even without an active coordinate. Full input
        // traces must record an authoritative focus or outside-client loss rather than show only the
        // preceding visible point. Compact proof mode still ignores this broad path and stays rate-limited.
        traced_host_pointer_polled;
    constexpr std::uint32_t kCursorPollRequiredDebugMask =
        (1u << 9u) | (1u << 10u) | (1u << 11u) | (1u << 16u);
    constexpr std::uint32_t kCursorPollForbiddenDebugMask =
        (1u << 14u) | (1u << 17u);
    bool trace_selected_cursor_poll_event = false;
    if (host_pointer_metadata != nullptr &&
        trace_boot_input_at(current_vi) &&
        host_pointer_metadata->sampled &&
        host_pointer_metadata->sequence_domain ==
            HostPointerSequenceDomain::AbsolutePosition &&
        host_pointer_metadata->sequence != 0u &&
        host_pointer_metadata->acquired_ms != 0u &&
        ir_active && std::strcmp(pointer_source, "mouse") == 0 &&
        (traced_host_pointer.debug_flags & kCursorPollRequiredDebugMask) ==
            kCursorPollRequiredDebugMask &&
        (traced_host_pointer.debug_flags & kCursorPollForbiddenDebugMask) == 0u) {
        static std::atomic<std::uint64_t>
            s_last_traced_selected_cursor_poll_sequence{0u};
        const std::uint64_t previous_sequence =
            s_last_traced_selected_cursor_poll_sequence.exchange(
                host_pointer_metadata->sequence,
                std::memory_order_relaxed);
        trace_selected_cursor_poll_event =
            previous_sequence != host_pointer_metadata->sequence;
    }
    if (host_pointer_metadata != nullptr) {
        host_pointer_metadata->trace_selected_cursor_poll_event =
            trace_selected_cursor_poll_event;
    }
    const bool emit_input_trace =
        trace_boot_input_sample_at(current_vi, trace_interesting) ||
        trace_selected_cursor_poll_event;
    if (emit_input_trace) {
        static constexpr std::array<int, 16> kRawTraceVirtualKeys{
            VK_SPACE,
            VK_RETURN,
            VK_LSHIFT,
            VK_RSHIFT,
            '1',
            VK_NUMPAD1,
            '2',
            VK_NUMPAD2,
            VK_LEFT,
            VK_RIGHT,
            VK_UP,
            VK_DOWN,
            'A',
            'D',
            'W',
            'S',
        };
        // This field records process-global raw key state even when the game window is unfocused. It is
        // diagnostic only and is not routed through the focus-filtered functional input path.
        const std::uint32_t raw_key_mask =
            galaxy::input::collect_native_raw_key_trace_mask(
                host_window_input_enabled, kRawTraceVirtualKeys, [](int vk) {
                    return (GetAsyncKeyState(vk) & 0x8000) != 0;
                });
        static std::uint64_t s_input_trace_sample = 0;
        ++s_input_trace_sample;
        const HostPointerSequenceDomain host_pointer_sequence_domain =
            host_pointer_metadata != nullptr
            ? host_pointer_metadata->sequence_domain
            : HostPointerSequenceDomain::None;
        const std::uint64_t host_pointer_sequence =
            host_pointer_metadata != nullptr
            ? host_pointer_metadata->sequence
            : 0u;
        const std::uint64_t host_pointer_acquired_ms =
            host_pointer_metadata != nullptr
            ? host_pointer_metadata->acquired_ms
            : 0u;
        const bool host_pointer_sampled =
            host_pointer_metadata != nullptr &&
            host_pointer_metadata->sampled;
        std::ostringstream trace_line;
        trace_line << "[input-trace] boundary=bt-hid sample="
                  << s_input_trace_sample << " source=" << input_source
                  << " vi=" << current_vi
                  << " xinput=" << (controller.connected ? 1 : 0)
                  << " slot="
                  << (controller.connected ? static_cast<int>(controller.slot)
                                           : -1)
                  << " report=0x" << std::hex
                  << static_cast<unsigned>(report_mode)
                  << " buttons=0x" << buttons
                  << " hold=0x" << buttons
                  << " script=0x" << script.trace_mask << std::dec
                  << " focus=" << (window_focused ? 1 : 0)
                  << " fgwnd=0x" << std::hex
                  << reinterpret_cast<std::uintptr_t>(focused_window)
                  << " keyraw=0x" << raw_key_mask << std::dec
                  << " stick=(" << stick_x << ',' << stick_y << ')'
                  << " rstick=(" << pointer_axes.x << ',' << pointer_axes.y
                  << ')'
                   << " pointer=(" << s_pointer_x << ',' << s_pointer_y << ')'
                   << " ptrsrc=" << pointer_source
                   << " host-domain="
                   << host_pointer_sequence_domain_name(
                          host_pointer_sequence_domain)
                   << " host-seq=" << host_pointer_sequence
                   << " host-acquired-ms=" << host_pointer_acquired_ms
                   << " host-sampled=" << (host_pointer_sampled ? 1 : 0)
                   << " hostptr=("
                  << (traced_host_pointer_polled ? 1 : 0) << ','
                  << (traced_host_pointer.absolute_valid ? 1 : 0) << ','
                  << (traced_host_pointer.inside_client ? 1 : 0) << ','
                  << traced_host_pointer.client_x << ','
                  << traced_host_pointer.client_y << ','
                  << traced_host_pointer.client_width << ','
                  << traced_host_pointer.client_height << ')'
                  << " hostdbg=(0x" << std::hex
                  << traced_host_pointer.debug_flags
                  << ",0x" << traced_host_pointer.debug_error
                  << ",0x" << traced_host_pointer.debug_window
                  << std::dec << ')'
                  << " irdots=("
                  << (snapshot.ir[0].visible ? 1 : 0) << ','
                  << snapshot.ir[0].x << ',' << snapshot.ir[0].y << ','
                  << static_cast<unsigned>(snapshot.ir[0].size) << ';'
                  << (snapshot.ir[1].visible ? 1 : 0) << ','
                  << snapshot.ir[1].x << ',' << snapshot.ir[1].y << ','
                  << static_cast<unsigned>(snapshot.ir[1].size) << ')'
                  << " ir=" << (ir_active ? 1 : 0)
                  << " shake=" << (shake_active ? 1 : 0)
                  << " nunchuk=("
                  << static_cast<unsigned>(snapshot.nunchuk.stick_x) << ','
                  << static_cast<unsigned>(snapshot.nunchuk.stick_y) << ')'
                  << '\n';
        std::cerr << trace_line.str();
    }

    return snapshot;
}

constexpr bool boot_asset_extents_fit(std::uint64_t bi2_size,
                                     std::uint64_t fst_size) noexcept {
    constexpr std::uint64_t available = 0x81800000ull - kRmge01BootArenaLow;
    return bi2_size <= available && fst_size <= available - bi2_size;
}

}  // namespace

galaxy::input::WiimoteInputSnapshot native_game_wiimote_snapshot(
    std::uint8_t report_mode,
    std::uint64_t current_vi,
    bool ir_enabled,
    std::int32_t star_pointer_mode,
    std::uint8_t tilt_section) {
    const galaxy::input::PlayContext context =
        star_pointer_mode >= 0 && static_cast<std::uint32_t>(star_pointer_mode) < galaxy::input::kStarPointerModeCount
        ? galaxy::input::play_context_from_star_pointer_mode(static_cast<std::uint32_t>(star_pointer_mode))
        : galaxy::input::PlayContext::Play;
    const galaxy::input::tilt::Section section = tilt_section == 1u ? galaxy::input::tilt::Section::StarBall
        : tilt_section == 2u ? galaxy::input::tilt::Section::Ray
        : galaxy::input::tilt::Section::None;
    return build_native_hid_input_snapshot(
        report_mode, current_vi, galaxy::get_runtime_input_mode_state(), std::nullopt, ir_enabled, nullptr, context,
        section);
}

void native_game_rumble(bool motor_on, std::int32_t star_pointer_mode) noexcept {
    publish_rumble_for_mode(
        motor_on, star_pointer_mode >= 0 ? std::optional<std::uint32_t>(static_cast<std::uint32_t>(star_pointer_mode))
                                         : std::nullopt);
}

void native_game_wiimote_calibration(
    std::uint8_t (&eeprom)[0x2A],
    std::uint8_t (&nunchuk_calibration)[32]) {
    const auto bytes = create_virtual_wiimote_eeprom();
    std::memcpy(eeprom, bytes.data(), sizeof(eeprom));
    const auto registers = create_virtual_nunchuk_registers(true);
    std::memcpy(nunchuk_calibration, registers.data() + 0x20u, sizeof(nunchuk_calibration));
}

std::vector<std::byte> native_game_empty_mii_database() {
    return create_empty_rfl_database();
}

struct NativeGameAudio::State {
    JAudioHostMixer mixer;
    NativeAudioSink sink;
};

NativeGameAudio::NativeGameAudio() : state_(std::make_unique<State>()) {}
NativeGameAudio::~NativeGameAudio() = default;

namespace {
// A guest address's bytes in the native game's regions (the arena's physical, cached and uncached views, and the
// module image).
std::byte* native_game_resolve(void* user, std::uint32_t address, std::uint32_t size) {
    const auto& memory = *static_cast<const GuestMemory*>(user);
    const GuestMemoryFastRegion& fast = memory.fast_regions[address >> 28u];
    const std::uint32_t offset = address & 0x0FFF'FFFFu;
    if (fast.host_base != nullptr && offset <= fast.size && size <= fast.size - offset) {
        return fast.host_base + offset;
    }
    for (std::uint32_t i = 0; i < memory.region_count; ++i) {
        const GuestMemoryRegion& region = memory.regions[i];
        if (address - region.guest_base < region.size && size <= region.size - (address - region.guest_base)) {
            return region.host_base + (address - region.guest_base);
        }
    }
    return nullptr;
}
}  // namespace

bool NativeGameAudio::dsp_command(const std::uint32_t* words, std::uint32_t count, const GuestMemory& memory) {
    const JAudioGuestMemory guest{const_cast<GuestMemory*>(&memory), &native_game_resolve};
    JAudioHostMixer& mixer = state_->mixer;
    const auto word = [&](std::uint32_t index) { return index < count ? words[index] : 0u; };
    if (count == 0u) {
        return mixer.release_halt(words[0], guest);
    }
    switch (words[0] >> 24u) {
    case 0x81u:
        if (count >= 5u) {
            mixer.setup(words[0], word(1u), word(2u), word(3u), word(4u), guest);
        } else {
            mixer.note_ignored_command();
        }
        break;
    case 0x8Eu:
        mixer.set_varam(word(1u));
        break;
    case 0x82u: {
        // The volume categories, once an audio frame (master applies at the output); they never reached the native
        // game's mixer before b106 (the owner: the audio split does nothing).
        const galaxy::RuntimeAudioSettings audio = galaxy::get_runtime_settings().audio;
        mixer.set_bus_gains({1.0f, audio.music, audio.voice, audio.sfx, audio.ambience, audio.cutscene});
        mixer.sync_frame(words[0], word(1u), word(2u), word(3u), word(4u), guest);
        break;
    }
    default:
        mixer.note_ignored_command();
        break;
    }
    return false;
}

void NativeGameAudio::set_channel_owner(std::uint32_t (*owner)(std::uint32_t, std::uint32_t*)) {
    state_->mixer.set_owner_resolver(owner);
}

bool NativeGameAudio::play(std::span<const std::byte> block, std::uint32_t sample_rate) {
    return state_->sink.submit_pcm16_be_stereo(block, sample_rate, 0u, 0u);
}

unsigned NativeGameAudio::queued_blocks() const {
    return state_->sink.unplayed_buffers();
}

unsigned NativeGameAudio::target_blocks() const {
    return std::max(state_->sink.prebuffer_buffers(), 1u);
}

bool NativeGameAudio::output_available() const {
    return state_->sink.output_available();
}

// Windows keeps the game running out of sight (minimised, as v1.0.1).
void NativeGameAudio::set_paused(bool) {}

std::string NativeGameAudio::stats_line() const {
    const JAudioHostMixerStats& s = state_->mixer.stats();
    std::ostringstream line;
    line << "setups=" << s.setups << " frames=" << s.frames << " subframes=" << s.subframes
         << " channel-renders=" << s.channel_renders << " adpcm-blocks=" << s.adpcm_blocks << " pcm=" << s.pcm_renders
         << " streams=" << s.stream_renders << " oscillators=" << s.oscillator_renders << " fir=" << s.fir_renders
         << " iir=" << s.iir_renders << " auto-mixer=" << s.auto_mixer_renders
         << " fx-line-subframes=" << s.fx_line_subframes << " unresolved=" << s.unresolved_reads
         << " unsupported-osc=" << s.unsupported_oscillators << " ignored=" << s.ignored_commands
         << " stray=" << s.stray_releases << " categories=" << s.category_decisions
         << " owner-misses=" << s.category_owner_misses << " varam=0x" << std::hex << state_->mixer.varam() << std::dec
         << " mix-ms=" << s.mix_ns / 1000000u;
    return line.str();
}

GamePaths locate_game(const std::filesystem::path& input) {
    if (std::filesystem::is_regular_file(input)) {
        if (input.filename() == L"game.pak") {
            return {input.parent_path()};
        }
        throw std::runtime_error(
            "game input file must be the installer-generated game.pak");
    }
    const std::array<std::filesystem::path, 2> roots{input / L"DATA", input};
    for (const auto& root : roots) {
        if (std::filesystem::is_regular_file(root / L"game.pak")) {
            return {root};
        }
    }
    throw std::runtime_error(
        "game input does not contain DATA/game.pak or game.pak; run build-pak first");
}

namespace {
}  // namespace

void set_env_flag_cache_enabled(bool enabled) noexcept {
    g_env_flag_cache_enabled.store(enabled, std::memory_order_release);
}

// Native DSP coprocessor bridge (product path, gated by GALAXY_DSP_NATIVE).
//
// When enabled, the real lowered RMGE01 ucode runs on a free-running worker in place of the high-level
// mixer. The host owns three jobs: load the ucode at boot, ferry CPU<->DSP mail across the thread
// boundary, and translate the two DSP memory buses (DSP DMA -> MRAM, sample accelerator -> ARAM) into
// guest memory. Product builds hard-fail without this generated coprocessor; the mixer is reachable only
// from the test-support target.
#if defined(GALAXY_HAVE_NATIVE_DSP)
namespace {

struct GeneratedDsp {
    void (*entry)(galaxy::DspContext&);
    std::uint16_t (*iram_start)();
    std::uint32_t (*iram_byte_len)();
    const char* (*iram_sha1)();
    std::uint32_t (*probe_contract_version)();
    std::uint32_t (*probe_capabilities)();
};

#if defined(GALAXY_NATIVE_DSP_MODULE)
// Product builds load the DSP that Setup compiles from the user's game, so the
// prebuilt runtime itself contains nothing derived from the game.
const GeneratedDsp& generated_dsp() {
    static const GeneratedDsp dsp = [] {
        std::wstring path(32768, L'\0');
        const DWORD length = GetModuleFileNameW(
            nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (length == 0u || length >= path.size()) {
            throw std::runtime_error("cannot locate NebulaRuntime.exe");
        }
        path.resize(length);
        path.resize(path.find_last_of(L"\\/") + 1u);
        path += L"RMGE01_dsp.dll";
        const HMODULE module = LoadLibraryExW(
            path.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        if (module == nullptr) {
            throw std::runtime_error("RMGE01_dsp.dll could not be loaded");
        }
        const auto load = [module](const char* name) {
            const FARPROC symbol = GetProcAddress(module, name);
            if (symbol == nullptr) {
                throw std::runtime_error(
                    std::string("RMGE01_dsp.dll has no ") + name);
            }
            return symbol;
        };
        GeneratedDsp result{};
        result.entry = reinterpret_cast<decltype(result.entry)>(
            load("galaxy_rmge01_dsp_entry"));
        result.iram_start = reinterpret_cast<decltype(result.iram_start)>(
            load("galaxy_rmge01_dsp_entry_expected_iram_start_address"));
        result.iram_byte_len = reinterpret_cast<decltype(result.iram_byte_len)>(
            load("galaxy_rmge01_dsp_entry_expected_iram_byte_len"));
        result.iram_sha1 = reinterpret_cast<decltype(result.iram_sha1)>(
            load("galaxy_rmge01_dsp_entry_expected_iram_sha1"));
        result.probe_contract_version =
            reinterpret_cast<decltype(result.probe_contract_version)>(
                load("galaxy_rmge01_dsp_entry_generated_probe_contract_version"));
        result.probe_capabilities =
            reinterpret_cast<decltype(result.probe_capabilities)>(
                load("galaxy_rmge01_dsp_entry_generated_probe_capabilities"));
        return result;
    }();
    return dsp;
}
#endif

}  // namespace

#if !defined(GALAXY_NATIVE_DSP_MODULE)
extern "C" void galaxy_rmge01_dsp_entry(galaxy::DspContext& ctx);
extern "C" std::uint16_t galaxy_rmge01_dsp_entry_expected_iram_start_address();
extern "C" std::uint32_t galaxy_rmge01_dsp_entry_expected_iram_byte_len();
extern "C" const char* galaxy_rmge01_dsp_entry_expected_iram_sha1();
extern "C" std::uint32_t
galaxy_rmge01_dsp_entry_generated_probe_contract_version();
extern "C" std::uint32_t
galaxy_rmge01_dsp_entry_generated_probe_capabilities();

namespace {

const GeneratedDsp& generated_dsp() {
    static constexpr GeneratedDsp dsp{
        &galaxy_rmge01_dsp_entry,
        &galaxy_rmge01_dsp_entry_expected_iram_start_address,
        &galaxy_rmge01_dsp_entry_expected_iram_byte_len,
        &galaxy_rmge01_dsp_entry_expected_iram_sha1,
        &galaxy_rmge01_dsp_entry_generated_probe_contract_version,
        &galaxy_rmge01_dsp_entry_generated_probe_capabilities,
    };
    return dsp;
}

}  // namespace
#endif
#endif

// Host-backed NAND (ISFS): save data with fresh-Wii semantics

namespace {

// A NAND root the user assembled (copied from another installation, extracted from a zip, edited in Explorer) can lack
// the metadata streams written here, carry other metadata, or hold files the game never reads. Such a root is rebuilt
// like the first start of a new console: the files the game reads (its title data and the Mii database) are carried
// over with their default metadata, and everything else is dropped or set aside.

}  // namespace

#if defined(GALAXY_NATIVE_HOST_TEST_SUPPORT)
void native_host_test_attach_nand_metadata(const std::filesystem::path& path) {
    (void)attach_nand_metadata_to_existing_object(path, {}, false, {});
}
#endif

namespace {

}  // namespace

namespace {

void publish_rumble_for_mode(bool motor_on, std::optional<std::uint32_t> star_pointer_mode) noexcept {
    static galaxy::input::rumble::MotorEnvelope s_motor{};
    RumbleRequest request{};
    request.motor_on = motor_on;
    request.level = galaxy::input::rumble::quantize_level(
        s_motor.update(motor_on, galaxy::host::controller_hid_now_us()));
    request.target = g_rumble_host.target;
    request.strength = g_rumble_host.strength;
    request.focused = g_rumble_host.focused;
    const std::optional<std::uint32_t>& mode = star_pointer_mode;
    const bool menu = mode.has_value() && (*mode == 0u || *mode == 2u || *mode == 7u);
    const NativeSettingsVisibleProvider overlay = g_native_settings_visible.load(std::memory_order_acquire);
    request.paused = menu || (overlay != nullptr && overlay());
    publish_rumble_request(request);
}

}  // namespace

}  // namespace galaxy::host
