#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <mutex>
#include <span>
#include <stdexcept>
#include <thread>
#include <utility>

namespace galaxy::host {

// One control owner. Backend fields belong exclusively to the job until the
// owner reaps completion. active() deliberately stays true after the job ends
// so readers cannot race its final writes before join establishes ownership.
class NativeAudioRecoveryTask {
public:
    NativeAudioRecoveryTask() = default;
    ~NativeAudioRecoveryTask() { if (thread_.joinable()) thread_.join(); }
    NativeAudioRecoveryTask(const NativeAudioRecoveryTask&) = delete;
    NativeAudioRecoveryTask& operator=(const NativeAudioRecoveryTask&) = delete;
    [[nodiscard]] bool active() const noexcept { return thread_.joinable(); }
    template<class Job> void start(Job&& job) {
        if (active()) throw std::logic_error("audio recovery already owns the backend");
        done_.store(false, std::memory_order_relaxed);
        error_ = nullptr;
        thread_ = std::thread([this, job = std::forward<Job>(job)]() mutable {
            try { job(); } catch (...) { error_ = std::current_exception(); }
            done_.store(true, std::memory_order_release);
        });
    }
    [[nodiscard]] bool finish_ready() {
        if (!active()) return true;
        if (!done_.load(std::memory_order_acquire)) return false;
        finish();
        return true;
    }
    // Explicit shutdown may block. Routine health polling must use finish_ready.
    void finish() {
        if (thread_.joinable()) thread_.join();
        if (error_) std::rethrow_exception(std::exchange(error_, nullptr));
    }
private:
    std::thread thread_;
    std::atomic_bool done_{false};
    std::exception_ptr error_;
};

// HRESULT values are kept platform-independent for policy tests. Unexpected
// engine/programming errors remain fatal, as does any failure in proof mode.
[[nodiscard]] constexpr bool native_audio_endpoint_error_recoverable(
    std::uint32_t error, bool strict_proof) noexcept {
    return !strict_proof && (error == 0x80070490u || // ERROR_NOT_FOUND
        error == 0x88960004u || // XAUDIO2_E_DEVICE_INVALIDATED
        error == 0x88890004u);   // AUDCLNT_E_DEVICE_INVALIDATED
}

[[nodiscard]] constexpr bool native_audio_endpoint_retry_due(
    std::uint64_t now_ns, std::uint64_t last_attempt_ns) noexcept {
    return now_ns >= last_attempt_ns &&
        now_ns - last_attempt_ns >= 1'000'000'000ull;
}

struct NativeAudioCallbackBufferObservation {
    bool completed{};
    bool started{};
};

// Completion must be acquired before start. Normal OnBufferEnd follows the
// release publication in OnBufferStart; sampling in the reverse order can
// combine an old false start with a newly true completion. Flush is separately
// authorized by the sink only after DestroyVoice has quiesced callbacks.
template<class ReadCompleted, class ReadStarted>
[[nodiscard]] inline NativeAudioCallbackBufferObservation
observe_callback_buffer_state(ReadCompleted&& read_completed,
                              ReadStarted&& read_started) {
    const bool completed = read_completed();
    const bool started = read_started();
    return {completed, started};
}

// Wii AI DMA stores each stereo frame as big-endian right, left. The native
// PCM output and WAV writer require little-endian left, right. Adapted from
// WiiCompiled runtime/src/audio_backend.cpp::PushWiiAiSamplesBE16,
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

enum class NativeAudioPreinitializeMode : std::uint8_t {
    NativeOutput,
    SkipDisabled,
    SkipWavDumpOnly,
};

[[nodiscard]] constexpr NativeAudioPreinitializeMode
native_audio_preinitialize_mode(
    bool audio_disabled,
    bool wav_dump_only) noexcept {
    // Dump-only submission is checked before AUDIO_DISABLE in the sink, so it
    // has the same precedence here. Neither explicit diagnostic mode should
    // initialize a Windows render endpoint.
    if (wav_dump_only) {
        return NativeAudioPreinitializeMode::SkipWavDumpOnly;
    }
    if (audio_disabled) {
        return NativeAudioPreinitializeMode::SkipDisabled;
    }
    return NativeAudioPreinitializeMode::NativeOutput;
}

enum class NativeAudioInitializationState : std::uint8_t {
    Uninitialized,
    Initializing,
    Ready,
    Failed,
};

struct NativeAudioInitializationSnapshot {
    NativeAudioInitializationState state{
        NativeAudioInitializationState::Uninitialized};
    std::uint64_t initialization_attempts{};
    std::uint64_t preinitialize_calls{};
    std::uint64_t submit_checks{};
    std::uint64_t submit_initialization_attempts{};
    bool preinitialized{};
};

// Single-owner initialization gate. NativeAudioSink is initialized and fed by
// the simulation thread; the separate audio worker owns only source-voice
// queue operations. Keeping the policy here makes the critical invariant
// testable without requiring a Windows audio endpoint in CI.
class NativeAudioInitializationGate final {
public:
    template <typename Initializer>
    bool preinitialize(Initializer&& initializer) {
        ++preinitialize_calls_;
        return ensure_ready(
            false, std::forward<Initializer>(initializer));
    }

