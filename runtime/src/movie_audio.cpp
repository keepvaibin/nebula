#include "galaxy/movie_audio.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace galaxy::audio {
namespace {

// The player's volume curve: the square of volume / 127, as a 15-bit fraction.
[[nodiscard]] float volume_gain(float volume) noexcept {
    const int step = std::clamp(static_cast<int>(volume), 0, 127);
    return std::round(32768.0f * static_cast<float>(step * step) / (127.0f * 127.0f)) / 32768.0f;
}

}  // namespace

void MovieAudio::open(bool mono) {
    std::lock_guard<std::mutex> lock(mutex_);
    open_ = true;
    mono_ = mono;
    queue_.clear();
    started_ = false;
    ends_.clear();
    queued_end_ = 0;
    pulled_ = 0;
    unplayed_ = 0u;
    heard_ = 0;
    volume_ = target_ = 127.0f;
    ramp_ = 0;
    resuming_ = false;
    fade_ = 1.0f;
    fade_step_ = 0.0f;
}

void MovieAudio::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    open_ = false;
    queue_.clear();
    started_ = false;
    ends_.clear();
    queued_end_ = 0;
    pulled_ = 0;
    unplayed_ = 0u;
    heard_ = 0;
}

void MovieAudio::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.clear();
    started_ = false;
    ends_.clear();
    queued_end_ = 0;
    pulled_ = 0;
    unplayed_ = 0u;
    heard_ = 0;
    volume_ = target_;
    ramp_ = 0;
}

void MovieAudio::set_volume(std::int32_t volume, std::int32_t milliseconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    target_ = static_cast<float>(std::clamp(volume, 0, 127));
    milliseconds = std::clamp(milliseconds, 0, 60000);
    if (milliseconds > 0) {
        ramp_ = 32 * milliseconds;
        step_ = (target_ - volume_) / static_cast<float>(ramp_);
    } else {
        ramp_ = 0;
        volume_ = target_;
    }
}

void MovieAudio::resume() {
    std::lock_guard<std::mutex> lock(mutex_);
    resuming_ = true;
}

bool MovieAudio::push(const std::uint8_t* record, std::size_t size) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.size() >= kQueueFrames) {
        return false;
    }
    if (record == nullptr) {
        return true;
    }
    std::uint32_t samples = 0u;
    if (size < sizeof(samples)) {
        return false;
    }
    std::memcpy(&samples, record, sizeof(samples));
    if (samples > (size - sizeof(samples)) / (2u * sizeof(std::int16_t))) {
        return false;
    }
    Frame frame;
    frame.samples.resize(static_cast<std::size_t>(samples) * 2u);
    std::memcpy(frame.samples.data(), record + sizeof(samples), frame.samples.size() * sizeof(std::int16_t));
    const std::int64_t begin = std::max(queued_end_, pulled_);
    queued_end_ = begin + samples;
    ends_.push_back({begin, queued_end_});
    queue_.push_back(std::move(frame));
    return true;
}

void MovieAudio::set_unplayed(std::size_t samples) {
    std::lock_guard<std::mutex> lock(mutex_);
    unplayed_ = samples;
}

double MovieAudio::position() {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::int64_t reached = pulled_ - static_cast<std::int64_t>(unplayed_);
    while (!ends_.empty() && ends_.front().end <= reached) {
        ends_.pop_front();
        ++heard_;
    }
    if (ends_.empty() || reached <= ends_.front().begin) {
        return heard_;
    }
    return heard_ + static_cast<double>(reached - ends_.front().begin) /
                        static_cast<double>(ends_.front().end - ends_.front().begin);
}

std::size_t MovieAudio::room() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size() < kQueueFrames ? kQueueFrames - queue_.size() : 0u;
}

bool MovieAudio::active() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return open_;
}

void MovieAudio::pull(float* out, std::size_t count) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!open_) {
        return;
    }
    if (resuming_) {
        resuming_ = false;
        fade_ = 0.0f;
        fade_step_ = 1.0f / 160.0f;
        if (queue_.size() < 3u) {
            return;
        }
    } else if (!started_ && queue_.size() < 2u) {
        return;
    }
    started_ = true;
    // The output keeps running after the queue empties: its silence lets the last movie samples pass through
    // the device's latency. Counting only movie samples would leave position() forever short of the end.
    pulled_ += static_cast<std::int64_t>(count);
    while (count != 0u && !queue_.empty()) {
        Frame& frame = queue_.front();
        const std::size_t available = (frame.samples.size() - frame.read) / 2u;
        const std::size_t n = std::min(available, count);
        for (std::size_t i = 0; i < n; ++i) {
            if (ramp_ != 0) {
                --ramp_;
                volume_ += step_;
            } else {
                volume_ = target_;
            }
            float volume = volume_;
            if (fade_ < 1.0f) {
                volume = volume_ * fade_;
                fade_ += fade_step_;
                if (fade_ >= 1.0f) {
                    fade_ = 1.0f;
                    fade_step_ = 0.0f;
                }
            }
            const float gain = volume_gain(volume) / 32768.0f;
            float left = frame.samples[frame.read] * gain;
            float right = frame.samples[frame.read + 1u] * gain;
            if (mono_) {
                left = right = 0.707f * (left + right) / 2.0f;
            }
            out[0] += left;
            out[1] += right;
            out += 2;
            frame.read += 2u;
        }
        count -= n;
        if (frame.read >= frame.samples.size()) {
            queue_.pop_front();
        }
    }
}

}  // namespace galaxy::audio
