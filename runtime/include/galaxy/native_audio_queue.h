// Policy pieces of the native audio sink (native_host.cpp) that need no audio device: the AI sample layout, the
// initialization gate, the source voice's frequency ratio and playback lifecycle, and the callback ledger.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace galaxy::host {

// A lost or missing output device (HRESULT values as unsigned): the sink keeps running without sound. Any other
// error is fatal to the output.
[[nodiscard]] constexpr bool native_audio_endpoint_error_recoverable(std::uint32_t error) noexcept {
    return error == 0x80070490u || // ERROR_NOT_FOUND
        error == 0x88960004u ||    // XAUDIO2_E_DEVICE_INVALIDATED
        error == 0x88890004u;      // AUDCLNT_E_DEVICE_INVALIDATED
}

struct NativeAudioCallbackBufferObservation {
    bool completed{};
    bool started{};
};

// Completion must be read before start. OnBufferEnd follows the release publication in OnBufferStart; reading in
// the reverse order can combine an old false start with a new true completion.
template<class ReadCompleted, class ReadStarted>
[[nodiscard]] inline NativeAudioCallbackBufferObservation
observe_callback_buffer_state(ReadCompleted&& read_completed,
                              ReadStarted&& read_started) {
    const bool completed = read_completed();
    const bool started = read_started();
    return {completed, started};
}

// Wii AI DMA stores each stereo frame as big-endian right, left; the output and the WAV writer take little-endian
// left, right. Adapted from WiiCompiled runtime/src/audio_backend.cpp::PushWiiAiSamplesBE16,
// GPLv3, https://github.com/patchzyy/Wiicompiled/blob/83463764b8acda394e058b0c689a10b8561fc380/runtime/src/audio_backend.cpp
[[nodiscard]] inline bool native_audio_convert_wii_ai_pcm16(
    std::span<const std::byte> source,
    std::span<std::byte> destination) noexcept {
    if (source.size() != destination.size() || (source.size() & 3u) != 0u) {
        return false;
    }
    for (std::size_t i = 0; i < source.size(); i += 4u) {
        destination[i] = source[i + 3u];
        destination[i + 1u] = source[i + 2u];
        destination[i + 2u] = source[i + 1u];
        destination[i + 3u] = source[i];
    }
    return true;
}

enum class NativeAudioInitializationState : std::uint8_t {
    Uninitialized,
    Initializing,
    Ready,
    Failed,
};

// The sink opens the device on the first submitted block, once: a failed initialization is not retried.
class NativeAudioInitializationGate final {
public:
    template <typename Initializer>
    bool ensure_ready_for_submit(Initializer&& initializer) {
        if (state_ == NativeAudioInitializationState::Ready) {
            return true;
        }
        if (state_ == NativeAudioInitializationState::Failed ||
            state_ == NativeAudioInitializationState::Initializing) {
            return false;
        }
        state_ = NativeAudioInitializationState::Initializing;
        bool initialized = false;
        try {
            initialized = static_cast<bool>(
                std::forward<Initializer>(initializer)());
        } catch (...) {
            state_ = NativeAudioInitializationState::Failed;
            throw;
        }
        state_ = initialized
            ? NativeAudioInitializationState::Ready
            : NativeAudioInitializationState::Failed;
        return initialized;
    }

private:
    NativeAudioInitializationState state_{
        NativeAudioInitializationState::Uninitialized};
};

inline constexpr std::uint32_t kNativeAudioVoiceBaseSampleRate = 32'000u;
inline constexpr float kNativeAudioMaxFrequencyRatio = 1.5f;

struct NativeAudioFrequencyRatioDecision {
    float ratio{};
    bool valid{};
};

// The source voice keeps one 32 kHz PCM format; XAudio2 plays the same samples at 32 or 48 kHz through the frequency
// ratio. Both ratios are exact binary floats.
[[nodiscard]] constexpr NativeAudioFrequencyRatioDecision
native_audio_frequency_ratio_for_sample_rate(
    std::uint32_t sample_rate) noexcept {
    switch (sample_rate) {
    case 32'000u:
        return {1.0f, true};
    case 48'000u:
        return {1.5f, true};
    default:
        return {};
    }
}

struct NativeAudioFrequencyRatioBoundaryDecision {
    float ratio{};
    bool valid{};
    bool apply_required{};
};

