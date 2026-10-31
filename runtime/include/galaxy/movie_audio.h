// The movie player's sound (nebula_game_api.h movie_audio*): each THP frame's audio record decoded on arrival (its
// DSP-ADPCM channels) and queued, up to 20 frames ahead; the mixer takes it at 32 kHz each audio frame with the
// player's volume (a square law over 0-127, ramped per sample), its fade-in after a pause and, in mono output, the
// mean of the two sides at -3 dB. Playback starts once the frame after the current one is decoded too, and falls
// silent when the queue runs dry, as the game's player did.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <span>
#include <vector>

namespace galaxy::audio {

class MovieAudio {
public:
    static constexpr std::size_t kQueueFrames = 20u;

    void open(bool mono);
    void close();
    // The queued frames dropped, the volume back to full.
    void reset();
    // 0-127, reached over `milliseconds` (32 samples a millisecond).
    void set_volume(std::int32_t volume, std::int32_t milliseconds);
    // After a pause: once two frames wait, 4800 samples of silence, then a fade in of 0.001 a sample.
    void resume();
    // One frame's record (header and ADPCM data); false when the queue is full or the record is malformed (nothing
    // taken). Null asks only whether there is room.
    bool push(const std::uint8_t* record, std::size_t size);
    // `count` stereo samples at 32 kHz, interleaved (left, right), added to `out`.
    void pull(float* out, std::size_t count);
    [[nodiscard]] bool active() const;

    // A record's samples, interleaved (left, right) (tests).
    [[nodiscard]] static bool decode(const std::uint8_t* record, std::size_t size, std::vector<std::int16_t>& out);

private:
    struct Frame {
        std::vector<std::int16_t> samples;  // interleaved (left, right)
        std::size_t read{};
    };

    mutable std::mutex mutex_;
    std::deque<Frame> queue_;
    bool open_{};
    bool mono_{};
    float volume_{127.0f};
    float target_{127.0f};
    float step_{};
    std::int32_t ramp_{};
    bool resuming_{};
    std::int32_t silence_{};
    float fade_{1.0f};
    float fade_step_{};
};

}  // namespace galaxy::audio
