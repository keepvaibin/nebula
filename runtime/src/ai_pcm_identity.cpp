#include "galaxy/ai_pcm_identity.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace galaxy::audio {
namespace {

constexpr std::uint64_t kFnv1aOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnv1aPrime = 1099511628211ull;

bool equal_bytes(
    const std::vector<std::byte>& left,
    std::span<const std::byte> right) noexcept {
    return left.size() == right.size() &&
           std::equal(left.begin(), left.end(), right.begin());
}

std::uint64_t buffer_duration_ns(
    std::span<const std::byte> source,
    std::uint32_t sample_rate) {
    if (sample_rate == 0u) {
        throw std::invalid_argument("AI PCM sample rate must be nonzero");
    }
    const std::uint64_t stereo_frames = source.size() / 4u;
    return (stereo_frames * 1'000'000'000ull + sample_rate - 1u) /
           sample_rate;
}

}  // namespace

Pcm16BeStereoAnalysis analyze_pcm16_be_stereo(
    std::span<const std::byte> source) {
    if ((source.size() & 3u) != 0u) {
        throw std::invalid_argument(
            "AI PCM buffer must contain complete stereo PCM16 frames");
    }

    Pcm16BeStereoAnalysis analysis{};
    analysis.sample_count = source.size() / sizeof(std::uint16_t);
    analysis.content_hash = kFnv1aOffset;
    if (source.size() >= 4u) {
        analysis.first_left =
            (std::to_integer<std::uint16_t>(source[0]) << 8u) |
            std::to_integer<std::uint16_t>(source[1]);
        analysis.first_right =
            (std::to_integer<std::uint16_t>(source[2]) << 8u) |
            std::to_integer<std::uint16_t>(source[3]);
    }

    std::uint64_t sum_abs = 0u;
    for (std::size_t offset = 0; offset < source.size(); offset += 2u) {
        const std::uint8_t high = std::to_integer<std::uint8_t>(source[offset]);
        const std::uint8_t low =
            std::to_integer<std::uint8_t>(source[offset + 1u]);
        analysis.content_hash ^= high;
        analysis.content_hash *= kFnv1aPrime;
        analysis.content_hash ^= low;
        analysis.content_hash *= kFnv1aPrime;

        const std::uint16_t bits =
            (static_cast<std::uint16_t>(high) << 8u) |
            static_cast<std::uint16_t>(low);
        const auto signed_sample = static_cast<std::int16_t>(bits);
        const std::uint32_t magnitude =
            signed_sample == std::numeric_limits<std::int16_t>::min()
                ? 32768u
                : static_cast<std::uint32_t>(
                      std::abs(static_cast<int>(signed_sample)));
        if (bits != 0u) {
            ++analysis.nonzero_samples;
        }
        analysis.peak = std::max(analysis.peak, magnitude);
        sum_abs += magnitude;
    }
    if (analysis.sample_count != 0u) {
        analysis.mean_abs = static_cast<std::uint32_t>(
            sum_abs / analysis.sample_count);
    }
    return analysis;
}

void AiPcmIdentityTracker::reset() {
    stats_ = {};
    previous_content_.clear();
    previous_audible_content_.clear();
    distinct_contents_.clear();
    distinct_hash_buckets_.clear();
    previous_address_ = 0u;
    previous_sample_rate_ = 0u;
    previous_audible_sample_rate_ = 0u;
    current_identical_audible_run_ = 0u;
    current_identical_audible_run_duration_ns_ = 0u;
    current_repeated_known_content_run_ = 0u;
    current_repeated_known_content_run_duration_ns_ = 0u;
    current_repeated_known_content_run_has_audible_ = false;
    have_previous_content_ = false;
    have_previous_sample_rate_ = false;
    have_previous_audible_content_ = false;
}

bool AiPcmIdentityTracker::same_previous_content(
    std::span<const std::byte> source) const noexcept {
    return have_previous_content_ && equal_bytes(previous_content_, source);
}

bool AiPcmIdentityTracker::same_previous_audible_content(
    std::uint32_t sample_rate,
    std::span<const std::byte> source) const noexcept {
    return have_previous_audible_content_ &&
           previous_audible_sample_rate_ == sample_rate &&
           equal_bytes(previous_audible_content_, source);
}

bool AiPcmIdentityTracker::record_distinct_content(
    std::uint64_t hash,
    std::span<const std::byte> source) {
    auto& bucket = distinct_hash_buckets_[hash];
    for (const std::size_t index : bucket) {
        if (index >= distinct_contents_.size()) {
            throw std::logic_error("AI PCM identity bucket is corrupt");
        }
        if (equal_bytes(distinct_contents_[index], source)) {
            return true;
        }
    }

    const std::size_t index = distinct_contents_.size();
    distinct_contents_.emplace_back(source.begin(), source.end());
    bucket.push_back(index);
    stats_.distinct_content_identities = distinct_contents_.size();
    return false;
}

void AiPcmIdentityTracker::observe(
    std::uint32_t guest_address,
    std::uint32_t sample_rate,
    std::span<const std::byte> source,
    const Pcm16BeStereoAnalysis& analysis) {
    if ((source.size() & 3u) != 0u) {
        throw std::invalid_argument(
            "AI PCM identity input must contain complete stereo frames");
    }
    if (analysis.sample_count != source.size() / sizeof(std::uint16_t)) {
        throw std::invalid_argument(
            "AI PCM analysis does not match the observed buffer size");
    }
    if (sample_rate != 32'000u && sample_rate != 48'000u) {
        throw std::invalid_argument(
            "AI PCM identity sample rate is not a native 32 or 48 kHz rate");
    }

    const std::uint64_t stereo_frames = source.size() / 4u;
    const std::uint64_t duration_ns =
        buffer_duration_ns(source, sample_rate);
    ++stats_.buffers;
    stats_.stereo_frames += stereo_frames;
    stats_.total_duration_ns += duration_ns;
    if (stats_.buffers == 1u) {
        stats_.min_buffer_duration_ns = duration_ns;
    } else {
        stats_.min_buffer_duration_ns =
            std::min(stats_.min_buffer_duration_ns, duration_ns);
    }
    stats_.max_buffer_duration_ns =
        std::max(stats_.max_buffer_duration_ns, duration_ns);
    if (sample_rate == 32'000u) {
        ++stats_.buffers_32000;
        stats_.stereo_frames_32000 += stereo_frames;
    } else {
        ++stats_.buffers_48000;
        stats_.stereo_frames_48000 += stereo_frames;
    }
    if (have_previous_sample_rate_ && previous_sample_rate_ != sample_rate) {
        ++stats_.sample_rate_change_events;
    }
    previous_sample_rate_ = sample_rate;
    have_previous_sample_rate_ = true;
    stats_.nonzero_samples += analysis.nonzero_samples;
    stats_.peak_window = std::max(stats_.peak_window, analysis.peak);

    const bool same_content = same_previous_content(source);
    if (have_previous_content_) {
        stats_.content_change_events += same_content ? 0u : 1u;
        stats_.address_change_events +=
            previous_address_ == guest_address ? 0u : 1u;
    }
    const bool audible = analysis.nonzero_samples != 0u;
    const bool content_was_known =
        record_distinct_content(analysis.content_hash, source);
    if (content_was_known) {
        ++current_repeated_known_content_run_;
        current_repeated_known_content_run_duration_ns_ += duration_ns;
        current_repeated_known_content_run_has_audible_ =
            current_repeated_known_content_run_has_audible_ || audible;
        // Pure digital silence is not a stuck audible note. Silent buffers do
        // remain part of a repeated sequence once that sequence contains any
        // audible PCM, which catches A/silence/A/silence freeze patterns.
        if (current_repeated_known_content_run_has_audible_) {
            stats_.repeated_known_content_run_max = std::max(
                stats_.repeated_known_content_run_max,
                current_repeated_known_content_run_);
            stats_.repeated_known_content_run_max_duration_ns = std::max(
                stats_.repeated_known_content_run_max_duration_ns,
                current_repeated_known_content_run_duration_ns_);
        }
    } else {
        current_repeated_known_content_run_ = 0u;
        current_repeated_known_content_run_duration_ns_ = 0u;
        current_repeated_known_content_run_has_audible_ = false;
    }

    if (audible) {
        ++stats_.audible_buffers;
        if (same_previous_audible_content(sample_rate, source)) {
            ++current_identical_audible_run_;
            current_identical_audible_run_duration_ns_ += duration_ns;
        } else {
            current_identical_audible_run_ = 1u;
            current_identical_audible_run_duration_ns_ = duration_ns;
        }
        stats_.identical_audible_run_max = std::max(
            stats_.identical_audible_run_max,
            current_identical_audible_run_);
        stats_.identical_audible_run_max_duration_ns = std::max(
            stats_.identical_audible_run_max_duration_ns,
            current_identical_audible_run_duration_ns_);
        previous_audible_content_.assign(source.begin(), source.end());
        previous_audible_sample_rate_ = sample_rate;
        have_previous_audible_content_ = true;
    } else {
        previous_audible_content_.clear();
        previous_audible_sample_rate_ = 0u;
        current_identical_audible_run_ = 0u;
        current_identical_audible_run_duration_ns_ = 0u;
        have_previous_audible_content_ = false;
    }

    previous_content_.assign(source.begin(), source.end());
    previous_address_ = guest_address;
    have_previous_content_ = true;
}

}  // namespace galaxy::audio