    template <typename Initializer>
    bool ensure_ready_for_submit(Initializer&& initializer) {
        ++submit_checks_;
        if (state_ == NativeAudioInitializationState::Ready) {
            return true;
        }
        if (state_ == NativeAudioInitializationState::Failed) {
            return false;
        }
        ++submit_initialization_attempts_;
        return ensure_ready(
            true, std::forward<Initializer>(initializer));
    }

    [[nodiscard]] bool ready() const noexcept {
        return state_ == NativeAudioInitializationState::Ready;
    }

    [[nodiscard]] NativeAudioInitializationSnapshot snapshot() const noexcept {
        return {
            state_,
            initialization_attempts_,
            preinitialize_calls_,
            submit_checks_,
            submit_initialization_attempts_,
            preinitialized_,
        };
    }

private:
    template <typename Initializer>
    bool ensure_ready(bool from_submit, Initializer&& initializer) {
        if (state_ == NativeAudioInitializationState::Ready) {
            return true;
        }
        if (state_ == NativeAudioInitializationState::Failed ||
            state_ == NativeAudioInitializationState::Initializing) {
            return false;
        }

        state_ = NativeAudioInitializationState::Initializing;
        ++initialization_attempts_;
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
        if (initialized && !from_submit) {
            preinitialized_ = true;
        }
        return initialized;
    }

    NativeAudioInitializationState state_{
        NativeAudioInitializationState::Uninitialized};
    std::uint64_t initialization_attempts_{};
    std::uint64_t preinitialize_calls_{};
    std::uint64_t submit_checks_{};
    std::uint64_t submit_initialization_attempts_{};
    bool preinitialized_{};
};

inline constexpr std::uint32_t kNativeAudioVoiceBaseSampleRate = 32'000u;
inline constexpr float kNativeAudioMaxFrequencyRatio = 1.5f;

struct NativeAudioFrequencyRatioDecision {
    float ratio{};
    bool valid{};
};

// The source voice has one immutable 32 kHz PCM format for its entire
// lifetime. XAudio2 consumes the same PCM sample sequence at 32 or 48 kHz by
// applying the corresponding frequency ratio at the exact OnBufferStart
// boundary. Both supported ratios are exactly representable as binary floats.
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

// Zero means no ratio has yet been established on this source-voice lifetime.
// The first buffer always performs an explicit SetFrequencyRatio, even when it
// requests XAudio2's nominal 1.0 default. Later same-rate buffers inherit the
// last successful voice state; only a real 32/48 kHz transition calls XAudio.
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

// Worker-owned source-voice lifecycle. Queue callbacks publish facts only; the
// worker observes the authoritative XAudio queue depth and performs each voice
// transition exactly once. Stopping an empty voice does not flush it, so PCM
// accepted during recovery remains queued until the configured prebuffer has
// been rebuilt.
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

    [[nodiscard]] constexpr bool started() const noexcept {
        return state_ == NativeAudioPlaybackState::Running;
    }

    [[nodiscard]] constexpr bool recovering() const noexcept {
        return state_ == NativeAudioPlaybackState::Recovering;
    }

    [[nodiscard]] constexpr NativeAudioPlaybackState state() const noexcept {
        return state_;
    }

private:
    NativeAudioPlaybackState state_{NativeAudioPlaybackState::Stopped};
};

