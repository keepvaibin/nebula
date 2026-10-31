#include "galaxy/movie_audio.h"

#include <algorithm>
#include <cmath>

namespace galaxy::audio {
namespace {

[[nodiscard]] std::uint32_t be32(const std::uint8_t* p) noexcept {
    return (static_cast<std::uint32_t>(p[0]) << 24u) | (static_cast<std::uint32_t>(p[1]) << 16u) |
           (static_cast<std::uint32_t>(p[2]) << 8u) | static_cast<std::uint32_t>(p[3]);
}

[[nodiscard]] std::int16_t be16(const std::uint8_t* p) noexcept {
    return static_cast<std::int16_t>((static_cast<std::uint16_t>(p[0]) << 8u) | p[1]);
}

// The player's volume curve: the square of volume / 127, as a 15-bit fraction.
[[nodiscard]] float volume_gain(float volume) noexcept {
    const int step = std::clamp(static_cast<int>(volume), 0, 127);
    return std::round(32768.0f * static_cast<float>(step * step) / (127.0f * 127.0f)) / 32768.0f;
}

// The record's header: the offset of the second channel's data, the samples, each channel's eight predictor pairs
// and its two history samples (big-endian); the data from byte 0x50. DSP-ADPCM: 8-byte frames of a header byte
// (predictor in bits 4-6, scale in bits 0-3) and 14 four-bit samples.
constexpr std::size_t kHeaderBytes = 0x50u;

void decode_channel(const std::uint8_t* data, const std::uint8_t* end, const std::uint8_t* coefficients,
                    std::int32_t history1, std::int32_t history2, std::uint32_t samples, std::int16_t* out,
                    std::int16_t* also) {
    std::uint32_t predictor = 0;
    std::uint32_t scale = 0;
    for (std::uint32_t i = 0; i < samples; ++i) {
        const std::uint32_t nibble_index = i % 14u;
        const std::uint8_t* frame = data + (i / 14u) * 8u;
        if (frame + 8u > end) {
            break;
        }
        if (nibble_index == 0u) {
            predictor = (frame[0] >> 4u) & 7u;
            scale = frame[0] & 0xFu;
        }
        const std::uint8_t byte = frame[1u + nibble_index / 2u];
        const std::int32_t nibble = (nibble_index & 1u) != 0u ? static_cast<std::int8_t>(byte << 4u) >> 4
                                                               : static_cast<std::int8_t>(byte) >> 4;
        const std::int64_t c1 = be16(coefficients + predictor * 4u);
        const std::int64_t c2 = be16(coefficients + predictor * 4u + 2u);
        std::int64_t value = c1 * history1 + c2 * history2 + (static_cast<std::int64_t>(nibble) << scale << 11);
        value = std::clamp<std::int64_t>((value << 5) + 0x8000, -2147483648LL, 2147483647LL);
        const auto sample = static_cast<std::int16_t>(value >> 16);
        out[i * 2u] = sample;
        if (also != nullptr) {
            also[i * 2u] = sample;
        }
        history2 = history1;
        history1 = sample;
    }
}

}  // namespace

bool MovieAudio::decode(const std::uint8_t* record, std::size_t size, std::vector<std::int16_t>& out) {
    if (record == nullptr || size < kHeaderBytes) {
        return false;
    }
    const std::uint32_t second = be32(record);
    const std::uint32_t samples = be32(record + 4u);
    if (samples > 0x10000u) {
        return false;
    }
    out.assign(static_cast<std::size_t>(samples) * 2u, 0);
    const std::uint8_t* end = record + size;
    const std::uint8_t* left = record + kHeaderBytes;
    if (second == 0u) {
        decode_channel(left, end, record + 8u, be16(record + 0x48u), be16(record + 0x4Au), samples, out.data(),
                       out.data() + 1);
        return true;
    }
    if (second > size - kHeaderBytes) {
        return false;
    }
    decode_channel(left, end, record + 8u, be16(record + 0x48u), be16(record + 0x4Au), samples, out.data(), nullptr);
    decode_channel(left + second, end, record + 0x28u, be16(record + 0x4Cu), be16(record + 0x4Eu), samples,
                   out.data() + 1, nullptr);
    return true;
}

void MovieAudio::open(bool mono) {
    std::lock_guard<std::mutex> lock(mutex_);
    open_ = true;
    mono_ = mono;
    queue_.clear();
    volume_ = target_ = 127.0f;
    ramp_ = 0;
    resuming_ = false;
    silence_ = 0;
    fade_ = 1.0f;
    fade_step_ = 0.0f;
}

void MovieAudio::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    open_ = false;
    queue_.clear();
}

void MovieAudio::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.clear();
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
    Frame frame;
    if (!decode(record, size, frame.samples)) {
        return false;
    }
    queue_.push_back(std::move(frame));
    return true;
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
        fade_step_ = 0.001f;
        silence_ = 4800;
        if (queue_.size() < 3u) {
            return;
        }
    } else if (queue_.size() < 2u) {
        return;
    }
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
            if (silence_ > 0) {
                --silence_;
                volume = 0.0f;
            } else if (fade_ < 1.0f) {
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
