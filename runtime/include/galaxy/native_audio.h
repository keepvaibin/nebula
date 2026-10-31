// The game's sound on the host: the native mixer (audio_mixer.h) over the game's voices, its frames played on the
// platform's output device (XAudio2 on Windows, SDL elsewhere) with the master volume, and the WAV dump the tests use.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include <nebula_game_api.h>

namespace galaxy::host {

class NativeGameAudio {
public:
    NativeGameAudio();
    ~NativeGameAudio();
    NativeGameAudio(const NativeGameAudio&) = delete;
    NativeGameAudio& operator=(const NativeGameAudio&) = delete;

    // The game's audio calls (nebula_game_api.h audio_*), on its audio thread. setup opens the output device.
    void setup(NebulaAudioVoice* voices, std::uint32_t count, const NebulaAudioEffect* effects,
               const void* sound_memory, std::uint32_t sound_memory_size);
    void mix(float gain);
    void frame(const std::int16_t* stream, std::uint32_t stream_frames);
    // Frames submitted and not yet played (the sink's pending queue and the device's).
    [[nodiscard]] unsigned queued_blocks() const;
    // The frames the device holds before it plays (or plays again after running dry): the level to keep queued.
    [[nodiscard]] unsigned target_blocks() const;
    [[nodiscard]] bool output_available() const;
    // The device stopped while the app is out of sight (SDL hosts), queued frames kept for its return.
    void set_paused(bool paused);
    // The mixer's counters for the log.
    [[nodiscard]] std::string stats_line() const;

private:
    struct State;
    std::unique_ptr<State> state_;
};

}  // namespace galaxy::host