// XAudio invokes OnBufferEnd after the last byte of the completed buffer and
// before the first byte of its successor.  A GetState observation made in that
// callback therefore distinguishes a continuously queued stream from a real
// empty boundary without relying on a later worker-thread snapshot.  Keep an
// explicit Unobserved value so an unwired callback cannot be mistaken for
// proof that a successor was present.
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

// Worker-owned cursor over callback-published empty boundaries. The one-voice
// design never drains for a rate change, so every observed empty boundary is a
// real underrun. Coalescing more than one previously unseen boundary is an
// invariant failure rather than evidence that may be silently discarded.
class NativeAudioEmptyBoundaryTracker final {
public:
    // Only after DestroyVoice has quiesced callbacks. Flush may produce
    // several unstarted ends; they are not underruns of the next voice.
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

    [[nodiscard]] constexpr std::uint64_t handled_empty_boundaries()
        const noexcept {
        return handled_empty_boundaries_;
    }

private:
    std::uint64_t handled_empty_boundaries_{};
};

struct NativeAudioRateLedgerSnapshot {
    std::uint64_t submitted_buffers_32000{};
    std::uint64_t submitted_stereo_frames_32000{};
    std::uint64_t submitted_buffers_48000{};
    std::uint64_t submitted_stereo_frames_48000{};
    std::uint64_t sample_rate_change_events{};
};

// Generation-local accounting for buffers that reached the native output
// boundary.  Separate frame totals are required because one aggregate average
// cannot prove elapsed audio duration across a 32/48 kHz mixed-rate window.
class NativeAudioRateLedger final {
public:
    [[nodiscard]] bool record_submission(
        std::uint32_t sample_rate,
        std::uint64_t pcm_bytes) noexcept {
        if (pcm_bytes == 0u || (pcm_bytes & 3u) != 0u) {
            return false;
        }
        const std::uint64_t stereo_frames = pcm_bytes / 4u;
        std::uint64_t* buffer_count = nullptr;
        std::uint64_t* frame_count = nullptr;
        switch (sample_rate) {
        case 32'000u:
            buffer_count = &snapshot_.submitted_buffers_32000;
            frame_count = &snapshot_.submitted_stereo_frames_32000;
            break;
        case 48'000u:
            buffer_count = &snapshot_.submitted_buffers_48000;
            frame_count = &snapshot_.submitted_stereo_frames_48000;
            break;
        default:
            return false;
        }

        const bool rate_changed =
            last_sample_rate_ != 0u && last_sample_rate_ != sample_rate;
        if (*buffer_count == std::numeric_limits<std::uint64_t>::max() ||
            stereo_frames >
                std::numeric_limits<std::uint64_t>::max() - *frame_count ||
            (rate_changed &&
             snapshot_.sample_rate_change_events ==
                 std::numeric_limits<std::uint64_t>::max())) {
            return false;
        }

        ++*buffer_count;
        *frame_count += stereo_frames;
        if (rate_changed) {
            ++snapshot_.sample_rate_change_events;
        }
        last_sample_rate_ = sample_rate;
        return true;
    }

    void reset() noexcept {
        snapshot_ = {};
        last_sample_rate_ = 0u;
    }

    [[nodiscard]] NativeAudioRateLedgerSnapshot snapshot() const noexcept {
        return snapshot_;
    }

private:
    NativeAudioRateLedgerSnapshot snapshot_{};
    std::uint32_t last_sample_rate_{};
};

struct NativeAudioSubmissionWindowSnapshot {
    std::uint64_t submitted_buffers{};
    NativeAudioRateLedgerSnapshot rate_ledger{};
    std::uint64_t submit_delta_events{};
    std::uint64_t total_submit_delta_us{};
    std::uint64_t max_submit_delta_us{};
    std::uint64_t submit_delta_over_expected{};
    std::uint64_t submit_delta_over_25_ms{};
};

