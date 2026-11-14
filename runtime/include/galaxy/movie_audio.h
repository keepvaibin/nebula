// The movie player's sound (nebula_game_api.h movie_audio*): each movie frame's audio record (its stereo sample count, then
// its PCM16 samples, cook/thp.rs) queued, up to 20 frames ahead; the mixer takes it at 32 kHz each audio frame with the
// player's volume (a square law over 0-127, ramped per sample), its fade-in after a pause and, in mono output, the
// mean of the two sides at -3 dB. Playback starts once two frames wait. The player shows the frame at this queue's front (MoviePlayerSimple::exePlaying).
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
    static constexpr std::uint32_t kRate = 32000u;

    void open(bool mono);
    void close();
    // The queued frames dropped, the volume back to full.
    void reset();
    // 0-127, reached over `milliseconds` (32 samples a millisecond).
    void set_volume(std::int32_t volume, std::int32_t milliseconds);
    // After a pause: once two frames wait, 4800 samples of silence, then a fade in of 0.001 a sample.
    void resume();
    // One frame's record; false when the queue is full or the record is malformed (nothing
    // taken). Null asks only whether there is room.
    bool push(const std::uint8_t* record, std::size_t size);
    // `count` stereo samples at 32 kHz, interleaved (left, right), added to `out`.
    void pull(float* out, std::size_t count);
    [[nodiscard]] bool active() const;
    // The frames that could still be queued.
    [[nodiscard]] std::size_t room() const;
    // How much of what the mixer made (samples at kRate, the movie's and every other sound's) has not been played
    // yet, as the output takes it.
    void set_unplayed(std::size_t samples);
    // Where playing has reached: the frames played in full and the fraction of the next.
    [[nodiscard]] double position();

private:
    struct Frame {
        std::vector<std::int16_t> samples;  // interleaved (left, right)
        std::size_t read{};
        std::int64_t start{};  // where its first sample went, in made_ (set when it starts playing)
    };

    mutable std::mutex mutex_;
    std::deque<Frame> queue_;
    bool open_{};
    bool started_{};
    // One clock for the picture: made_ counts every sample the mixer asked pull for since open (movie or not), and
    // each frame pull finished is stamped there with where its first sample went and the place after its last.
    // made_ - unplayed_ is what the output has played, so a frame is heard once its last sample has left the output,
    // with or without other sound after the movie.
    struct Span {
        std::int64_t start;
        std::int64_t end;
    };
    std::deque<Span> ends_;
    std::int64_t made_{};
    std::size_t unplayed_{};
    std::int32_t heard_{};
    bool mono_{};
    float volume_{127.0f};
    float target_{127.0f};
    float step_{};
    std::int32_t ramp_{};
    bool resuming_{};
    float fade_{1.0f};
    float fade_step_{};
};

}  // namespace galaxy::audio
