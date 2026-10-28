// The native game's Windows host services: the game's audio on XAudio2, keyboard, mouse and controllers as Wii
// Remote input reports (with the scripted and replayed test input), vibration and the virtual Wii devices.
#include "galaxy/native_host.h"
#include "galaxy/wii_device_data.h"

#include "galaxy/native_audio_queue.h"
#include "galaxy/platform_time.h"
#include "galaxy/jaudio_host_mixer.h"
#include "galaxy/controller_hid.h"
#include "galaxy/wiimote_hid.h"
#include "galaxy/input_bindings.h"
#include "galaxy/native_input.h"
#include "galaxy/native_settings_pad.h"
#include "galaxy/pointer_control.h"
#include "galaxy/rumble.h"
#include "galaxy/tilt.h"
#include "galaxy/diagnostic_stick_lease.h"
#include "galaxy/experimental_ultrawide_aspect.h"
#include "galaxy/gx_d3d12.h"
#include "galaxy/runtime_settings.h"
#include "galaxy/scope_exit.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include "galaxy/windows_power.h"
#include <Xinput.h>
#include <mmdeviceapi.h>
#include <wrl/client.h>
#include <xaudio2.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace galaxy::host {
namespace {

using Microsoft::WRL::ComPtr;

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

// The input report reads its script switches on every poll, and getenv_s scans the whole environment block each
// time. Once set_env_flag_cache_enabled(true) runs, a value is read once per literal name (the pointer is the key).
// Tests leave the cache off so the variables they change stay visible.
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

std::optional<std::string> read_env_string_uncached(const char* name) {
    // Short values fit the stack buffer; getenv_s reports the size of a longer one for an exact allocation.
    constexpr std::size_t kStackEnvironmentValueLength = 512u;
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

// The string counterpart of the flag cache, for the scripted input read on every report.
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

bool xinput_requires_focus() {
    static const bool requires_focus =
        read_env_flag("GALAXY_XINPUT_REQUIRE_FOCUS");
    return requires_focus;
}

// Registers the audio worker with MMCSS ("Pro Audio", else "Audio") at critical priority for its lifetime.
class ScopedMmcssAudioThread {
public:
    ScopedMmcssAudioThread() {
        avrt_ = LoadLibraryW(L"avrt.dll");
        if (avrt_ == nullptr) {
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
            return;
        }

        if (av_set_priority_ != nullptr) {
            constexpr int kAvrtPriorityCritical = 2;
            av_set_priority_(task_handle_, kAvrtPriorityCritical);
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

// GALAXY_AUDIO_WAV_DUMP=<path>: the output as a 16-bit stereo WAV file, at most 15 seconds of 32 kHz audio. A sample
// rate change starts a new file beside it (<name>.rate-<hz>.part-<n>.wav).
class AudioWavDump {
public:
    ~AudioWavDump() {
        finish();
    }

    AudioWavDump(const AudioWavDump&) = delete;
    AudioWavDump& operator=(const AudioWavDump&) = delete;

    AudioWavDump() = default;

    // Called from the submitting thread and from the audio worker.
    bool append(std::span<const std::byte> pcm_le, std::uint32_t sample_rate) {
        std::lock_guard<std::mutex> lock(mutex_);
        return append_locked(pcm_le, sample_rate);
    }

    bool finish() {
        std::lock_guard<std::mutex> lock(mutex_);
        return finish_locked();
    }

private:
    // 15 s of 32 kHz stereo, or GALAXY_AUDIO_WAV_DUMP_SECONDS (up to an hour) for a longer recording.
    static std::uint32_t max_bytes() {
        static const std::uint32_t bytes = [] {
            unsigned long seconds = 15ul;
            if (const std::optional<std::string> value = read_env_string("GALAXY_AUDIO_WAV_DUMP_SECONDS")) {
                const unsigned long parsed = std::strtoul(value->c_str(), nullptr, 10);
                if (parsed > 0ul && parsed <= 3600ul) seconds = parsed;
            }
            return 32000u * 4u * static_cast<std::uint32_t>(seconds);
        }();
        return bytes;
    }

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
        if (data_bytes_ >= max_bytes()) {
            return true;
        }
        std::size_t write_size = std::min<std::size_t>(
            pcm_le.size(),
            static_cast<std::size_t>(max_bytes() - data_bytes_));
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
        // The first sample's time, to line a recording's audio up with its snapshots ("[snapshot] ... qpc=").
        std::cout << "[audio] WAV dump enabled: " << path_
                  << " max-bytes=" << max_bytes() << " qpc=" << galaxy::qpc_now() << '\n';
        return true;
    }

    std::ofstream out_;
    std::filesystem::path base_path_;
    std::filesystem::path path_;
    std::uint32_t data_bytes_{};
    std::uint32_t sample_rate_{};
    std::uint32_t segment_index_{};
    bool disabled_{};
    bool open_attempted_{};
};

// The game's audio interface output on XAudio2: one 32 kHz stereo PCM16 source voice that plays 48 kHz blocks at a
// 1.5 frequency ratio. The submitting thread converts each block and queues it; a worker thread owns the source voice
// and feeds it, and XAudio2's callbacks only publish facts for that worker (native_audio_queue.h). A missing or lost
// output device leaves the sink running without sound; any other failure stops the output for the session.
class NativeAudioSink final : public IXAudio2VoiceCallback,
                              public IXAudio2EngineCallback {
public:
    NativeAudioSink() = default;
    ~NativeAudioSink() {
        shutdown_backend(true);
    }

    NativeAudioSink(const NativeAudioSink&) = delete;
    NativeAudioSink& operator=(const NativeAudioSink&) = delete;

    // One AI block in the console's layout (big-endian right, left). Always called from the same thread.
    bool submit_pcm16_be_stereo(std::span<const std::byte> source, std::uint32_t sample_rate) {
        if (source.empty()) {
            return true;
        }
        if ((source.size() & 3u) != 0u ||
            source.size() > std::numeric_limits<UINT32>::max()) {
            log_drop("invalid AI PCM buffer size");
            return false;
        }
        if (sample_rate != 32'000u && sample_rate != 48'000u) {
            log_drop("unsupported Wii AI PCM sample rate");
            return false;
        }
        if (read_env_flag("GALAXY_AUDIO_WAV_DUMP_ONLY")) {
            return submit_to_wav_dump_only(source, sample_rate);
        }
        if (!initialization_gate_.ensure_ready_for_submit([this] { return initialize_backend(); })) {
            return false;
        }
        if (initialization_thread_id_ != 0u && initialization_thread_id_ != GetCurrentThreadId()) {
            strict_failure_.store(true, std::memory_order_release);
            return false;
        }
        if (strict_failure_.load(std::memory_order_acquire)) {
            return false;
        }

        if (endpoint_offline_ || endpoint_recovery_pending_.load(std::memory_order_acquire)) {
            // No device: the block is dropped unheard, but still reaches a requested WAV dump.
            if (get_env_path(L"GALAXY_AUDIO_WAV_DUMP").has_value()) {
                std::vector<std::byte> pcm_le(source.size());
                if (!native_audio_convert_wii_ai_pcm16(source, pcm_le)) {
                    log_drop("offline audio WAV diagnostic failed");
                    return false;
                }
                apply_output_master_gain(pcm_le);
                if (!wav_dump_.append(pcm_le, sample_rate)) {
                    log_drop("offline audio WAV diagnostic failed");
                    return false;
                }
            }
            return true;
        }
        auto buffer = std::make_unique<SubmittedBuffer>();
        buffer->sample_rate = sample_rate;
        buffer->bytes.resize(source.size());
        if (!native_audio_convert_wii_ai_pcm16(
                source, std::span<std::byte>(buffer->bytes))) {
            log_drop("invalid Wii AI PCM channel conversion");
            return false;
        }
        apply_output_master_gain(buffer->bytes);
        return enqueue_pending_buffer(std::move(buffer));
    }

    // Accepted and not yet played: the worker's pending queue and the device's.
    unsigned unplayed_buffers() const {
        return queue_mirror_.queued_buffers() + pending_queue_depth_.load(std::memory_order_acquire);
    }

    // The buffers the voice waits for before it starts (and starts again after running dry).
    unsigned prebuffer_buffers() const noexcept {
        return kPrebufferBuffers;
    }

    bool output_available() const noexcept {
        return initialized_ && !endpoint_offline_ &&
            !endpoint_recovery_pending_.load(std::memory_order_acquire);
    }

private:
    // XAudio2 queue depth the worker keeps at most, and how long it waits for room before logging (wait) or giving
    // up on a device that stopped draining (stuck).
    static constexpr std::uint32_t kQueueLimit = 16u;
    static constexpr std::uint32_t kQueueWaitMs = 100u;
    static constexpr std::uint32_t kQueueStuckMs = 750u;
    static constexpr std::uint32_t kPrebufferBuffers = 4u;
    // Blocks converted and waiting for the worker; a full queue holds the submitting thread back for at most
    // kPendingQueueTimeoutMs before the output stops.
    static constexpr std::size_t kPendingQueueLimit = 256u;
    static constexpr std::int64_t kPendingQueueTimeoutMs = 250;

    struct SubmittedBuffer {
        std::atomic_bool started{false};
        std::atomic_bool completed{false};
        std::uint32_t sample_rate{};
        std::vector<std::byte> bytes;
    };

    bool submit_to_wav_dump_only(std::span<const std::byte> source, std::uint32_t sample_rate) {
        const std::optional<std::filesystem::path> dump_path =
            get_env_path(L"GALAXY_AUDIO_WAV_DUMP");
        if (!dump_path.has_value() || dump_path->empty()) {
            std::cerr << "[audio] GALAXY_AUDIO_WAV_DUMP_ONLY requires "
                         "GALAXY_AUDIO_WAV_DUMP\n";
            return false;
        }
        std::vector<std::byte> pcm_le(source.size());
        if (!native_audio_convert_wii_ai_pcm16(source, std::span<std::byte>(pcm_le))) {
            log_drop("invalid Wii AI PCM channel conversion");
            return false;
        }
        apply_output_master_gain(pcm_le);
        if (!wav_dump_.append(
                std::span<const std::byte>(pcm_le.data(), pcm_le.size()),
                sample_rate)) {
            log_drop("WAV dump-only output failed");
            return false;
        }
        const std::uint64_t submitted = ++dump_only_buffers_;
        if (submitted <= 8 || submitted % 1000 == 0) {
            std::cout << "[audio] dump-only AI PCM buffer #"
                      << submitted
                      << " bytes=" << source.size() << '\n';
        }
        return true;
    }

    // Master volume, applied once to the final output so it also covers audio mixed outside the DSP channels (the
    // movie player). A change ramps across one buffer from the previous value. One submitting thread.
    void apply_output_master_gain(std::vector<std::byte>& pcm_le) noexcept {
        const float master = galaxy::get_runtime_settings().audio.master;
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

    bool enqueue_pending_buffer(std::unique_ptr<SubmittedBuffer> buffer) {
        if (strict_failure_.load(std::memory_order_acquire)) {
            return false;
        }
        {
            std::unique_lock<std::mutex> lock(pending_mutex_);
            // The device can be lost between the offline check in submit_pcm16_be_stereo and here. The worker then
            // stops draining, so this block is dropped instead of waiting for room.
            if (endpoint_recovery_pending_.load(std::memory_order_acquire)) {
                lock.unlock();
                return preserve_unheard_buffer(*buffer);
            }
            if (worker_stop_.load(std::memory_order_acquire)) {
                strict_failure_.store(true, std::memory_order_release);
                return false;
            }
            const auto pending_wait_started =
                std::chrono::steady_clock::now();
            while (pending_buffers_.size() >= kPendingQueueLimit) {
                if (!pending_overflow_logged_) {
                    pending_overflow_logged_ = true;
                    std::cout << "[audio] native pending queue full"
                              << " pending=" << pending_buffers_.size()
                              << " limit=" << kPendingQueueLimit
                              << "; applying backpressure without dropping AI buffer\n";
                }
                pending_cv_.wait_for(lock, std::chrono::milliseconds(1));
                if (strict_failure_.load(std::memory_order_acquire)) {
                    return false;
                }
                if (endpoint_recovery_pending_.load(std::memory_order_acquire)) {
                    lock.unlock();
                    return preserve_unheard_buffer(*buffer);
                }
                if (worker_stop_.load(std::memory_order_acquire)) {
                    strict_failure_.store(true, std::memory_order_release);
                    return false;
                }
                const auto pending_wait_ms =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() -
                        pending_wait_started)
                        .count();
                if (pending_wait_ms >= kPendingQueueTimeoutMs) {
                    strict_failure_.store(true, std::memory_order_release);
                    return false;
                }
            }
            if (endpoint_recovery_pending_.load(std::memory_order_acquire)) {
                lock.unlock();
                return preserve_unheard_buffer(*buffer);
            }
            pending_buffers_.push_back(std::move(buffer));
            pending_queue_depth_.store(
                static_cast<std::uint32_t>(pending_buffers_.size()), std::memory_order_release);
        }
        if (buffer_end_event_ == nullptr || !SetEvent(buffer_end_event_)) {
            strict_failure_.store(true, std::memory_order_release);
            return false;
        }
        return true;
    }

    bool preserve_unheard_buffer(const SubmittedBuffer& buffer) {
        if (strict_failure_.load(std::memory_order_acquire)) return false;
        if (get_env_path(L"GALAXY_AUDIO_WAV_DUMP").has_value() &&
            !wav_dump_.append(buffer.bytes, buffer.sample_rate)) {
            log_drop("offline audio WAV diagnostic failed");
            return false;
        }
        return true;
    }

    bool initialize_backend() {
        if (initialized_) {
            return true;
        }
        initialization_thread_id_ = GetCurrentThreadId();

        const HRESULT com_hr = !com_initialized_
            ? CoInitializeEx(nullptr, COINIT_MULTITHREADED) : S_FALSE;
        if (SUCCEEDED(com_hr)) {
            com_initialized_ = true;
        } else if (com_hr != RPC_E_CHANGED_MODE) {
            strict_failure_.store(true, std::memory_order_release);
            std::cerr << "[audio] CoInitializeEx failed hr=0x" << std::hex
                      << static_cast<unsigned long>(com_hr) << std::dec
                      << '\n';
            return false;
        }

        if (buffer_end_event_ == nullptr) {
            buffer_end_event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (buffer_end_event_ == nullptr) {
                strict_failure_.store(true, std::memory_order_release);
                std::cerr << "[audio] CreateEventW for queue wake failed error="
                          << GetLastError() << '\n';
                shutdown_backend(false);
                return false;
            }
        }

        HRESULT hr = XAudio2Create(&engine_, 0, XAUDIO2_DEFAULT_PROCESSOR);
        if (FAILED(hr)) {
            strict_failure_.store(true, std::memory_order_release);
            std::cerr << "[audio] XAudio2Create failed hr=0x" << std::hex
                      << static_cast<unsigned long>(hr) << std::dec << '\n';
            shutdown_backend(false);
            return false;
        }
        hr = engine_->RegisterForCallbacks(this);
        if (FAILED(hr)) {
            strict_failure_.store(true, std::memory_order_release);
            std::cerr << "[audio] RegisterForCallbacks failed hr=0x"
                      << std::hex << static_cast<unsigned long>(hr)
                      << std::dec << '\n';
            shutdown_backend(false);
            return false;
        }
        engine_callback_registered_ = true;

        // What endpoint discovery saw, for the log line when there is no usable endpoint: endpoints belong to the
        // Windows session, so a device the device manager lists can still be missing here.
        struct MasteringVoiceEndpointProbe {
            HRESULT default_create_result{E_FAIL};
            HRESULT enumerator_create_result{E_FAIL};
            HRESULT enumerate_active_result{E_FAIL};
            HRESULT active_count_result{E_FAIL};
            std::optional<HRESULT> last_active_endpoint_result{};
            UINT active_render_endpoint_count{};
        } endpoint_probe{};

        // The default endpoint, else the first active render endpoint XAudio2 accepts.
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
            if (native_audio_endpoint_error_recoverable(static_cast<std::uint32_t>(hr))) {
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
                shutdown_backend(false);
                if (strict_failure_.load(std::memory_order_acquire)) return false;
                wait_for_endpoint_after_shutdown();
                return true;
            }
            strict_failure_.store(true, std::memory_order_release);
            std::cerr << "[audio] CreateMasteringVoice failed hr=0x"
                      << std::hex << static_cast<unsigned long>(hr)
                      << std::dec << '\n';
            shutdown_backend(false);
            return false;
        }
        endpoint_offline_ = false;

        hr = engine_->StartEngine();
        if (FAILED(hr)) {
            if (native_audio_endpoint_error_recoverable(static_cast<std::uint32_t>(hr))) {
                shutdown_backend(false);
                if (strict_failure_.load(std::memory_order_acquire)) return false;
                wait_for_endpoint_after_shutdown();
                return true;
            }
            strict_failure_.store(true, std::memory_order_release);
            std::cerr << "[audio] XAudio2 StartEngine failed hr=0x" << std::hex
                      << static_cast<unsigned long>(hr) << std::dec << '\n';
            shutdown_backend(false);
            return false;
        }
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

        shutting_down_.store(false, std::memory_order_release);
        worker_stop_.store(false, std::memory_order_release);
        try {
            worker_thread_ =
                std::thread(&NativeAudioSink::audio_worker_loop, this);
        } catch (const std::exception& ex) {
            strict_failure_.store(true, std::memory_order_release);
            std::cerr << "[audio] failed to start native audio worker: "
                      << ex.what() << '\n';
            shutdown_backend(false);
            return false;
        }
        initialized_ = true;
        return true;
    }

    // No device: the sink accepts blocks and drops them (output_available() is false).
    void wait_for_endpoint_after_shutdown() noexcept {
        endpoint_offline_ = true;
        initialized_ = true;
    }

    void shutdown_backend(bool release_control_com) {
        shutting_down_.store(true, std::memory_order_release);
        worker_stop_.store(true, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(pending_mutex_);
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
        if (source_voice_ != nullptr) {
            const HRESULT stop_hr = source_voice_->Stop(0);
            if (FAILED(stop_hr) &&
                !native_audio_endpoint_error_recoverable(static_cast<std::uint32_t>(stop_hr))) {
                strict_failure_.store(true, std::memory_order_release);
                std::cerr << "[audio] SourceVoice Stop during shutdown failed hr=0x"
                          << std::hex << static_cast<unsigned long>(stop_hr)
                          << std::dec << '\n';
            }
            const HRESULT flush_hr = source_voice_->FlushSourceBuffers();
            if (FAILED(flush_hr) &&
                !native_audio_endpoint_error_recoverable(static_cast<std::uint32_t>(flush_hr))) {
                strict_failure_.store(true, std::memory_order_release);
                std::cerr
                    << "[audio] SourceVoice FlushSourceBuffers during shutdown failed hr=0x"
                    << std::hex << static_cast<unsigned long>(flush_hr)
                    << std::dec << '\n';
            }
            source_voice_->DestroyVoice();
            callback_source_voice_.store(nullptr, std::memory_order_release);
            source_voice_ = nullptr;
        }
        // FlushSourceBuffers can end queued buffers that never started. DestroyVoice has quiesced the callbacks, so
        // only this teardown accepts that; live playback treats it as a failure.
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
        initialized_ = false;
    }

    void audio_worker_loop() noexcept {
        try {
            audio_worker_body();
        } catch (...) {
            strict_failure_.store(true, std::memory_order_release);
            // Submitted buffers stay in in_flight_buffers_ until the stopped voice is destroyed; pending ones can go.
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
                clear_pending_after_worker_failure();
                return;
            }
            if (buffer == nullptr && !callback_event) {
                const DWORD wait_result = buffer_end_event_ != nullptr
                    ? WaitForSingleObject(buffer_end_event_, kQueueWaitMs)
                    : WAIT_FAILED;
                if (wait_result == WAIT_FAILED &&
                    !worker_stop_.load(std::memory_order_acquire)) {
                    strict_failure_.store(true, std::memory_order_release);
                    clear_pending_after_worker_failure();
                    return;
                }
                continue;
            }
            if (callback_event && source_voice_ != nullptr) {
                const unsigned queued = query_source_queue();
                if (queued == 0u && !mark_source_queue_empty()) {
                    clear_pending_after_worker_failure();
                    return;
                }
            }
            if (buffer == nullptr) {
                continue;
            }
            if (!submit_pending_to_xaudio(buffer)) {
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

    // A lost device marks the output unavailable; the caller stops feeding it.
    bool request_endpoint_recovery(HRESULT error) noexcept {
        if (!native_audio_endpoint_error_recoverable(static_cast<std::uint32_t>(error))) return false;
        if (!shutting_down_.load(std::memory_order_acquire)) {
            endpoint_recovery_pending_.store(true, std::memory_order_release);
        }
        return true;
    }

    // Acts on what the callbacks published. False stops the worker: a lost device or a fatal error.
    bool process_callback_failures() {
        if (endpoint_recovery_pending_.load(std::memory_order_acquire) &&
            !shutting_down_.load(std::memory_order_acquire)) return false;
        const std::uint64_t engine_errors =
            callback_ledger_.engine_critical_errors();
        if (engine_errors != handled_engine_critical_error_count_) {
            handled_engine_critical_error_count_ = engine_errors;
            const auto error = static_cast<HRESULT>(callback_ledger_.last_engine_critical_error());
            if (native_audio_endpoint_error_recoverable(static_cast<std::uint32_t>(error))) {
                if (!shutting_down_.load(std::memory_order_acquire)) {
                    std::cerr << "[audio] output endpoint unavailable hr=0x" << std::hex
                              << static_cast<unsigned long>(error) << std::dec
                              << "; scheduling device recovery\n";
                    endpoint_recovery_pending_.store(true, std::memory_order_release);
                    return false;
                }
            } else {
                strict_failure_.store(true, std::memory_order_release);
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
            const auto error = static_cast<HRESULT>(callback_ledger_.last_voice_error());
            if (request_endpoint_recovery(error)) {
                if (!shutting_down_.load(std::memory_order_acquire)) return false;
            } else {
                strict_failure_.store(true, std::memory_order_release);
                if (!voice_error_logged_) {
                    voice_error_logged_ = true;
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
            const auto error = static_cast<HRESULT>(callback_ledger_.last_frequency_ratio_apply_error());
            if (request_endpoint_recovery(error)) {
                if (!shutting_down_.load(std::memory_order_acquire)) return false;
            } else {
                strict_failure_.store(true, std::memory_order_release);
                if (!frequency_ratio_apply_error_logged_) {
                    frequency_ratio_apply_error_logged_ = true;
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
            return mark_source_queue_empty();
        case NativeAudioEmptyBoundaryDisposition::FatalCounterDiscontinuity:
            strict_failure_.store(true, std::memory_order_release);
            return false;
        }
        return false;
    }

    // Drops the buffers XAudio2 has finished with. A buffer that ended without starting is a failure, except after
    // FlushSourceBuffers at teardown.
    [[nodiscard]] bool account_started_and_reap_completed_buffers(
        bool voice_destroyed_after_flush = false) {
        for (auto it = in_flight_buffers_.begin();
             it != in_flight_buffers_.end();) {
            SubmittedBuffer& buffer = **it;
            const auto observation = observe_callback_buffer_state(
                [&] { return buffer.completed.load(std::memory_order_acquire); },
                [&] { return buffer.started.load(std::memory_order_acquire); });
            if (observation.completed && !observation.started && !voice_destroyed_after_flush) {
                strict_failure_.store(true, std::memory_order_release);
                return false;
            }
            if (observation.completed) {
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
            strict_failure_.store(true, std::memory_order_release);
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
            if (native_audio_endpoint_error_recoverable(static_cast<std::uint32_t>(hr))) {
                endpoint_recovery_pending_.store(true, std::memory_order_release);
                return false;
            }
            strict_failure_.store(true, std::memory_order_release);
            std::cerr << "[audio] CreateSourceVoice failed base-rate="
                      << kNativeAudioVoiceBaseSampleRate << " hr=0x" << std::hex
                      << static_cast<unsigned long>(hr) << std::dec << '\n';
            return false;
        }
        callback_source_voice_.store(
            source_voice_, std::memory_order_release);
        // Zero: the voice's first buffer sets its ratio explicitly, even the nominal 1.0.
        callback_active_sample_rate_.store(0u, std::memory_order_release);
        playback_lifecycle_.reset();
        queue_mirror_.publish_from_worker(0);
        return true;
    }

    bool submit_pending_to_xaudio(std::unique_ptr<SubmittedBuffer>& buffer) {
        if (source_voice_ == nullptr) {
            strict_failure_.store(true, std::memory_order_release);
            return false;
        }
        if (strict_failure_.load(std::memory_order_acquire)) {
            return false;
        }

        XAUDIO2_VOICE_STATE state{};
        bool waited_for_queue_space = false;
        auto queue_wait_started = std::chrono::steady_clock::now();
        // OnBufferEnd wakes the worker for freed space only while this is set. It is raised before each queue
        // observation, so no end is missed.
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
            if (state.BuffersQueued < kQueueLimit) {
                queue_space_wait_.store(false, std::memory_order_release);
                break;
            }
            if (!waited_for_queue_space) {
                waited_for_queue_space = true;
                queue_wait_started = std::chrono::steady_clock::now();
            }
            const auto elapsed_ms = std::chrono::duration_cast<
                std::chrono::milliseconds>(std::chrono::steady_clock::now() - queue_wait_started).count();
            const auto queue_wait_elapsed_ms = static_cast<std::uint32_t>(
                std::min<std::int64_t>(
                    std::max<std::int64_t>(elapsed_ms, 0),
                    std::numeric_limits<std::uint32_t>::max()));
            if (queue_wait_elapsed_ms >= kQueueWaitMs &&
                !queue_full_wait_logged_) {
                queue_full_wait_logged_ = true;
                std::cout << "[audio] native worker queue stayed full for "
                          << queue_wait_elapsed_ms
                          << " ms; waiting without dropping AI buffer"
                          << " queued=" << state.BuffersQueued
                          << " limit=" << kQueueLimit
                          << " pending="
                          << pending_queue_depth_.load(
                                  std::memory_order_acquire)
                          << '\n';
            }
            if (queue_wait_elapsed_ms >= kQueueStuckMs) {
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
                        << " limit=" << kQueueLimit
                        << " pending="
                        << pending_queue_depth_.load(
                               std::memory_order_acquire)
                        << '\n';
                }
                strict_failure_.store(true, std::memory_order_release);
                return false;
            }
            const DWORD wait_result = buffer_end_event_ != nullptr
                ? WaitForSingleObject(buffer_end_event_, kQueueWaitMs)
                : (Sleep(kQueueWaitMs), WAIT_TIMEOUT);
            if (worker_stop_.load(std::memory_order_acquire)) {
                return false;
            }
            if (wait_result == WAIT_FAILED) {
                strict_failure_.store(true, std::memory_order_release);
                if (!queue_wait_failure_logged_) {
                    queue_wait_failure_logged_ = true;
                    std::cerr << "[audio] native worker queue wait failed"
                              << " error=" << GetLastError()
                              << " queued=" << state.BuffersQueued
                              << " limit=" << kQueueLimit << '\n';
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

        const std::size_t buffer_size = buffer->bytes.size();
        if (resume_fade_pending_.exchange(false, std::memory_order_acq_rel)) {
            // The voice ran dry and stopped on silence: the first buffer after the restart rises from silence instead
            // of stepping.
            fade_in_pcm16_le_stereo(buffer->bytes);
        }
        wav_dump_.append(
            std::span<const std::byte>(buffer->bytes.data(), buffer_size),
            buffer->sample_rate);
        if (!process_callback_failures()) {
            return false;
        }
        const unsigned queued_before_submit = query_source_queue();
        if (queued_before_submit == 0u && !mark_source_queue_empty()) {
            return false;
        }
        // Reaps after the final capacity observation, before adding a buffer, even if the callbacks drained a full
        // queue during this submit.
        if (!account_started_and_reap_completed_buffers() ||
            !process_callback_failures()) {
            return false;
        }
        if (source_voice_ == nullptr) {
            strict_failure_.store(true, std::memory_order_release);
            return false;
        }

        XAUDIO2_BUFFER xa_buffer{};
        xa_buffer.AudioBytes = static_cast<UINT32>(buffer_size);
        xa_buffer.pAudioData =
            reinterpret_cast<const BYTE*>(buffer->bytes.data());
        xa_buffer.pContext = buffer.get();

        // The buffer is owned by in_flight_buffers_ before XAudio2 can see it, and stays there until reaped or the
        // voice is destroyed.
        in_flight_buffers_.push_back(std::move(buffer));
        const HRESULT hr = source_voice_->SubmitSourceBuffer(&xa_buffer);
        if (FAILED(hr)) {
            buffer = std::move(in_flight_buffers_.back());
            in_flight_buffers_.pop_back();
            if (native_audio_endpoint_error_recoverable(static_cast<std::uint32_t>(hr))) {
                endpoint_recovery_pending_.store(true, std::memory_order_release);
                return false;
            }
            strict_failure_.store(true, std::memory_order_release);
            if (!submit_failure_logged_) {
                submit_failure_logged_ = true;
                std::cerr << "[audio] XAudio2 SubmitSourceBuffer failed hr=0x"
                          << std::hex << static_cast<unsigned long>(hr)
                          << std::dec << '\n';
            }
            return false;
        }
        if (!process_callback_failures()) {
            return false;
        }
        return start_source_voice_if_ready(query_source_queue());
    }

    bool start_source_voice_if_ready(unsigned queued_after_submit) {
        if (source_voice_ == nullptr) {
            return true;
        }
        if (playback_lifecycle_.start_action(queued_after_submit, kPrebufferBuffers) !=
            NativeAudioPlaybackAction::StartVoice) {
            return true;
        }
        const HRESULT hr = source_voice_->Start(0);
        if (FAILED(hr)) {
            if (request_endpoint_recovery(hr)) return false;
            strict_failure_.store(true, std::memory_order_release);
            if (!source_start_failure_logged_) {
                source_start_failure_logged_ = true;
                std::cerr << "[audio] SourceVoice Start failed hr=0x"
                          << std::hex << static_cast<unsigned long>(hr)
                          << std::dec << '\n';
            }
            return false;
        }
        playback_lifecycle_.mark_started();
        return true;
    }

    void log_drop(const char* reason) {
        const auto drops = ++dropped_buffers_;
        strict_failure_.store(true, std::memory_order_release);
        if (drops <= 8) {
            std::cerr << "[audio] dropped AI buffer: " << reason << '\n';
        }
    }

    // The queue ran dry: stops the voice so it restarts, faded in, once the prebuffer is queued again.
    bool mark_source_queue_empty() {
        if (shutting_down_.load(std::memory_order_acquire)) {
            return true;
        }
        if (playback_lifecycle_.observe_queue(0u) !=
            NativeAudioPlaybackAction::StopVoiceForRecovery) {
            return true;
        }
        if (source_voice_ == nullptr) {
            strict_failure_.store(true, std::memory_order_release);
            return false;
        }
        const HRESULT stop_hr = source_voice_->Stop(0);
        if (FAILED(stop_hr)) {
            if (request_endpoint_recovery(stop_hr)) return false;
            strict_failure_.store(true, std::memory_order_release);
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
    // Sets the frequency ratio exactly at the buffer that changes the sample rate; same-rate buffers keep the voice's
    // ratio without calling into XAudio2's engine thread.
    void STDMETHODCALLTYPE OnBufferStart(void* context) noexcept override {
        auto* const buffer = static_cast<SubmittedBuffer*>(context);
        if (buffer == nullptr) {
            callback_ledger_.record_voice_error(
                static_cast<std::int32_t>(E_POINTER));
            signal_callback_event();
            return;
        }
        const std::uint32_t active_sample_rate =
            callback_active_sample_rate_.load(std::memory_order_acquire);
        const NativeAudioFrequencyRatioBoundaryDecision ratio =
            native_audio_frequency_ratio_at_boundary(
                active_sample_rate, buffer->sample_rate);
        HRESULT ratio_hr = E_INVALIDARG;
        bool ratio_apply_invoked = false;
        bool ratio_apply_succeeded = false;
        if (ratio.valid && ratio.apply_required) {
            if (IXAudio2SourceVoice* const voice =
                    callback_source_voice_.load(std::memory_order_acquire);
                voice != nullptr) {
                ratio_apply_invoked = true;
                ratio_hr = voice->SetFrequencyRatio(
                    ratio.ratio, XAUDIO2_COMMIT_NOW);
                if (SUCCEEDED(ratio_hr)) {
                    ratio_apply_succeeded = true;
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
        } else if (!ratio.valid) {
            callback_ledger_.record_voice_error(
                static_cast<std::int32_t>(ratio_hr));
        }
        if (ratio_apply_invoked) {
            callback_ledger_.record_frequency_ratio_apply(
                ratio_apply_succeeded, static_cast<std::int32_t>(ratio_hr));
        }
        if (empty_active_.load(std::memory_order_acquire)) {
            empty_active_.store(false, std::memory_order_release);
        }
        // The worker reaps starts at its next submit or idle timeout; only failures, empty boundaries and freed
        // queue space wake it.
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
            queue_evidence = callback_state.BuffersQueued == 0u
                ? NativeAudioBufferEndQueueEvidence::EmptyAtBoundary
                : NativeAudioBufferEndQueueEvidence::SuccessorQueued;
        }
        // The callback's last access to the buffer; the worker frees it.
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
    NativeAudioQueueMirror queue_mirror_;
    NativeAudioEmptyBoundaryTracker empty_boundary_tracker_;
    NativeAudioPlaybackLifecycle playback_lifecycle_;
    NativeAudioInitializationGate initialization_gate_;
    AudioWavDump wav_dump_;
    std::uint32_t output_master_q16_ = galaxy::audio::kNativeAudioUnityGainQ16;
    bool output_master_started_ = false;
    std::uint64_t dump_only_buffers_{};
    std::uint64_t dropped_buffers_{};
    DWORD initialization_thread_id_{};
    std::uint64_t handled_voice_error_count_{};
    std::uint64_t handled_engine_critical_error_count_{};
    std::uint64_t handled_frequency_ratio_apply_error_count_{};
    std::uint64_t handled_callback_wake_error_count_{};
    std::uint64_t queue_stuck_recovery_events_{};
    bool initialized_{};
    bool endpoint_unavailable_logged_{};
    // No device, or a device that was lost: the blocks are dropped unheard.
    bool endpoint_offline_{};
    std::atomic_bool endpoint_recovery_pending_{false};
    bool com_initialized_{};
    bool engine_callback_registered_{};
    std::atomic_bool queue_space_wait_{false};
    std::atomic_bool empty_active_{false};
    std::atomic_bool resume_fade_pending_{false};
    std::atomic_bool shutting_down_{false};
    // A fatal output failure: every later block is refused.
    std::atomic_bool strict_failure_{false};
    bool source_start_failure_logged_{};
    bool submit_failure_logged_{};
    bool voice_error_logged_{};
    bool engine_critical_error_logged_{};
    bool frequency_ratio_apply_error_logged_{};
    bool callback_wake_error_logged_{};
    bool queue_full_wait_logged_{};
    bool queue_wait_failure_logged_{};
    bool queue_stuck_recovery_logged_{};
    bool pending_overflow_logged_{};
    std::deque<std::unique_ptr<SubmittedBuffer>> pending_buffers_;
    std::deque<std::unique_ptr<SubmittedBuffer>> in_flight_buffers_;
    std::thread worker_thread_;
    std::mutex pending_mutex_;
    std::condition_variable pending_cv_;
    std::atomic_bool worker_stop_{false};
    std::atomic<std::uint32_t> pending_queue_depth_{0};
};

}  // namespace

static std::atomic<HostPointerProvider> g_host_pointer_provider{nullptr};
static std::atomic<HostPointerTransitionProvider>
    g_host_pointer_transition_provider{nullptr};
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

namespace {
// GALAXY_TEST_LAST_INPUT=<n>: input::ControlScheme n is the one in use whatever else gives input, as if attached (hidden
// runs show the menus' buttons for each controller); -1 without it.
int test_last_input_scheme() noexcept {
    static const int s_test_scheme = [] {
        const std::optional<std::string> scheme = read_env_string_uncached("GALAXY_TEST_LAST_INPUT");
        const int value = scheme.has_value() ? std::atoi(scheme->c_str()) : -1;
        return value >= 0 && value < static_cast<int>(input::kControlSchemeCount) ? value : -1;
    }();
    return s_test_scheme;
}
}  // namespace

std::optional<input::DeviceKey> last_input_device() noexcept {
    if (const int test_scheme = test_last_input_scheme(); test_scheme >= 0) {
        return input::DeviceKey{static_cast<input::ControlScheme>(test_scheme),
                                test_scheme == 0 ? input::kKeyboardSlot : std::uint8_t{0}};
    }
    const std::uint32_t key = g_last_input.load(std::memory_order_relaxed);
    if (key == kLastInputNone) return std::nullopt;
    if (key == kLastInputKeyboard) return input::DeviceKey{input::ControlScheme::Keyboard, input::kKeyboardSlot};
    return input::DeviceKey{static_cast<input::ControlScheme>((key >> 8u) & 0xffu),
                            static_cast<std::uint8_t>(key & 0xffu)};
}

void note_keyboard_input() noexcept {
    g_last_input.store(kLastInputKeyboard, std::memory_order_relaxed);
}

// The style Auto shows now (button_icons sets it, the settings menu reads it).
std::atomic<input::ButtonIconStyle> g_auto_icon_style{input::ButtonIconStyle::Wii};

input::ButtonIconStyle button_icons_auto_style() noexcept {
    return g_auto_icon_style.load(std::memory_order_relaxed);
}

namespace {
bool current_process_runtime_window(HWND& window);
}  // namespace

// What the layout keys (input::key_icon_layout_key) print on the keyboard the game's window reads: each one's
// character alone (a dead key's too: a German keyboard's ^ and its acute accent), as a capital; 0 where it prints none.
// Looked up at most once a second. GALAXY_TEST_KEY_LAYOUT=<hex>,... (13 UCS-2 codes) stands in for them: hidden runs
// show another country's keyboard.
std::array<std::uint16_t, input::kKeyIconLayoutCount> keyboard_layout_labels() noexcept {
    static std::array<std::uint16_t, input::kKeyIconLayoutCount> s_labels{};
    static std::uint64_t s_checked_ms = 0u;
    static const std::optional<std::array<std::uint16_t, input::kKeyIconLayoutCount>> s_test = [] {
        const std::optional<std::string> value = read_env_string_uncached("GALAXY_TEST_KEY_LAYOUT");
        if (!value.has_value()) return std::optional<std::array<std::uint16_t, input::kKeyIconLayoutCount>>{};
        std::array<std::uint16_t, input::kKeyIconLayoutCount> labels{};
        const char* p = value->c_str();
        for (std::size_t i = 0; i < labels.size() && *p != '\0'; ++i) {
            char* end = nullptr;
            labels[i] = static_cast<std::uint16_t>(std::strtoul(p, &end, 16));
            p = *end == ',' ? end + 1 : end;
        }
        return std::optional<std::array<std::uint16_t, input::kKeyIconLayoutCount>>(labels);
    }();
    if (s_test.has_value()) return *s_test;
    const std::uint64_t now_ms = GetTickCount64();
    if (s_checked_ms != 0u && now_ms - s_checked_ms < 1000u) return s_labels;
    s_checked_ms = now_ms;
    HWND window = nullptr;
    const HKL layout =
        GetKeyboardLayout(current_process_runtime_window(window) ? GetWindowThreadProcessId(window, nullptr) : 0u);
    for (std::size_t i = 0; i < s_labels.size(); ++i) {
        // The top bit marks a dead key; the character is in the low word.
        wchar_t character[2] = {static_cast<wchar_t>(
                                    MapVirtualKeyExW(input::key_icon_layout_key(i), MAPVK_VK_TO_CHAR, layout) & 0xFFFFu),
                                L'\0'};
        if (character[0] != L'\0') CharUpperBuffW(character, 1u);
        s_labels[i] = static_cast<std::uint16_t>(character[0]);
    }
    return s_labels;
}

input::ButtonIconSpec button_icons(bool menu) noexcept {
    using input::ButtonIconStyle;
    using input::ControlScheme;
    // The game's thread asks each frame.
    // The pads attached: read once a second and when the last input's device changes (the roster asks XInput's empty
    // slots, which is slow).
    static std::array<ControlScheme, input::kMaxControllers> s_attached{};
    static std::size_t s_attached_count = 0u;
    static std::uint64_t s_attached_checked_ms = 0u;
    static std::optional<input::DeviceKey> s_device;
    static std::optional<ControlScheme> s_pad;
    const std::optional<input::DeviceKey> device = last_input_device();
    const std::uint64_t now_ms = GetTickCount64();
    if (device != s_device || now_ms - s_attached_checked_ms >= 1000u) {
        s_device = device;
        s_attached_checked_ms = now_ms;
        std::array<input::ControllerInfo, input::kMaxControllers> attached{};
        s_attached_count = connected_controllers(attached);
        for (std::size_t i = 0; i < s_attached_count; ++i) s_attached[i] = attached[i].scheme;
    }
    const auto attached = [](ControlScheme scheme) {
        return static_cast<int>(scheme) == test_last_input_scheme() ||
               std::find(s_attached.begin(), s_attached.begin() + s_attached_count, scheme) !=
                   s_attached.begin() + s_attached_count;
    };
    // The pad in use while it is attached; else the family remembered while such a pad is attached, else the first
    // pad attached, else none.
    if (device.has_value() && input::scheme_is_gamepad(device->scheme) && attached(device->scheme)) {
        s_pad = device->scheme;
    } else if (s_pad.has_value() && !attached(*s_pad)) {
        s_pad = s_attached_count != 0u ? std::optional<ControlScheme>(s_attached[0]) : std::nullopt;
    }
    const std::optional<ControlScheme> pad = s_pad;
    // Auto's: a real Wii Remote's own while one is in use; the keyboard's while it is (a key or a mouse button pressed
    // since a controller's last input; with no controller attached and none used, from the start), and always in the
    // keyboard and mouse input mode, never in the controller one; else the controller's family (one of no known
    // family shows the Wii's). Moving the mouse alone leaves them (a controller's player may aim with it).
    const RuntimeInputMode mode = get_runtime_settings().input_mode;
    const bool keyboard_last = device.has_value() ? device->scheme == ControlScheme::Keyboard : !pad.has_value();
    const bool keyboard = mode == RuntimeInputMode::KeyboardMouse ||
                          (mode != RuntimeInputMode::Controller && keyboard_last);
    ButtonIconStyle auto_style = ButtonIconStyle::Wii;
    if (real_wiimote_in_use()) {
    } else if (keyboard) {
        auto_style = ButtonIconStyle::Keyboard;
    } else {
        switch (pad.value_or(ControlScheme::Keyboard)) {
        case ControlScheme::Xbox:
        case ControlScheme::Steam: auto_style = ButtonIconStyle::Xbox; break;
        case ControlScheme::SwitchPro:
        case ControlScheme::SwitchPro2: auto_style = ButtonIconStyle::Nintendo; break;
        case ControlScheme::PlayStation: auto_style = ButtonIconStyle::PlayStation; break;
        case ControlScheme::Keyboard:
        case ControlScheme::Generic: break;
        }
    }
    g_auto_icon_style.store(auto_style, std::memory_order_relaxed);
    ButtonIconStyle style = auto_style;
    switch (get_runtime_settings().button_icons) {
    case ButtonIcons::Auto: break;
    case ButtonIcons::Wii: style = ButtonIconStyle::Wii; break;
    case ButtonIcons::Xbox: style = ButtonIconStyle::Xbox; break;
    case ButtonIcons::Nintendo: style = ButtonIconStyle::Nintendo; break;
    case ButtonIcons::PlayStation: style = ButtonIconStyle::PlayStation; break;
    case ButtonIcons::Keyboard: style = ButtonIconStyle::Keyboard; break;
    }
    // The bindings of the controller in use; a style chosen without one takes its own family's. A chosen style shows
    // the bound buttons by place (an 8BitDo pad seen as an Xbox one, with Nintendo's labels). The keyboard's are its own.
    const ControlScheme scheme = style == ButtonIconStyle::Keyboard  ? ControlScheme::Keyboard
                                 : pad.has_value()                   ? *pad
                                 : style == ButtonIconStyle::Nintendo    ? ControlScheme::SwitchPro
                                 : style == ButtonIconStyle::PlayStation ? ControlScheme::PlayStation
                                                                         : ControlScheme::Xbox;
    // Worked out again only when the bindings, the style, the controller or the context change.
    static std::uint64_t s_generation = ~std::uint64_t{0};
    static ButtonIconStyle s_style = ButtonIconStyle::Wii;
    static ControlScheme s_scheme = ControlScheme::Keyboard;
    static bool s_menu = false;
    static input::ButtonIconSpec s_spec;
    const std::uint64_t generation = input::controls_config_generation();
    if (generation != s_generation || style != s_style || scheme != s_scheme || menu != s_menu) {
        s_generation = generation;
        s_style = style;
        s_scheme = scheme;
        s_menu = menu;
        s_spec = input::button_icon_spec(style, input::get_controls_config().table(scheme), menu);
    }
    if (style == ButtonIconStyle::Keyboard) s_spec.layout = keyboard_layout_labels();
    return s_spec;
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
    return static_cast<std::uint64_t>(GetTickCount64());
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
    // The touch surface's finger travel this report (its width and height 1 each) and whether the controller has one.
    bool touch_live = false;
    float touch_dx = 0.0f;
    float touch_dy = 0.0f;
    // Controller up (gravity reaction, SDL frame) when the IMU knows it.
    bool gravity_valid = false;
    galaxy::input::tilt::Vec3 gravity{0.0f, 1.0f, 0.0f};
    // XInput user index or Switch worker slot (with the scheme: identity).
    std::uint8_t device_slot = 0xFF;
};

// Vibration (controller_hid.h): the controller in use, its strength setting and the window focus. The live input
// path sets these each report and the game's motor state reads them when it is published. Game thread only; the
// replay path leaves it empty (no controller vibrates).
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
        live.touch_live = pads[i].touch_live;
        live.touch_dx = pads[i].touch_dx;
        live.touch_dy = pads[i].touch_dy;
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
            pad.touch_dx = pad.touch_dy = 0.0f;
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

galaxy::input::NativeMousePointerProjection project_client_mouse_pointer(
    bool window_focused,
    bool absolute_valid,
    bool inside_client,
    int client_x,
    int client_y,
    int client_width,
    int client_height) {
    const auto actual = galaxy::gx::current_host_client_extent();
    if (actual.width > 1 && actual.height > 1 &&
        (actual.width != client_width ||
         actual.height != client_height)) {
        // A mouse sample from before a resize is not projected through the new window shape while the game's
        // aspect is being latched.
        return {};
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
        1.0f,
        1.0f,
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

// A replayed XInput pad uses Windows' recommended thumbstick dead zones, a trigger threshold of 96 and the default
// pointer speeds.
constexpr std::int16_t kReplayLeftDeadzone = XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE;
constexpr std::int16_t kReplayPointerDeadzone = XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE;
constexpr std::uint8_t kXInputTriggerThreshold = 96u;
constexpr float kReplayStickPointerSpeed = 2.4f;
constexpr float kReplayKeyboardPointerSpeed = 1.6f;

bool replay_xinput_has_input(const NativeInputReplaySample& sample) {
    if (!sample.xinput_connected) {
        return false;
    }
    return sample.xinput_buttons != 0u ||
           sample.left_trigger >= kXInputTriggerThreshold ||
           sample.right_trigger >= kXInputTriggerThreshold ||
           std::fabs(galaxy::input::normalize_xinput_axis(
               sample.thumb_lx, kReplayLeftDeadzone)) >
               0.001f ||
           std::fabs(galaxy::input::normalize_xinput_axis(
               sample.thumb_ly, kReplayLeftDeadzone)) >
               0.001f ||
           std::fabs(galaxy::input::normalize_xinput_axis(
               sample.thumb_rx, kReplayPointerDeadzone)) >
               0.001f ||
           std::fabs(galaxy::input::normalize_xinput_axis(
               sample.thumb_ry, kReplayPointerDeadzone)) >
               0.001f;
}

// How long the mouse pointer stays shown through invalid cursor samples after its last sample inside the window.
constexpr ULONGLONG kMousePointerLossHoldMs = 500;

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

void apply_script_button_token(
    NativeInputScriptOverlay& overlay,
    const std::string& token,
    const char* script_name) {
    const std::string name = uppercase_ascii(token);
    if (name == "A") {
        overlay.buttons.a = true;
    } else if (name == "B") {
        overlay.buttons.b = true;
    } else if (name == "PLUS" || name == "START") {
        overlay.buttons.plus = true;
    } else if (name == "MINUS" || name == "SELECT" || name == "BACK") {
        overlay.buttons.minus = true;
    } else if (name == "ONE" || name == "1") {
        overlay.buttons.one = true;
    } else if (name == "TWO" || name == "2") {
        overlay.buttons.two = true;
    } else if (name == "HOME") {
        overlay.buttons.home = true;
    } else if (name == "LEFT") {
        overlay.buttons.left = true;
    } else if (name == "RIGHT") {
        overlay.buttons.right = true;
    } else if (name == "UP") {
        overlay.buttons.up = true;
    } else if (name == "DOWN") {
        overlay.buttons.down = true;
    } else if (name == "C") {
        overlay.nunchuk_c = true;
    } else if (name == "Z") {
        overlay.nunchuk_z = true;
    } else if (name == "SHAKE") {
        overlay.shake = true;
    } else {
        throw std::runtime_error(
            std::string("unsupported native input script button '") + token +
            "' in " + script_name);
    }
}

void apply_script_buttons(
    NativeInputScriptOverlay& overlay,
    const std::string& text,
    const char* script_name) {
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
        apply_script_button_token(overlay, token, script_name);
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

// GALAXY_INPUT_AUTOPRESS_SCRIPT_STOP_AT_MARIO_CONTROL ends the script at the frame
// GALAXY_INPUT_AUTOPRESS_SCRIPT_STOP_FALLBACK_VI names.
void apply_button_script_env(
    NativeInputScriptOverlay& overlay,
    std::uint64_t current_vi,
    const char* env_name) {
    const std::optional<std::string> script = read_env_string(env_name);
    if (!script.has_value()) {
        return;
    }
    if (read_env_flag(
            "GALAXY_INPUT_AUTOPRESS_SCRIPT_STOP_AT_MARIO_CONTROL")) {
        const std::optional<std::uint64_t> fallback_vi =
            read_env_u64_optional(
                "GALAXY_INPUT_AUTOPRESS_SCRIPT_STOP_FALLBACK_VI");
        if (fallback_vi.has_value() && current_vi >= *fallback_vi) {
            return;
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
            apply_script_buttons(overlay, fields[2], env_name);
            // A step's first report is logged with its time, as a pointer jump is, for input-to-present measurements.
            static std::string logged;  // under the report lock (native_host.h), as every report is built
            const std::string step = fields[0] + ':' + fields[2];
            if (current_vi == start && logged != step) {
                logged = step;
                std::cerr << "[latency] buttons " << fields[2] << " vi=" << current_vi << " qpc=" << galaxy::qpc_now()
                          << '\n';
            }
        }
    }
}

// GALAXY_INPUT_STICK_SCRIPT_RELATIVE_MARIO_CONTROL counts the script's times from the frame
// GALAXY_INPUT_STICK_SCRIPT_RELATIVE_FALLBACK_VI names.
void apply_stick_script_env(
    NativeInputScriptOverlay& overlay,
    std::uint64_t current_vi,
    const char* env_name) {
    const std::optional<std::string> script = read_env_string(env_name);
    if (!script.has_value()) {
        return;
    }
    std::uint64_t anchor = 0;
    if (read_env_flag("GALAXY_INPUT_STICK_SCRIPT_RELATIVE_MARIO_CONTROL")) {
        anchor = read_env_u64_optional(
                     "GALAXY_INPUT_STICK_SCRIPT_RELATIVE_FALLBACK_VI")
                     .value_or(0);
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
        }
    }
}

// GALAXY_INPUT_STICK_COMMAND_FILE=<file>: a stick (and A) command another process writes while the game runs
// (diagnostic_stick_lease.h), read at most every 100 ms.
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
        polled = true;
        last_poll_ms = now_ms;
        const HANDLE file = CreateFileA(
            path->c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            lease.cancel();
        } else {
            // A bounded read rejects a larger or partly written command; the writer replaces the file atomically.
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
    }
    if (const auto active = lease.active(now_ms); active.has_value()) {
        overlay.has_stick = true;
        overlay.stick_x = active->x;
        overlay.stick_y = active->y;
        overlay.buttons.a = overlay.buttons.a || active->a;
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
}

struct PointerPathPoint {
    std::uint64_t vi;
    float x;
    float y;
};

// GALAXY_INPUT_POINTER_PATH "vi:x:y;vi:x:y;...": the pointer glides between those points (VIs ascending), on screen
// from the first to the last. Parsed again only when the value changes.
void apply_pointer_path_env(
    NativeInputScriptOverlay& overlay,
    std::uint64_t current_vi) {
    const std::optional<std::string> script = read_env_string("GALAXY_INPUT_POINTER_PATH");
    if (!script.has_value()) {
        return;
    }
    thread_local std::string parsed;
    thread_local std::vector<PointerPathPoint> points;
    if (*script != parsed) {
        std::vector<PointerPathPoint> next;
        for (const std::string& segment : split_script_fields(*script, ';')) {
            if (segment.empty()) {
                continue;
            }
            const std::vector<std::string> fields = split_script_fields(segment, ':');
            if (fields.size() != 3u) {
                throw std::runtime_error("invalid native input pointer path segment");
            }
            const PointerPathPoint point{parse_script_u64(fields[0], "GALAXY_INPUT_POINTER_PATH"),
                                         parse_script_float(fields[1], "GALAXY_INPUT_POINTER_PATH"),
                                         parse_script_float(fields[2], "GALAXY_INPUT_POINTER_PATH")};
            if (!next.empty() && point.vi < next.back().vi) {
                throw std::runtime_error("native input pointer path VIs must ascend");
            }
            next.push_back(point);
        }
        points = std::move(next);
        parsed = *script;
    }
    if (points.empty() || current_vi < points.front().vi || current_vi > points.back().vi) {
        return;
    }
    // The first point at or after this VI, and the one before it.
    const auto after = std::lower_bound(points.begin(), points.end(), current_vi,
                                        [](const PointerPathPoint& point, std::uint64_t vi) { return point.vi < vi; });
    float x = after->x;
    float y = after->y;
    if (after != points.begin() && after->vi != current_vi) {
        const PointerPathPoint& before = *(after - 1);
        const float t = static_cast<float>(current_vi - before.vi) / static_cast<float>(after->vi - before.vi);
        x = before.x + (after->x - before.x) * t;
        y = before.y + (after->y - before.y) * t;
    }
    overlay.has_pointer = true;
    overlay.pointer_x = std::clamp(x, -1.0f, 1.0f);
    overlay.pointer_y = std::clamp(y, -1.0f, 1.0f);
    // A jump (a quarter of the picture or more in one step) is logged with its time: the first report that carries it
    // is built now, for input-to-present measurements against the present snapshots' times.
    static float last_x = x;  // under the report lock, as every report is built
    static float last_y = y;
    if (std::fabs(x - last_x) + std::fabs(y - last_y) >= 0.5f) {
        std::cerr << "[latency] pointer jump vi=" << current_vi << " x=" << x << " y=" << y << " qpc=" << galaxy::qpc_now()
                  << '\n';
    }
    last_x = x;
    last_y = y;
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
    // Values are read at every report, so a test can change them while the game runs; a later waypoint takes
    // precedence over an earlier one.
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
    apply_pointer_path_env(overlay, current_vi);
}

NativeInputScriptOverlay build_native_input_script_overlay(std::uint64_t current_vi) {
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
    apply_button_script_env(overlay, current_vi, "GALAXY_INPUT_AUTOPRESS_SCRIPT");
    apply_stick_script_env(overlay, current_vi, "GALAXY_INPUT_STICK_SCRIPT");
    apply_diagnostic_stick_command_file(overlay);
    apply_shake_script_env(
        overlay, current_vi, "GALAXY_INPUT_SHAKE_SCRIPT");
    apply_pointer_script_envs(overlay, current_vi);
    return overlay;
}

// In a tilt section (tilt.h) the virtual remote's accelerometer reports the controller's tilt or the stick's;
// elsewhere it keeps its rest reading.
void apply_tilt_accel(galaxy::input::WiimoteInputSnapshot& snapshot,
                      galaxy::input::tilt::Tilt& state,
                      galaxy::input::tilt::Section section,
                      const galaxy::input::tilt::Vec3* controller_up,
                      float stick_x,
                      float stick_y,
                      bool recenter) {
    const galaxy::input::tilt::Tilt::Output tilt =
        state.update(section, controller_up, stick_x, stick_y, recenter);
    if (!tilt.active) {
        return;
    }
    const galaxy::input::tilt::Counts counts = galaxy::input::tilt::accel_counts(tilt.game);
    snapshot.accel.x = counts.x;
    snapshot.accel.y = counts.y;
    snapshot.accel.z = counts.z;
}

// GALAXY_INPUT_REPLAY_LOG: one recorded sample of window, keyboard, mouse and XInput state as a Wii Remote report.
galaxy::input::WiimoteInputSnapshot build_replayed_hid_input_snapshot(
    std::uint64_t current_vi,
    const NativeInputReplaySample& replay,
    const NativeInputScriptOverlay& script,
    galaxy::RuntimeInputMode input_mode,
    bool ir_input_enabled,
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
               replay.right_trigger >= kXInputTriggerThreshold)
            : (key(1) || key(5) || key(6));

    float stick_x =
        use_controller
            ? galaxy::input::normalize_xinput_axis(
                  replay.thumb_lx, kReplayLeftDeadzone)
            : replay_key_axis(replay, 16, 13);
    float stick_y =
        use_controller
            ? galaxy::input::normalize_xinput_axis(
                  replay.thumb_ly, kReplayLeftDeadzone)
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
               replay.left_trigger >= kXInputTriggerThreshold)
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
              static_cast<float>(kReplayPointerDeadzone) / 32767.0f)
        : galaxy::input::StickVector{};
    const galaxy::input::StickVector stick_velocity =
        galaxy::input::stick_pointer_velocity(pointer_axes, kReplayStickPointerSpeed);
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
        }
        break;
    case galaxy::input::PointerSource::Keys:
        s_replay_pointer_x = std::clamp(s_replay_pointer_x + key_x * kReplayKeyboardPointerSpeed * dt, -1.0f, 1.0f);
        s_replay_pointer_y = std::clamp(s_replay_pointer_y - key_y * kReplayKeyboardPointerSpeed * dt, -1.0f, 1.0f);
        pointer_active = true;
        break;
    case galaxy::input::PointerSource::Stick:
        s_replay_pointer_x = std::clamp(s_replay_pointer_x + stick_velocity.x * dt, -1.0f, 1.0f);
        s_replay_pointer_y = std::clamp(s_replay_pointer_y - stick_velocity.y * dt, -1.0f, 1.0f);
        pointer_active = true;
        break;
    case galaxy::input::PointerSource::Gyro:
    case galaxy::input::PointerSource::None:
        if (pad_allowed) {
            pointer_active = true;
        }
        break;
    }
    if (recenter.pinned) {
        // Held recenter: the pointer stays at the centre (pointer_control.h).
        s_replay_pointer_x = 0.0f;
        s_replay_pointer_y = 0.0f;
        pointer_active = true;
    }
    if (script.has_pointer) {
        s_replay_pointer_x = script.pointer_x;
        s_replay_pointer_y = script.pointer_y;
        pointer_active = true;
    }
    galaxy::input::publish_wiimote_ir_dots(
        snapshot, s_replay_pointer_x, s_replay_pointer_y, pointer_active && ir_input_enabled);

    // Tilt sections steer with the replayed stick (tilt.h).
    static galaxy::input::tilt::Tilt s_replay_tilt{};
    apply_tilt_accel(snapshot, s_replay_tilt, tilt_section, nullptr, stick_x, stick_y, recenter.pressed);

    const bool shake_pressed =
        script.shake ||
        (use_controller
             ? (pad(XINPUT_GAMEPAD_X) || pad(XINPUT_GAMEPAD_Y) ||
                pad(XINPUT_GAMEPAD_RIGHT_SHOULDER))
             : (key(17) || key(22)));
    snapshot.shake = shake_pressed;
    return snapshot;
}

// The live controls as one Wii Remote report: the bindings of every connected controller and of the keyboard and
// mouse, the pointer from whichever source moved last, tilt, shake and the scripted test input.
galaxy::input::WiimoteInputSnapshot build_native_hid_input_snapshot(
    std::uint64_t current_vi,
    const galaxy::RuntimeInputModeState& input_mode_state,
    bool ir_input_enabled,
    galaxy::input::PlayContext play_context,
    galaxy::input::tilt::Section tilt_section) {
    galaxy::input::WiimoteInputSnapshot snapshot{};
    const NativeInputScriptOverlay script = build_native_input_script_overlay(current_vi);
    if (const NativeInputReplaySample* replay =
            native_input_replay_sample_for_vi(current_vi);
        replay != nullptr) {
        // Replayed pads reach the settings overlay the way live pads do.
        NativeInputReplaySample routed = *replay;
        if (routed.xinput_connected &&
            route_native_settings_pad(
                routed.xinput_buttons,
                galaxy::input::normalize_xinput_axis(
                    routed.thumb_lx, kReplayLeftDeadzone),
                galaxy::input::normalize_xinput_axis(
                    routed.thumb_ly, kReplayLeftDeadzone))) {
            routed.left_trigger = routed.right_trigger = 0u;
            routed.thumb_lx = routed.thumb_ly = 0;
            routed.thumb_rx = routed.thumb_ry = 0;
        }
        return build_replayed_hid_input_snapshot(
            current_vi,
            routed,
            script,
            input_mode_state.mode,
            ir_input_enabled,
            tilt_section);
    }

    // A hidden test window (GALAXY_HIDE_WINDOW) is no keyboard or mouse source: no focus, key or cursor calls are
    // made, while controllers, replay and scripts still work.
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
    const std::uint32_t capture_armed = g_binding_capture_armed.load(std::memory_order_acquire);
    const galaxy::input::ControlScheme capture_scheme = capture_scheme_of(capture_armed);
    const bool capturing = capture_armed != 0u;
    const bool controller_mode_polls =
        (input_mode == galaxy::RuntimeInputMode::Controller ||
         input_mode == galaxy::RuntimeInputMode::Auto) &&
        (!xinput_requires_focus() || window_focused);
    const bool poll_controller =
        controller_mode_polls || (capturing && capture_scheme == galaxy::input::ControlScheme::Xbox);
    const galaxy::input::SchemeTuning& xbox_tuning =
        controls.config.tune(galaxy::input::ControlScheme::Xbox);
    // Every XInput pad is read; the right stick arrives raw and the pointer
    // takes a radial dead zone.
    std::array<NativeHidControllerState, XUSER_MAX_COUNT> xinput_pads{};
    const std::size_t xinput_count = poll_controller
        ? poll_native_xinput_pads(deadzone_counts(xbox_tuning.stick_deadzone), xinput_pads)
        : 0u;
    if (poll_controller) {
        std::uint32_t xinput_mask = 0u;
        for (std::size_t i = 0; i < xinput_count; ++i) xinput_mask |= 1u << xinput_pads[i].slot;
        g_xinput_mask.store(xinput_mask, std::memory_order_release);
        g_xinput_looked_ms.store(GetTickCount64(), std::memory_order_relaxed);
    }
    const float xinput_pointer_deadzone_fraction =
        static_cast<float>(deadzone_counts(xbox_tuning.pointer_deadzone)) / 32767.0f;
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
            std::fabs(pad.gyro_x) + std::fabs(pad.gyro_y) > 0.003f ||
            std::fabs(pad.touch_dx) + std::fabs(pad.touch_dy) > 0.0005f) {
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
    const bool controller_connected = std::any_of(live_pads.begin(), live_pads.end(),
        [](const LivePad& pad) { return pad.sample.connected; });
    const bool use_pads = controller_mode_polls && controller_connected;
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
    const float trigger_threshold =
        static_cast<float>(kXInputTriggerThreshold) / 255.0f;

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
    // The touch pointer's travel this report (pointer units, 2 = the picture's width and height; y down).
    float touch_x = 0.0f;
    float touch_y = 0.0f;
    bool pad_touch_pointing = false;
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
                               std::fabs(pad.gyro_x) + std::fabs(pad.gyro_y) > 0.003f ||
                               std::fabs(pad.touch_dx) + std::fabs(pad.touch_dy) > 0.0005f;
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
            const bool touch_pointing = tuning.touch_pointer && pad.touch_live;
            galaxy::input::LogicalInput pad_logic = galaxy::input::evaluate_pad_bindings(
                controls.config.table(pad.sample.scheme), pad.sample, trigger_threshold,
                s_pad_previous_pressed[index], gyro_pointing || touch_pointing);
            s_pad_previous_pressed[index] = pad_logic.pressed;
            if (tuning.invert_pointer_y) pad_logic.pointer_y = -pad_logic.pointer_y;
            if (std::fabs(pad_logic.pointer_x) + std::fabs(pad_logic.pointer_y) >
                std::fabs(pad_logic_all.pointer_x) + std::fabs(pad_logic_all.pointer_y)) {
                pad_pointer_speed = tuning.pointer_speed;
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
            if (touch_pointing) {
                pad_touch_pointing = true;
                touch_x += pad.touch_dx * 2.0f * tuning.touch_sensitivity;
                touch_y += (tuning.invert_pointer_y ? -pad.touch_dy : pad.touch_dy) * 2.0f * tuning.touch_sensitivity;
                if (!pad_gyro_pointing) {
                    pointer_hide_seconds = tuning.pointer_hide_seconds;
                    pointer_hide_radius = tuning.pointer_hide_radius;
                }
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

    static float s_pointer_x = 0.0f;
    static float s_pointer_y = 0.0f;
    static float s_mouse_x = 0.0f;
    static float s_mouse_y = 0.0f;
    static ULONGLONG s_last_mouse_inside_tick = 0;
    static ULONGLONG s_last_live_mouse_tick = 0;
    static std::uint64_t s_last_host_pointer_absolute_sequence = 0;
    static std::uint64_t s_last_host_pointer_transition_sequence = 0;
    static bool s_mouse_pointer_seen_inside = false;
    const ULONGLONG live_mouse_script_grace_ms =
        script_pointer_live_mouse_grace_ms();
    const std::uint64_t now = host_pointer_now_ms();

    bool pointer_active = false;
    bool live_mouse_recent = false;
    // The mouse this report: where it points, whether that is on the picture
    // and whether it moved (a new physical sample inside the client).
    bool mouse_visible = false;
    bool mouse_moved = false;
    bool mouse_button_held = false;
    const bool drop_injected_mouse = drop_injected && native_pointer_input_injected(false);
    bool host_pointer_polled = false;
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
            }
            host_pointer_polled = true;
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
                    const auto mouse = [mouse_targets](WiiTarget target) {
                        return (mouse_targets & galaxy::input::wii_target_bit(target)) != 0u;
                    };
                    snapshot.buttons.a = snapshot.buttons.a || mouse(WiiTarget::A);
                    snapshot.buttons.b = snapshot.buttons.b || mouse(WiiTarget::B);
                    snapshot.buttons.one = snapshot.buttons.one || mouse(WiiTarget::One);
                    snapshot.buttons.two = snapshot.buttons.two || mouse(WiiTarget::Two);
                    snapshot.buttons.plus = snapshot.buttons.plus || mouse(WiiTarget::Plus);
                    snapshot.buttons.minus = snapshot.buttons.minus || mouse(WiiTarget::Minus);
                    snapshot.buttons.home = snapshot.buttons.home || mouse(WiiTarget::Home);
                    snapshot.buttons.up = snapshot.buttons.up || mouse(WiiTarget::DpadUp);
                    snapshot.buttons.down = snapshot.buttons.down || mouse(WiiTarget::DpadDown);
                    snapshot.buttons.left = snapshot.buttons.left || mouse(WiiTarget::DpadLeft);
                    snapshot.buttons.right = snapshot.buttons.right || mouse(WiiTarget::DpadRight);
                    snapshot.nunchuk.c = snapshot.nunchuk.c || mouse(WiiTarget::C);
                    snapshot.nunchuk.z = snapshot.nunchuk.z || mouse(WiiTarget::Z);
                    logic.pressed |= mouse_targets;
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
                // Only a new inside sample, or a queued button edge with its own coordinates, renews the hold
                // through invalid samples; repeated polls of the same coordinate do not.
                if (host_pointer_has_fresh_coordinates) {
                    s_last_mouse_inside_tick = now;
                    s_mouse_pointer_seen_inside = true;
                    mouse_moved = !drop_injected_mouse;
                }
                mouse_visible = true;
            } else if (pointer_availability.invalid_sample_hold_allowed &&
                       s_mouse_pointer_seen_inside &&
                       now >= s_last_mouse_inside_tick &&
                       now - s_last_mouse_inside_tick <=
                           kMousePointerLossHoldMs) {
                // Remote Desktop can fail absolute cursor sampling for a moment while the pointer is still over the
                // client: an invalid sample is uncertainty, not a lost sensor bar.
                mouse_visible = true;
            }
        }
    }
    if (allow_absolute_pointer && !mouse_visible && !host_pointer_polled) {
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
                // The pointer tracks only while the cursor is over the client area (all of the screen in
                // fullscreen). In a window it drops out when the cursor leaves, as a Wii Remote does when it
                // loses the sensor bar.
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
            }
        }
    }

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
    const float keyboard_speed =
        controls.config.tune(galaxy::input::ControlScheme::Keyboard).pointer_speed;
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
    activity.touch = pad_touch_pointing && std::fabs(touch_x) + std::fabs(touch_y) > 0.0f;
    // Recenter (pointer_control.h RecenterHold): the press gives the pointer to the recentering device
    // at the picture centre. While the button is held, the pointer stays there and that device keeps it.
    const bool recenter_held = held(WiiTarget::PointerRecenter);
    static galaxy::input::RecenterHold s_recenter{};
    const galaxy::input::RecenterHold::Output recenter = s_recenter.update(recenter_held);
    if (recenter.pressed) {
        const bool pad_recenter = pad_logic_all.held(WiiTarget::PointerRecenter);
        s_pointer_owner.owner = pad_recenter
            ? (pad_gyro_pointing    ? galaxy::input::PointerSource::Gyro
               : pad_touch_pointing ? galaxy::input::PointerSource::Touch
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
        }
        break;
    case galaxy::input::PointerSource::Keys:
        s_pointer_x = std::clamp(s_pointer_x + keyboard_pointer_x * keyboard_speed * dt,
                                 -galaxy::input::kStickPointerLimit, galaxy::input::kStickPointerLimit);
        s_pointer_y = std::clamp(s_pointer_y - keyboard_pointer_y * keyboard_speed * dt,
                                 -galaxy::input::kStickPointerLimit, galaxy::input::kStickPointerLimit);
        pointer_active = true;
        break;
    case galaxy::input::PointerSource::Stick:
        s_pointer_x = std::clamp(s_pointer_x + stick_velocity.x * dt,
                                 -galaxy::input::kStickPointerLimit, galaxy::input::kStickPointerLimit);
        s_pointer_y = std::clamp(s_pointer_y - stick_velocity.y * dt,
                                 -galaxy::input::kStickPointerLimit, galaxy::input::kStickPointerLimit);
        pointer_active = true;
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
        break;
    case galaxy::input::PointerSource::Touch:
        // The finger moves it as a touchpad moves a cursor; it stays on the picture.
        s_pointer_x = std::clamp(s_pointer_x + touch_x, -galaxy::input::kStickPointerLimit,
                                 galaxy::input::kStickPointerLimit);
        s_pointer_y = std::clamp(s_pointer_y + touch_y, -galaxy::input::kStickPointerLimit,
                                 galaxy::input::kStickPointerLimit);
        pointer_active = true;
        break;
    case galaxy::input::PointerSource::None:
        break;
    }
    if (recenter.pinned) {
        // Gyro and stick motion while the button is held is discarded.
        s_pointer_x = 0.0f;
        s_pointer_y = 0.0f;
        pointer_active = true;
    }
    // A connected controller keeps the pointer on screen until the mouse
    // owns it.
    if (!pointer_active && use_pads && owner != galaxy::input::PointerSource::Mouse &&
        owner != galaxy::input::PointerSource::Gyro) {
        pointer_active = true;
    }
    // Still-pointer hiding (pointer_control.h PointerRestHide): a hidden gyro
    // pointer comes back only with recenter; a stick pointer also with a push
    // or a move out of the radius.
    static galaxy::input::PointerRestHide s_rest_hide{};
    const bool controller_owned = owner == galaxy::input::PointerSource::Stick ||
                                  owner == galaxy::input::PointerSource::Gyro ||
                                  owner == galaxy::input::PointerSource::Touch ||
                                  (owner == galaxy::input::PointerSource::None && use_pads);
    const bool rest_hidden = s_rest_hide.update(
        s_pointer_x, s_pointer_y, now_us, controller_owned,
        recenter.pinned || recenter.released ||
            (owner == galaxy::input::PointerSource::Stick && activity.stick) ||
            (owner == galaxy::input::PointerSource::Touch && activity.touch),
        owner == galaxy::input::PointerSource::Gyro,
        pointer_hide_seconds, pointer_hide_radius);
    if (rest_hidden) {
        pointer_active = false;
    }
    // In menus a key press acts on the pointer's position, so a resting mouse pointer stays.
    static galaxy::input::MouseRestHide s_mouse_rest_hide{};
    if (s_mouse_rest_hide.update(owner == galaxy::input::PointerSource::Mouse && pointer_active && window_focused &&
                                     play_context == galaxy::input::PlayContext::Play,
                                 mouse_moved || mouse_button_held, now_us)) {
        pointer_active = false;
    }
    if (script.has_pointer &&
        (script_pointer_force_enabled() || !live_mouse_recent ||
         !pointer_active)) {
        s_pointer_x = script.pointer_x;
        s_pointer_y = script.pointer_y;
        pointer_active = true;
    }
    galaxy::input::publish_wiimote_ir_dots(
        snapshot, s_pointer_x, s_pointer_y, pointer_active && ir_input_enabled);

    static galaxy::input::tilt::Tilt s_tilt{};
    apply_tilt_accel(snapshot, s_tilt, tilt_section, tilt_up, stick_x, stick_y, recenter.pressed);

    const bool shake_pressed =
        script.shake || held(WiiTarget::Shake) || motion_shake_remote;
    static bool s_previous_shake = false;
    if (shake_pressed && !s_previous_shake) {
        std::cerr << "[input-shake] vi=" << current_vi << " source="
                  << (motion_shake_remote ? "motion" : held(WiiTarget::Shake) ? "binding" : "script")
                  << " tilt-section=" << galaxy::input::tilt::section_name(tilt_section) << '\n';
    }
    s_previous_shake = shake_pressed;
    snapshot.shake = shake_pressed;
    snapshot.nunchuk_shake = held(WiiTarget::NunchukShake) || motion_shake_nunchuk;
    return snapshot;
}

}  // namespace

galaxy::input::WiimoteInputSnapshot native_game_wiimote_snapshot(
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
        current_vi, galaxy::get_runtime_input_mode_state(), ir_enabled, context, section);
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
// An address's bytes in the native game's regions (the arena's physical, cached and uncached views, and the module
// image).
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
        // The volume categories, once an audio frame; the master volume applies at the output.
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
    return state_->sink.submit_pcm16_be_stereo(block, sample_rate);
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

// The Windows host keeps the game and its sound running while minimised.
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

void set_env_flag_cache_enabled(bool enabled) noexcept {
    g_env_flag_cache_enabled.store(enabled, std::memory_order_release);
}

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