// One lock protects the generation-local XAudio submission and cadence facts.
// A diagnostic snapshot therefore cannot observe buffer N before the cadence
// interval which brought N into the same native queue. The caller still owns
// generation selection; this ledger owns the all-or-nothing counter update.
class NativeAudioSubmissionWindowLedger final {
public:
    [[nodiscard]] bool record_submission(
        std::uint32_t sample_rate,
        std::uint64_t pcm_bytes,
        bool has_window_predecessor,
        std::uint64_t delta_us,
        std::uint64_t expected_us) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        const bool expected_predecessor = submitted_buffers_ != 0u;
        if (has_window_predecessor != expected_predecessor ||
            submitted_buffers_ == std::numeric_limits<std::uint64_t>::max() ||
            (has_window_predecessor &&
             (submit_delta_events_ ==
                  std::numeric_limits<std::uint64_t>::max() ||
              delta_us >
                  std::numeric_limits<std::uint64_t>::max() -
                      total_submit_delta_us_ ||
              (expected_us != 0u && delta_us > expected_us &&
               submit_delta_over_expected_ ==
                   std::numeric_limits<std::uint64_t>::max()) ||
              (delta_us > 25'000u &&
               submit_delta_over_25_ms_ ==
                   std::numeric_limits<std::uint64_t>::max())))) {
            return false;
        }
        if (!rate_ledger_.record_submission(sample_rate, pcm_bytes)) {
            return false;
        }

        ++submitted_buffers_;
        if (has_window_predecessor) {
            ++submit_delta_events_;
            total_submit_delta_us_ += delta_us;
            max_submit_delta_us_ = std::max(max_submit_delta_us_, delta_us);
            if (expected_us != 0u && delta_us > expected_us) {
                ++submit_delta_over_expected_;
            }
            if (delta_us > 25'000u) {
                ++submit_delta_over_25_ms_;
            }
        }
        return true;
    }

    void reset() noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        submitted_buffers_ = 0u;
        rate_ledger_.reset();
        submit_delta_events_ = 0u;
        total_submit_delta_us_ = 0u;
        max_submit_delta_us_ = 0u;
        submit_delta_over_expected_ = 0u;
        submit_delta_over_25_ms_ = 0u;
    }

    [[nodiscard]] NativeAudioSubmissionWindowSnapshot snapshot()
        const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return {
            submitted_buffers_,
            rate_ledger_.snapshot(),
            submit_delta_events_,
            total_submit_delta_us_,
            max_submit_delta_us_,
            submit_delta_over_expected_,
            submit_delta_over_25_ms_,
        };
    }

private:
    mutable std::mutex mutex_;
    std::uint64_t submitted_buffers_{};
    NativeAudioRateLedger rate_ledger_;
    std::uint64_t submit_delta_events_{};
    std::uint64_t total_submit_delta_us_{};
    std::uint64_t max_submit_delta_us_{};
    std::uint64_t submit_delta_over_expected_{};
    std::uint64_t submit_delta_over_25_ms_{};
};

struct NativeAudioStartedWindowSnapshot {
    std::uint64_t started_buffers{};
    NativeAudioRateLedgerSnapshot rate_ledger{};
    std::uint64_t frequency_ratio_apply_calls{};
    std::uint64_t frequency_ratio_inherited_buffers{};
    std::uint64_t frequency_ratio_change_actions{};
    std::uint64_t frequency_ratio_apply_errors{};
    std::uint64_t buffer_start_callback_core_work_count{};
    std::uint64_t buffer_start_callback_core_work_total_ns{};
    std::uint64_t buffer_start_callback_core_work_max_ns{};
    std::uint64_t buffer_start_callback_core_work_over_1_ms{};
    std::uint64_t buffer_start_callback_core_work_over_2_ms{};
};

