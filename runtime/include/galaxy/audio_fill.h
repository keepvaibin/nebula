// SPDX-License-Identifier: GPL-3.0-only
// How a sound output keeps ahead of the device, apart from any device so it can be tested.
//
// OutputFill: the frames an event-driven output (WASAPI shared mode) keeps queued ahead of the one playing. The game's
// whole sound frame is made inside the device's callback, so the headroom has to cover the slowest callback, not the
// average one. It starts at two engine periods; a late wake or a slow render raises it a period, up to kMostPeriods; it
// never goes down while the device stays open, so a machine that once needed more is not taken back into the gap.
// Every wake that finds the device empty is an under-run, counted.
//
// ChunkedPull: a callback that may ask for any number of frames (SDL's), served in chunks from one buffer made before
// the device starts, so the audio thread never allocates.
#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

namespace galaxy::host {

class OutputFill {
public:
    static constexpr std::uint32_t kStartPeriods = 2u;
    static constexpr std::uint32_t kMostPeriods = 4u;

    // A device opened: its engine period and its whole buffer, in frames.
    void open(std::uint32_t period_frames, std::uint32_t buffer_frames) noexcept {
        period_ = std::max(period_frames, 1u);
        buffer_ = std::max(buffer_frames, period_);
        fill_.store(std::min(period_ * kStartPeriods, buffer_), std::memory_order_relaxed);
        started_ = false;
    }

    // A wake of the device's thread: the time since the last one and one period, in the same clock units, and the
    // frames the device still held. The frames to give it now (0: it holds enough).
    std::uint32_t wake(std::int64_t since_last, std::int64_t period, std::uint32_t padding) noexcept {
        if (started_ && padding == 0u) {
            underruns_.fetch_add(1u, std::memory_order_relaxed);
        }
        started_ = true;
        if (since_last > period * 3 / 2) {
            raise();
        }
        const std::uint32_t fill = fill_.load(std::memory_order_relaxed);
        return padding >= fill ? 0u : fill - padding;
    }

    // A render that took `took` of one `period`: over three quarters leaves too little for the next wake.
    void rendered(std::int64_t took, std::int64_t period) noexcept {
        if (took > period * 3 / 4) {
            raise();
        }
    }

    [[nodiscard]] std::uint32_t fill() const noexcept { return fill_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t underruns() const noexcept { return underruns_.load(std::memory_order_relaxed); }

private:
    void raise() noexcept {
        const std::uint32_t fill = fill_.load(std::memory_order_relaxed);
        fill_.store(std::min({fill + period_, period_ * kMostPeriods, buffer_}), std::memory_order_relaxed);
    }

    std::uint32_t period_ = 1u;
    std::uint32_t buffer_ = 1u;
    bool started_ = false;
    std::atomic<std::uint32_t> fill_{1u};
    std::atomic<std::uint64_t> underruns_{0u};
};

class ChunkedPull {
public:
    // The buffer, made here (before the device starts): `chunk_frames` frames of `channels` samples.
    ChunkedPull(std::uint32_t chunk_frames, std::uint32_t channels)
        : chunk_(std::max(chunk_frames, 1u)), channels_(std::max(channels, 1u)),
          buffer_(static_cast<std::size_t>(chunk_) * channels_) {}

    // `frames` frames: render(out, n) then put(out, n) for each chunk of at most chunk() frames, in order.
    template <typename Render, typename Put>
    void pull(std::uint32_t frames, Render&& render, Put&& put) {
        while (frames > 0u) {
            const std::uint32_t n = std::min(frames, chunk_);
            render(buffer_.data(), n);
            put(static_cast<const float*>(buffer_.data()), n);
            frames -= n;
        }
    }

    [[nodiscard]] std::uint32_t chunk() const noexcept { return chunk_; }
    [[nodiscard]] const float* data() const noexcept { return buffer_.data(); }

private:
    std::uint32_t chunk_;
    std::uint32_t channels_;
    std::vector<float> buffer_;
};

}  // namespace galaxy::host
