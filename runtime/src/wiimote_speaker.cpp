#include "galaxy/wiimote_speaker.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace galaxy::input::speaker {

namespace {

constexpr std::uint32_t kRingMask = Speaker::kRingSamples - 1u;
static_assert((Speaker::kRingSamples & kRingMask) == 0u, "the ring size is a power of two");
constexpr std::uint32_t kTaps = Speaker::kTaps;
// Window samples before the output position.
constexpr std::uint32_t kLeadIn = kTaps / 2u - 1u;
constexpr std::uint32_t kPhaseBits = 8u;
constexpr std::uint32_t kPhases = 1u << kPhaseBits;
// A sound starts once the output buffer and one more report (40 ADPCM samples) are covered, so the reports that
// arrive while the buffer plays keep it going.
constexpr std::uint32_t kStartMargin = 40u;
// A backlog beyond this (reports from before the output caught up) is skipped.
constexpr std::uint32_t kMostBufferedMilliseconds = 200u;

using Row = std::array<float, kTaps>;

// Kaiser-windowed sinc (beta 6) cut off at 0.45 of the speaker rate; one row of taps per 1/256 sample, each row
// normalised to unit gain.
std::array<Row, kPhases> build_filter() noexcept {
    constexpr double kCutoff = 0.45;
    constexpr double kBeta = 6.0;
    constexpr double kHalfWidth = kTaps / 2.0;
    const auto bessel_i0 = [](double x) noexcept {
        double sum = 1.0;
        double term = 1.0;
        for (int k = 1; k < 32; ++k) {
            const double half = x / (2.0 * k);
            term *= half * half;
            sum += term;
        }
        return sum;
    };
    const double window_scale = 1.0 / bessel_i0(kBeta);
    std::array<Row, kPhases> rows{};
    for (std::uint32_t phase = 0; phase < kPhases; ++phase) {
        const double fraction = static_cast<double>(phase) / kPhases;
        std::array<double, kTaps> weights{};
        double sum = 0.0;
        for (std::uint32_t tap = 0; tap < kTaps; ++tap) {
            const double x = fraction + static_cast<double>(kLeadIn) - static_cast<double>(tap);
            const double u = std::numbers::pi * 2.0 * kCutoff * x;
            const double sinc = u == 0.0 ? 1.0 : std::sin(u) / u;
            const double edge = x / kHalfWidth;
            const double window =
                edge * edge < 1.0 ? bessel_i0(kBeta * std::sqrt(1.0 - edge * edge)) * window_scale : 0.0;
            weights[tap] = sinc * window;
            sum += weights[tap];
        }
        for (std::uint32_t tap = 0; tap < kTaps; ++tap) {
            rows[phase][tap] = static_cast<float>(weights[tap] / sum);
        }
    }
    return rows;
}

// Built at startup so the first sound costs nothing extra on the game thread.
const std::array<Row, kPhases> kFilter = build_filter();

std::int32_t load_le16(const std::byte* sample) noexcept {
    return static_cast<std::int16_t>(
        std::to_integer<std::uint16_t>(sample[0]) | (std::to_integer<std::uint16_t>(sample[1]) << 8u));
}

void store_le16(std::byte* sample, std::int32_t value) noexcept {
    const auto bits = static_cast<std::uint16_t>(static_cast<std::int16_t>(value));
    sample[0] = static_cast<std::byte>(bits & 0xFFu);
    sample[1] = static_cast<std::byte>(bits >> 8u);
}

}  // namespace

void Speaker::mix(std::span<std::byte> stereo_le, std::uint32_t output_rate, float gain) noexcept {
    if (!(gain > 0.0f)) {
        if (listening_.load(std::memory_order_relaxed)) listening_.store(false, std::memory_order_relaxed);
        return;
    }
    const std::uint32_t head = head_.load(std::memory_order_acquire);
    std::uint32_t tail = tail_.load(std::memory_order_relaxed);
    if (!listening_.load(std::memory_order_relaxed)) {
        // Whatever arrived before the volume was raised is stale.
        listening_.store(true, std::memory_order_relaxed);
        tail_.store(head, std::memory_order_release);
        window_ = {};
        fraction_ = 0;
        owed_shifts_ = 0;
        silent_shifts_ = kTaps;
        last_head_ = head;
        playing_ = false;
        return;
    }
    std::uint32_t available = head - tail;
    const bool growing = head != last_head_;
    last_head_ = head;
    if (!playing_ && available == 0u) return;
    const std::uint32_t rate = sample_rate_.load(std::memory_order_relaxed);
    const std::size_t frames = stereo_le.size() / 4u;
    if (rate == 0u || output_rate == 0u || frames == 0u) return;
    const std::uint32_t most = std::max(rate * kMostBufferedMilliseconds / 1000u, 4u * kTaps);
    if (available > most) {
        tail += available - most;
        available = most;
    }
    const std::uint64_t step = (static_cast<std::uint64_t>(rate) << 32u) / output_rate;
    if (!playing_) {
        const std::uint64_t needed = owed_shifts_ + ((fraction_ + frames * step) >> 32u);
        if (growing && available < needed + kStartMargin) {
            tail_.store(tail, std::memory_order_release);
            return;
        }
        playing_ = true;
    }
    std::byte* sample = stereo_le.data();
    for (std::size_t frame = 0; frame < frames; ++frame, sample += 4) {
        for (; owed_shifts_ != 0u; --owed_shifts_) {
            float next = 0.0f;
            if (available != 0u) {
                next = ring_[tail & kRingMask];
                ++tail;
                --available;
                silent_shifts_ = 0u;
            } else if (!growing && silent_shifts_ < kTaps) {
                // The stream ended: play out the window.
                ++silent_shifts_;
            } else {
                break;
            }
            std::copy(window_.begin() + 1, window_.end(), window_.begin());
            window_.back() = next;
        }
        if (owed_shifts_ != 0u) {
            playing_ = false;
            break;
        }
        const Row& row = kFilter[fraction_ >> (32u - kPhaseBits)];
        float value = 0.0f;
        for (std::uint32_t tap = 0; tap < kTaps; ++tap) value += window_[tap] * row[tap];
        const auto add = static_cast<std::int32_t>(std::lrint(value * gain));
        store_le16(sample, std::clamp(load_le16(sample) + add, -32768, 32767));
        store_le16(sample + 2u, std::clamp(load_le16(sample + 2u) + add, -32768, 32767));
        const std::uint64_t position = static_cast<std::uint64_t>(fraction_) + step;
        fraction_ = static_cast<std::uint32_t>(position);
        owed_shifts_ = static_cast<std::uint32_t>(position >> 32u);
    }
    tail_.store(tail, std::memory_order_release);
}

}  // namespace galaxy::input::speaker