// The audio worker consumes callback evidence only after acquiring the
// SubmittedBuffer::started release store. This ledger makes each started PCM
// buffer, its exact active 1.0/1.5 ratio, and its callback core-work duration
// one proof transaction. The first generation-local buffer may establish a new
// voice ratio or inherit the state established before the measurement window.
// Every later buffer must apply exactly at a 32/48 kHz transition and inherit
// everywhere else.
class NativeAudioStartedWindowLedger final {
public:
    [[nodiscard]] bool record_start(
        std::uint32_t sample_rate,
        std::uint64_t pcm_bytes,
        bool ratio_apply_required,
        bool ratio_apply_invoked,
        float requested_ratio,
        float active_ratio,
        bool ratio_apply_succeeded,
        bool ratio_inherited,
        bool callback_reported_change,
        std::uint64_t callback_core_work_ns) noexcept {
        const NativeAudioFrequencyRatioDecision expected_ratio =
            native_audio_frequency_ratio_for_sample_rate(sample_rate);
        if (!expected_ratio.valid ||
            started_buffers_ == std::numeric_limits<std::uint64_t>::max() ||
            buffer_start_callback_core_work_count_ ==
                std::numeric_limits<std::uint64_t>::max() ||
            callback_core_work_ns >
                std::numeric_limits<std::uint64_t>::max() -
                    buffer_start_callback_core_work_total_ns_) {
            return false;
        }

        const bool first_window_buffer = last_sample_rate_ == 0u;
        const bool expected_change =
            !first_window_buffer && last_sample_rate_ != sample_rate;
        const bool boundary_evidence_matches =
            first_window_buffer || ratio_apply_required == expected_change;
        const bool ratio_evidence_valid =
            requested_ratio == expected_ratio.ratio &&
            active_ratio == expected_ratio.ratio &&
            boundary_evidence_matches &&
            callback_reported_change == ratio_apply_required &&
            (ratio_apply_required
                 ? ratio_apply_invoked && ratio_apply_succeeded &&
                     !ratio_inherited
                 : !ratio_apply_invoked && !ratio_apply_succeeded &&
                     ratio_inherited);
        if ((ratio_apply_invoked &&
             frequency_ratio_apply_calls_ ==
                 std::numeric_limits<std::uint64_t>::max()) ||
            (ratio_inherited &&
             frequency_ratio_inherited_buffers_ ==
                 std::numeric_limits<std::uint64_t>::max()) ||
            (!ratio_evidence_valid &&
             frequency_ratio_apply_errors_ ==
                 std::numeric_limits<std::uint64_t>::max()) ||
            (callback_core_work_ns > 1'000'000u &&
             buffer_start_callback_core_work_over_1_ms_ ==
                 std::numeric_limits<std::uint64_t>::max()) ||
            (callback_core_work_ns > 2'000'000u &&
             buffer_start_callback_core_work_over_2_ms_ ==
                 std::numeric_limits<std::uint64_t>::max())) {
            return false;
        }
        if (expected_change && ratio_evidence_valid &&
            frequency_ratio_change_actions_ ==
                std::numeric_limits<std::uint64_t>::max()) {
            return false;
        }
        if (!rate_ledger_.record_submission(sample_rate, pcm_bytes)) {
            return false;
        }

        ++started_buffers_;
        if (ratio_apply_invoked) {
            ++frequency_ratio_apply_calls_;
        }
        if (ratio_inherited) {
            ++frequency_ratio_inherited_buffers_;
        }
        if (expected_change && ratio_evidence_valid) {
            ++frequency_ratio_change_actions_;
        }
        if (!ratio_evidence_valid) {
            ++frequency_ratio_apply_errors_;
        }
        ++buffer_start_callback_core_work_count_;
        buffer_start_callback_core_work_total_ns_ += callback_core_work_ns;
        buffer_start_callback_core_work_max_ns_ = std::max(
            buffer_start_callback_core_work_max_ns_, callback_core_work_ns);
        if (callback_core_work_ns > 1'000'000u) {
            ++buffer_start_callback_core_work_over_1_ms_;
        }
        if (callback_core_work_ns > 2'000'000u) {
            ++buffer_start_callback_core_work_over_2_ms_;
        }
        last_sample_rate_ = sample_rate;
        return ratio_evidence_valid;
    }

    void reset() noexcept {
        started_buffers_ = 0u;
        rate_ledger_.reset();
        frequency_ratio_apply_calls_ = 0u;
        frequency_ratio_inherited_buffers_ = 0u;
        frequency_ratio_change_actions_ = 0u;
        frequency_ratio_apply_errors_ = 0u;
        buffer_start_callback_core_work_count_ = 0u;
        buffer_start_callback_core_work_total_ns_ = 0u;
        buffer_start_callback_core_work_max_ns_ = 0u;
        buffer_start_callback_core_work_over_1_ms_ = 0u;
        buffer_start_callback_core_work_over_2_ms_ = 0u;
        last_sample_rate_ = 0u;
    }

