// The native game's Windows host services: the game's audio on XAudio2, keyboard, mouse and controllers as Wii
// Remote input reports (with the scripted and replayed test input), vibration and the virtual Wii devices.
#include "galaxy/native_host.h"
#include "galaxy/host_env.h"

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
#include "galaxy/renderer_api.h"
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

    // One AI block: 16-bit (right, left) pairs in host byte order. Always called from the same thread.
    bool submit_pcm16_stereo(std::span<const std::byte> source, std::uint32_t sample_rate) {
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
                if (!native_audio_convert_ai_pcm16(source, pcm_le)) {
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
        if (!native_audio_convert_ai_pcm16(
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
        if (!native_audio_convert_ai_pcm16(source, std::span<std::byte>(pcm_le))) {
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
            // The device can be lost between the offline check in submit_pcm16_stereo and here. The worker then
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


struct NativeGameAudio::State {
    JAudioHostMixer mixer;
    NativeAudioSink sink;
};

NativeGameAudio::NativeGameAudio() : state_(std::make_unique<State>()) {}
NativeGameAudio::~NativeGameAudio() = default;

void NativeGameAudio::setup(std::uint32_t channels, NebulaJAudioChannel* channel_records,
                            const std::int16_t* resample_filter, const std::int16_t* adpcm_filter,
                            const NebulaJAudioFxLine* fx_lines) {
    state_->mixer.setup(channels, channel_records, resample_filter, adpcm_filter, fx_lines);
}

void NativeGameAudio::set_aux(const void* base, std::uint32_t size) {
    state_->mixer.set_aux(static_cast<const std::byte*>(base), size);
}

void NativeGameAudio::frame(std::uint32_t subframes, std::uint32_t mixer_level, std::int16_t* left,
                            std::int16_t* right) {
    // The volume categories, once an audio frame; the master volume applies at the output.
    const galaxy::RuntimeAudioSettings audio = galaxy::get_runtime_settings().audio;
    state_->mixer.set_bus_gains({1.0f, audio.music, audio.voice, audio.sfx, audio.ambience, audio.cutscene});
    state_->mixer.sync_frame(subframes, mixer_level, left, right);
}

bool NativeGameAudio::mix(std::uint32_t group, std::uint32_t active) {
    return state_->mixer.release_halt(group, active);
}

void NativeGameAudio::set_channel_owner(std::uint32_t (*owner)(std::uint32_t, std::uint32_t*)) {
    state_->mixer.set_owner_resolver(owner);
}

bool NativeGameAudio::play(std::span<const std::byte> block, std::uint32_t sample_rate) {
    return state_->sink.submit_pcm16_stereo(block, sample_rate);
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
         << " oscillators=" << s.oscillator_renders << " fir=" << s.fir_renders
         << " iir=" << s.iir_renders << " auto-mixer=" << s.auto_mixer_renders
         << " fx-line-subframes=" << s.fx_line_subframes << " unresolved=" << s.unresolved_reads
         << " unsupported-osc=" << s.unsupported_oscillators
         << " stray=" << s.stray_releases << " categories=" << s.category_decisions
         << " owner-misses=" << s.category_owner_misses
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
}  // namespace galaxy::host