// `active_sample_rate` zero: no ratio set yet on this voice, so its first buffer sets one explicitly, even the
// nominal 1.0. Later buffers call XAudio2 only when the rate changes.
[[nodiscard]] constexpr NativeAudioFrequencyRatioBoundaryDecision
native_audio_frequency_ratio_at_boundary(
    std::uint32_t active_sample_rate,
    std::uint32_t requested_sample_rate) noexcept {
    const NativeAudioFrequencyRatioDecision requested =
        native_audio_frequency_ratio_for_sample_rate(requested_sample_rate);
    return {
        requested.ratio,
        requested.valid,
        requested.valid &&
            (active_sample_rate == 0u ||
             active_sample_rate != requested_sample_rate),
    };
}

static_assert(
    static_cast<float>(kNativeAudioVoiceBaseSampleRate) *
            native_audio_frequency_ratio_for_sample_rate(32'000u).ratio ==
        32'000.0f);
static_assert(
    static_cast<float>(kNativeAudioVoiceBaseSampleRate) *
            native_audio_frequency_ratio_for_sample_rate(48'000u).ratio ==
        48'000.0f);

enum class NativeAudioPlaybackState : std::uint8_t {
    Stopped,
    Running,
    Recovering,
};

enum class NativeAudioPlaybackAction : std::uint8_t {
    None,
    StopVoiceForRecovery,
    StartVoice,
};

// The worker's source-voice state. The callbacks publish facts only; the worker reads the XAudio2 queue depth and
// makes each transition once. Stopping an empty voice does not flush it, so blocks queued while it is stopped wait
// until the prebuffer is full again.
class NativeAudioPlaybackLifecycle final {
public:
    [[nodiscard]] constexpr NativeAudioPlaybackAction observe_queue(
        std::uint32_t queued_buffers) noexcept {
        if (queued_buffers == 0u &&
            state_ == NativeAudioPlaybackState::Running) {
            state_ = NativeAudioPlaybackState::Recovering;
            return NativeAudioPlaybackAction::StopVoiceForRecovery;
        }
        return NativeAudioPlaybackAction::None;
    }

    [[nodiscard]] constexpr NativeAudioPlaybackAction start_action(
        std::uint32_t queued_buffers,
        std::uint32_t prebuffer_buffers) const noexcept {
        const std::uint32_t required_buffers =
            prebuffer_buffers == 0u ? 1u : prebuffer_buffers;
        if (state_ != NativeAudioPlaybackState::Running &&
            queued_buffers >= required_buffers) {
            return NativeAudioPlaybackAction::StartVoice;
        }
        return NativeAudioPlaybackAction::None;
    }

    constexpr void mark_started() noexcept {
        state_ = NativeAudioPlaybackState::Running;
    }

    constexpr void reset() noexcept {
        state_ = NativeAudioPlaybackState::Stopped;
    }

private:
    NativeAudioPlaybackState state_{NativeAudioPlaybackState::Stopped};
};

// XAudio2 calls OnBufferEnd after the buffer's last byte and before its successor's first, so a GetState there tells
// a continuous stream from an empty queue. Unobserved: the callback had no voice to ask.
enum class NativeAudioBufferEndQueueEvidence : std::uint8_t {
    Unobserved,
    SuccessorQueued,
    EmptyAtBoundary,
};

enum class NativeAudioEmptyBoundaryDisposition : std::uint8_t {
    None,
    RecoverUnderrun,
    FatalCounterDiscontinuity,
};

// The worker's cursor over the empty boundaries the callbacks published. Each one is an underrun; more than one
// unseen boundary at a time breaks the one-voice design's invariant and is fatal.
class NativeAudioEmptyBoundaryTracker final {
public:
    // Only after DestroyVoice has quiesced the callbacks: a flush can end several unstarted buffers, which are not
    // underruns of the next voice.
    constexpr void reset_after_voice_destruction(std::uint64_t published) noexcept {
        handled_empty_boundaries_ = published;
    }
    [[nodiscard]] constexpr NativeAudioEmptyBoundaryDisposition observe(
        std::uint64_t published_empty_boundaries) noexcept {
        if (published_empty_boundaries == handled_empty_boundaries_) {
            return NativeAudioEmptyBoundaryDisposition::None;
        }
        if (published_empty_boundaries < handled_empty_boundaries_ ||
            published_empty_boundaries - handled_empty_boundaries_ != 1u) {
            handled_empty_boundaries_ = published_empty_boundaries;
            return NativeAudioEmptyBoundaryDisposition::
                FatalCounterDiscontinuity;
        }
        handled_empty_boundaries_ = published_empty_boundaries;
        return NativeAudioEmptyBoundaryDisposition::RecoverUnderrun;
    }

private:
    std::uint64_t handled_empty_boundaries_{};
};

// What XAudio2's callbacks (on its engine thread) publish for the audio worker, which alone touches the source voice
// and its queue. Every record advances event_sequence, which wakes the worker's next look at the queue.
class NativeAudioCallbackLedger final {
public:
    void record_buffer_end(
        NativeAudioBufferEndQueueEvidence queue_evidence =
            NativeAudioBufferEndQueueEvidence::Unobserved) noexcept {
        if (queue_evidence == NativeAudioBufferEndQueueEvidence::EmptyAtBoundary) {
            empty_buffer_end_boundaries_.fetch_add(
                1, std::memory_order_release);
        }
        event_sequence_.fetch_add(1, std::memory_order_release);
    }

    void record_voice_error(std::int32_t error) noexcept {
        last_voice_error_.store(error, std::memory_order_relaxed);
        voice_errors_.fetch_add(1, std::memory_order_release);
        event_sequence_.fetch_add(1, std::memory_order_release);
    }

    void record_engine_critical_error(std::int32_t error) noexcept {
        last_engine_critical_error_.store(error, std::memory_order_relaxed);
        engine_critical_errors_.fetch_add(1, std::memory_order_release);
        event_sequence_.fetch_add(1, std::memory_order_release);
    }

    void record_callback_wake_error(std::int32_t error) noexcept {
        last_callback_wake_error_.store(error, std::memory_order_relaxed);
        callback_wake_errors_.fetch_add(1, std::memory_order_release);
        event_sequence_.fetch_add(1, std::memory_order_release);
    }

    void record_frequency_ratio_apply(bool succeeded, std::int32_t error) noexcept {
        if (!succeeded) {
            last_frequency_ratio_apply_error_.store(
                error, std::memory_order_relaxed);
            frequency_ratio_apply_errors_.fetch_add(
                1, std::memory_order_release);
        }
        event_sequence_.fetch_add(1, std::memory_order_release);
    }

    [[nodiscard]] std::uint64_t voice_errors() const noexcept {
        return voice_errors_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::uint64_t engine_critical_errors() const noexcept {
        return engine_critical_errors_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::uint64_t callback_wake_errors() const noexcept {
        return callback_wake_errors_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::uint64_t frequency_ratio_apply_errors()
        const noexcept {
        return frequency_ratio_apply_errors_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::int32_t last_voice_error() const noexcept {
        return last_voice_error_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::int32_t last_engine_critical_error() const noexcept {
        return last_engine_critical_error_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::int32_t last_callback_wake_error() const noexcept {
        return last_callback_wake_error_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::int32_t last_frequency_ratio_apply_error()
        const noexcept {
        return last_frequency_ratio_apply_error_.load(
            std::memory_order_acquire);
    }

    [[nodiscard]] std::uint64_t empty_buffer_end_boundaries() const noexcept {
        return empty_buffer_end_boundaries_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::uint64_t event_sequence() const noexcept {
        return event_sequence_.load(std::memory_order_acquire);
    }

private:
    std::atomic_uint64_t voice_errors_{0};
    std::atomic_uint64_t engine_critical_errors_{0};
    std::atomic_uint64_t callback_wake_errors_{0};
    std::atomic_uint64_t frequency_ratio_apply_errors_{0};
    std::atomic<std::int32_t> last_voice_error_{0};
    std::atomic<std::int32_t> last_engine_critical_error_{0};
    std::atomic<std::int32_t> last_callback_wake_error_{0};
    std::atomic<std::int32_t> last_frequency_ratio_apply_error_{0};
    std::atomic_uint64_t empty_buffer_end_boundaries_{0};
    std::atomic_uint64_t event_sequence_{0};
};

// The XAudio2 queue depth the worker last read, for other threads (they never call the source voice).
class NativeAudioQueueMirror final {
public:
    void publish_from_worker(std::uint32_t queued_buffers) noexcept {
        queued_buffers_.store(queued_buffers, std::memory_order_release);
    }

    [[nodiscard]] std::uint32_t queued_buffers() const noexcept {
        return queued_buffers_.load(std::memory_order_acquire);
    }

private:
    std::atomic<std::uint32_t> queued_buffers_{0};
};

}  // namespace galaxy::host