    [[nodiscard]] NativeAudioStartedWindowSnapshot snapshot()
        const noexcept {
        return {
            started_buffers_,
            rate_ledger_.snapshot(),
            frequency_ratio_apply_calls_,
            frequency_ratio_inherited_buffers_,
            frequency_ratio_change_actions_,
            frequency_ratio_apply_errors_,
            buffer_start_callback_core_work_count_,
            buffer_start_callback_core_work_total_ns_,
            buffer_start_callback_core_work_max_ns_,
            buffer_start_callback_core_work_over_1_ms_,
            buffer_start_callback_core_work_over_2_ms_,
        };
    }

private:
    std::uint64_t started_buffers_{};
    NativeAudioRateLedger rate_ledger_;
    std::uint64_t frequency_ratio_apply_calls_{};
    std::uint64_t frequency_ratio_inherited_buffers_{};
    std::uint64_t frequency_ratio_change_actions_{};
    std::uint64_t frequency_ratio_apply_errors_{};
    std::uint64_t buffer_start_callback_core_work_count_{};
    std::uint64_t buffer_start_callback_core_work_total_ns_{};
    std::uint64_t buffer_start_callback_core_work_max_ns_{};
    std::uint64_t buffer_start_callback_core_work_over_1_ms_{};
    std::uint64_t buffer_start_callback_core_work_over_2_ms_{};
    std::uint32_t last_sample_rate_{};
};

// The first fatal audio reason publishes once.  Registration is synchronized
// with publication, and a notifier installed after publication is immediately
// replayed so an early asynchronous failure cannot wait for another guest DMA.
class NativeAudioFailureNotifier final {
public:
    using Callback = void (*)(void* user) noexcept;

    NativeAudioFailureNotifier() = default;
    NativeAudioFailureNotifier(const NativeAudioFailureNotifier&) = delete;
    NativeAudioFailureNotifier& operator=(
        const NativeAudioFailureNotifier&) = delete;

    void set(Callback callback, void* user) noexcept {
        Callback callback_to_invoke = nullptr;
        void* callback_user = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            callback_ = callback;
            callback_user_ = user;
            current_registration_notified_ = false;
            if (published_ && callback_ != nullptr) {
                current_registration_notified_ = true;
                callback_to_invoke = callback_;
                callback_user = callback_user_;
            }
        }
        if (callback_to_invoke != nullptr) {
            callback_to_invoke(callback_user);
        }
    }

    void publish() noexcept {
        Callback callback_to_invoke = nullptr;
        void* callback_user = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            published_ = true;
            if (callback_ != nullptr && !current_registration_notified_) {
                current_registration_notified_ = true;
                callback_to_invoke = callback_;
                callback_user = callback_user_;
            }
        }
        if (callback_to_invoke != nullptr) {
            callback_to_invoke(callback_user);
        }
    }

    [[nodiscard]] bool published() const noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        return published_;
    }

private:
    mutable std::mutex mutex_;
    Callback callback_{};
    void* callback_user_{};
    bool published_{};
    bool current_registration_notified_{};
};

[[nodiscard]] constexpr bool native_audio_submit_delta_in_window(
    std::uint64_t previous_buffer_generation,
    std::uint64_t current_buffer_generation,
    std::uint64_t active_generation) noexcept {
    return active_generation != 0u &&
           previous_buffer_generation == active_generation &&
           current_buffer_generation == active_generation;
}

[[nodiscard]] constexpr std::uint64_t native_audio_pcm_duration_ms_ceil(
    std::uint64_t pcm_bytes,
    std::uint32_t sample_rate) noexcept {
    if (sample_rate == 0u) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    const std::uint64_t frames = pcm_bytes / 4u;
    constexpr std::uint64_t kMillisecondsPerSecond = 1000u;
    if (frames >
        (std::numeric_limits<std::uint64_t>::max() - sample_rate + 1u) /
            kMillisecondsPerSecond) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return (frames * kMillisecondsPerSecond + sample_rate - 1u) /
        sample_rate;
}

// XAudio invokes voice callbacks on its own engine thread.  Keep that thread
// out of queue ownership: callbacks publish facts only, while the audio worker
// remains the sole authority for IXAudio2SourceVoice state and queue depth.
struct NativeAudioFrequencyRatioLedgerSnapshot {
    std::uint64_t apply_calls{};
    std::uint64_t change_actions{};
    std::uint64_t apply_errors{};
    bool coherent = true;
};

