#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace galaxy::audio {

struct Pcm16BeStereoAnalysis {
    std::uint32_t peak{};
    std::uint32_t mean_abs{};
    std::uint64_t nonzero_samples{};
    std::uint64_t sample_count{};
    std::uint16_t first_left{};
    std::uint16_t first_right{};
    // This is only an index into collision-resolved identity buckets. Exact
    // content decisions below always compare the complete byte sequence.
    std::uint64_t content_hash{};
};

[[nodiscard]] Pcm16BeStereoAnalysis analyze_pcm16_be_stereo(
    std::span<const std::byte> source);

// Full PCM analysis calculates a content hash and aggregate amplitudes for
// diagnostic identity proof. The native play path only needs that work until
// the first audible AI buffer: afterwards every buffer must be submitted
// regardless of its content. Keep tracing and an active measurement window
// exact, while avoiding that diagnostic scan during ordinary audible play.
[[nodiscard]] constexpr bool should_analyze_ai_pcm(
    bool identity_tracking_enabled,
    bool trace_pcm,
    bool audible_pcm_seen) noexcept {
    return identity_tracking_enabled || trace_pcm || !audible_pcm_seen;
}

struct AiPcmIdentityStats {
    std::uint64_t buffers{};
    std::uint64_t stereo_frames{};
    std::uint64_t buffers_32000{};
    std::uint64_t stereo_frames_32000{};
    std::uint64_t buffers_48000{};
    std::uint64_t stereo_frames_48000{};
    std::uint64_t sample_rate_change_events{};
    std::uint64_t total_duration_ns{};
    std::uint64_t min_buffer_duration_ns{};
    std::uint64_t max_buffer_duration_ns{};
    std::uint64_t audible_buffers{};
    std::uint64_t nonzero_samples{};
    std::uint32_t peak_window{};
    std::uint64_t content_change_events{};
    // The field name used by diagnostics is "distinct-content-hashes" for
    // compatibility, but this count is collision-safe: equal hashes are
    // resolved by byte-for-byte comparison including the buffer size.
    std::uint64_t distinct_content_identities{};
    std::uint64_t address_change_events{};
    std::uint64_t identical_audible_run_max{};
    std::uint64_t identical_audible_run_max_duration_ns{};
    std::uint64_t repeated_known_content_run_max{};
    std::uint64_t repeated_known_content_run_max_duration_ns{};
    std::uint64_t sink_submit_buffers{};
};

// Window-local exact PCM identity accounting. The tracker deliberately keeps
// copies of distinct buffers during a diagnostic window: this makes both the
// repeated-buffer and distinct-content claims immune to hash collisions.
class AiPcmIdentityTracker {
public:
    void reset();

    void observe(
        std::uint32_t guest_address,
        std::uint32_t sample_rate,
        std::span<const std::byte> source,
        const Pcm16BeStereoAnalysis& analysis);

    void record_sink_submit() noexcept {
        ++stats_.sink_submit_buffers;
    }

    [[nodiscard]] const AiPcmIdentityStats& stats() const noexcept {
        return stats_;
    }

private:
    [[nodiscard]] bool same_previous_content(
        std::span<const std::byte> source) const noexcept;
    [[nodiscard]] bool same_previous_audible_content(
        std::uint32_t sample_rate,
        std::span<const std::byte> source) const noexcept;
    [[nodiscard]] bool record_distinct_content(
        std::uint64_t hash,
        std::span<const std::byte> source);

    AiPcmIdentityStats stats_{};
    std::vector<std::byte> previous_content_;
    std::vector<std::byte> previous_audible_content_;
    std::vector<std::vector<std::byte>> distinct_contents_;
    std::unordered_map<std::uint64_t, std::vector<std::size_t>>
        distinct_hash_buckets_;
    std::uint32_t previous_address_{};
    std::uint32_t previous_sample_rate_{};
    std::uint32_t previous_audible_sample_rate_{};
    std::uint64_t current_identical_audible_run_{};
    std::uint64_t current_identical_audible_run_duration_ns_{};
    std::uint64_t current_repeated_known_content_run_{};
    std::uint64_t current_repeated_known_content_run_duration_ns_{};
    bool current_repeated_known_content_run_has_audible_{};
    bool have_previous_content_{};
    bool have_previous_sample_rate_{};
    bool have_previous_audible_content_{};
};

}  // namespace galaxy::audio
