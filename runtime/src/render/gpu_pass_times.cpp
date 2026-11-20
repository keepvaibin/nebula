// SPDX-License-Identifier: GPL-3.0-only
// The GPU's time by pass (gpu_pass_times.h).
#include "galaxy/render/gpu_pass_times.h"

#include "galaxy/benchmark.h"
#include "galaxy/render_api.h"

#include <cstdio>
#include <utility>

namespace galaxy::render {

const char* const kGpuPassPageNames[kGpuPassCount] = {
    "scene", "resolve", "frame read", "display copy", "bloom", "scene aa", "scene ao", "upscale", "present", "camera",
    "ao distance", "ao occlusion", "ao denoise", "ao upsample", "ao apply", "uploads"};
const char* const kGpuPassLogNames[kGpuPassCount] = {
    "scene", "resolve", "frame-read", "display-copy", "bloom", "scene-aa", "scene-ao", "upscale", "present", "camera",
    "ao-distance", "ao-occlusion", "ao-denoise", "ao-upsample", "ao-apply", "uploads"};

GpuPassMs gpu_pass_ms(const std::uint64_t* ticks, const GpuPass* marks, unsigned interval_count, double ms_per_tick) noexcept {
    GpuPassMs ms{};
    for (unsigned i = 0u; i < interval_count; ++i) {
        const auto pass = static_cast<std::size_t>(marks[i]);
        if (pass < kGpuPassCount && ticks[i + 1u] >= ticks[i]) {
            ms[pass] += static_cast<double>(ticks[i + 1u] - ticks[i]) * ms_per_tick;
        }
    }
    return ms;
}

bool GpuPassTimes::begin_frame(bool timed) noexcept {
    if (!timed && timed_) {
        for (std::atomic<float>& average : average_) {
            average.store(0.0f, std::memory_order_relaxed);
        }
    }
    timed_ = timed;
    return timed;
}

void GpuPassTimes::add_frame(const GpuPassMs& frame_ms, bool game_frame) noexcept {
    for (std::size_t pass = 0u; pass < kGpuPassCount; ++pass) {
        window_[pass] += frame_ms[pass];
        if (frame_ms[pass] > 0.0) {
            const float before = average_[pass].load(std::memory_order_relaxed);
            const float value = static_cast<float>(frame_ms[pass]);
            average_[pass].store(before == 0.0f ? value : before + (value - before) * 0.1f, std::memory_order_relaxed);
        }
    }
    ++window_frames_;
    galaxy::bench::note_gpu_frame(kGpuPassPageNames, frame_ms.data(), kGpuPassCount, game_frame);
}

std::size_t GpuPassTimes::page_times(GpuPassTime* out, std::size_t capacity) const noexcept {
    std::size_t count = 0u;
    for (std::size_t pass = 0u; pass < kGpuPassCount && count < capacity; ++pass) {
        if (const float ms = average_[pass].load(std::memory_order_relaxed); ms > 0.0f) {
            out[count++] = {kGpuPassPageNames[pass], ms};
        }
    }
    return count;
}

std::string GpuPassTimes::take_window(bool field) {
    std::string text;
    if (window_frames_ != 0u) {
        char item[64];
        for (std::size_t pass = 0u; pass < kGpuPassCount; ++pass) {
            if (window_[pass] <= 0.0) {
                continue;
            }
            const double ms = window_[pass] / static_cast<double>(window_frames_);
            if (field) {
                std::snprintf(item, sizeof(item), "%s%s:%.3f", text.empty() ? " gpu-pass-ms=" : ",", kGpuPassLogNames[pass], ms);
            } else {
                std::snprintf(item, sizeof(item), " %s=%.3f", kGpuPassLogNames[pass], ms);
            }
            text += item;
        }
    }
    window_.fill(0.0);
    window_frames_ = 0u;
    return text;
}

std::string GpuPassTimes::take_window_list() {
    return take_window(false);
}

std::string GpuPassTimes::take_window_field() {
    return take_window(true);
}

void FrameGpuTime::add(double gpu_ms, bool scene_drawn, bool generated) noexcept {
    const auto smooth = [gpu_ms](double& ema) { ema = ema == 0.0 ? gpu_ms : ema + (gpu_ms - ema) * 0.1; };
    if (scene_drawn) {
        smooth(scene_ema_ms_);
    }
    if (generated) {
        generated_ms_ += gpu_ms;
        ++generated_samples_;
        smooth(generated_ema_ms_);
    }
}

void FrameGpuTime::take_generated(double& total_ms, std::uint64_t& samples) noexcept {
    total_ms = std::exchange(generated_ms_, 0.0);
    samples = std::exchange(generated_samples_, 0u);
}

}  // namespace galaxy::render