class NativeAudioCallbackLedger final {
public:
    void record_buffer_end(
        NativeAudioBufferEndQueueEvidence queue_evidence =
            NativeAudioBufferEndQueueEvidence::Unobserved) noexcept {
        completed_buffers_.fetch_add(1, std::memory_order_release);
        switch (queue_evidence) {
        case NativeAudioBufferEndQueueEvidence::Unobserved:
            unobserved_buffer_end_boundaries_.fetch_add(
                1, std::memory_order_release);
            break;
        case NativeAudioBufferEndQueueEvidence::SuccessorQueued:
            nonempty_buffer_end_boundaries_.fetch_add(
                1, std::memory_order_release);
            break;
        case NativeAudioBufferEndQueueEvidence::EmptyAtBoundary:
            empty_buffer_end_boundaries_.fetch_add(
                1, std::memory_order_release);
            break;
        }
        event_sequence_.fetch_add(1, std::memory_order_release);
    }

    void record_voice_error(std::int32_t error) noexcept {
        last_voice_error_.store(error, std::memory_order_relaxed);
        voice_errors_.fetch_add(1, std::memory_order_release);
        failure_sequence_.fetch_add(1, std::memory_order_release);
        event_sequence_.fetch_add(1, std::memory_order_release);
    }

    void record_engine_critical_error(std::int32_t error) noexcept {
        last_engine_critical_error_.store(error, std::memory_order_relaxed);
        engine_critical_errors_.fetch_add(1, std::memory_order_release);
        failure_sequence_.fetch_add(1, std::memory_order_release);
        event_sequence_.fetch_add(1, std::memory_order_release);
    }

    void record_callback_wake_error(std::int32_t error) noexcept {
        last_callback_wake_error_.store(error, std::memory_order_relaxed);
        callback_wake_errors_.fetch_add(1, std::memory_order_release);
        failure_sequence_.fetch_add(1, std::memory_order_release);
        event_sequence_.fetch_add(1, std::memory_order_release);
    }

    void record_frequency_ratio_apply(
        bool ratio_changed,
        bool succeeded,
        std::int32_t error) noexcept {
        // A NativeAudioSink owns exactly one source voice, whose callbacks are
        // serialized. The odd/even sequence lets non-callback threads take one
        // coherent three-counter baseline without a callback-thread lock.
        frequency_ratio_snapshot_sequence_.fetch_add(
            1, std::memory_order_acq_rel);
        frequency_ratio_apply_calls_.fetch_add(1, std::memory_order_release);
        if (succeeded && ratio_changed) {
            frequency_ratio_change_actions_.fetch_add(
                1, std::memory_order_release);
        }
        if (!succeeded) {
            last_frequency_ratio_apply_error_.store(
                error, std::memory_order_relaxed);
            frequency_ratio_apply_errors_.fetch_add(
                1, std::memory_order_release);
            failure_sequence_.fetch_add(1, std::memory_order_release);
        }
        frequency_ratio_snapshot_sequence_.fetch_add(
            1, std::memory_order_release);
        event_sequence_.fetch_add(1, std::memory_order_release);
    }

    [[nodiscard]] NativeAudioFrequencyRatioLedgerSnapshot
    frequency_ratio_snapshot() const noexcept {
        NativeAudioFrequencyRatioLedgerSnapshot snapshot{};
        // Bounded diagnostic read: never wait for a descheduled callback.
        for (unsigned attempt = 0; attempt < 16u; ++attempt) {
            const std::uint64_t before =
                frequency_ratio_snapshot_sequence_.load(
                    std::memory_order_acquire);
            if ((before & 1u) != 0u) {
                continue;
            }
            snapshot.apply_calls =
                frequency_ratio_apply_calls_.load(std::memory_order_acquire);
            snapshot.change_actions =
                frequency_ratio_change_actions_.load(
                    std::memory_order_acquire);
            snapshot.apply_errors =
                frequency_ratio_apply_errors_.load(
                    std::memory_order_acquire);
            const std::uint64_t after =
                frequency_ratio_snapshot_sequence_.load(
                    std::memory_order_acquire);
            if (before == after) {
                return snapshot;
            }
        }
        snapshot.apply_calls = frequency_ratio_apply_calls_.load(std::memory_order_acquire);
        snapshot.change_actions = frequency_ratio_change_actions_.load(std::memory_order_acquire);
        snapshot.apply_errors = frequency_ratio_apply_errors_.load(std::memory_order_acquire);
        snapshot.coherent = false;
        return snapshot;
    }

    [[nodiscard]] std::uint64_t completed_buffers() const noexcept {
        return completed_buffers_.load(std::memory_order_acquire);
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

    [[nodiscard]] std::uint64_t frequency_ratio_apply_calls() const noexcept {
        return frequency_ratio_apply_calls_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::uint64_t frequency_ratio_change_actions()
        const noexcept {
        return frequency_ratio_change_actions_.load(std::memory_order_acquire);
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

    [[nodiscard]] std::uint64_t nonempty_buffer_end_boundaries() const noexcept {
        return nonempty_buffer_end_boundaries_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::uint64_t
    unobserved_buffer_end_boundaries() const noexcept {
        return unobserved_buffer_end_boundaries_.load(
            std::memory_order_acquire);
    }

    [[nodiscard]] std::uint64_t failure_sequence() const noexcept {
        return failure_sequence_.load(std::memory_order_acquire);
    }

    // This is intentionally sticky.  The simulation thread can poll it at a
    // scheduler boundary and hard-fail even if the guest never submits another
    // AI DMA buffer after an asynchronous XAudio error.
    [[nodiscard]] bool has_failure() const noexcept {
        return failure_sequence() != 0u;
    }

    [[nodiscard]] bool failure_since(
        std::uint64_t observed_failure_sequence) const noexcept {
        return failure_sequence() != observed_failure_sequence;
    }

    [[nodiscard]] std::uint64_t event_sequence() const noexcept {
        return event_sequence_.load(std::memory_order_acquire);
    }

private:
    std::atomic_uint64_t completed_buffers_{0};
    std::atomic_uint64_t voice_errors_{0};
    std::atomic_uint64_t engine_critical_errors_{0};
    std::atomic_uint64_t callback_wake_errors_{0};
    std::atomic_uint64_t frequency_ratio_snapshot_sequence_{0};
    std::atomic_uint64_t frequency_ratio_apply_calls_{0};
    std::atomic_uint64_t frequency_ratio_change_actions_{0};
    std::atomic_uint64_t frequency_ratio_apply_errors_{0};
    std::atomic<std::int32_t> last_voice_error_{0};
    std::atomic<std::int32_t> last_engine_critical_error_{0};
    std::atomic<std::int32_t> last_callback_wake_error_{0};
    std::atomic<std::int32_t> last_frequency_ratio_apply_error_{0};
    std::atomic_uint64_t empty_buffer_end_boundaries_{0};
    std::atomic_uint64_t nonempty_buffer_end_boundaries_{0};
    std::atomic_uint64_t unobserved_buffer_end_boundaries_{0};
    std::atomic_uint64_t failure_sequence_{0};
    std::atomic_uint64_t event_sequence_{0};
};

// A read-only cross-thread mirror of the most recent GetState result.  Only the
// audio worker publishes values; diagnostics may read them without calling the
// source voice from a second thread.
class NativeAudioQueueMirror final {
public:
    void publish_from_worker(std::uint32_t queued_buffers) noexcept {
        queued_buffers_.store(queued_buffers, std::memory_order_release);
        std::uint32_t current_max =
            max_queued_buffers_.load(std::memory_order_acquire);
        while (queued_buffers > current_max &&
               !max_queued_buffers_.compare_exchange_weak(
                   current_max,
                   queued_buffers,
                   std::memory_order_acq_rel,
                   std::memory_order_acquire)) {
        }
    }

    [[nodiscard]] std::uint32_t queued_buffers() const noexcept {
        return queued_buffers_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::uint32_t max_queued_buffers() const noexcept {
        return max_queued_buffers_.load(std::memory_order_acquire);
    }

private:
    std::atomic<std::uint32_t> queued_buffers_{0};
    std::atomic<std::uint32_t> max_queued_buffers_{0};
};

}  // namespace galaxy::host
