// The game's sound on the host: the native mixer (audio_mixer.h) over the game's voices, its frames pulled by the platform's
// output device (audio_device.h) with the master volume and the remote speaker, and the WAV dump the tests record.
#pragma once

#include <cstdint>
#include <functional>
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

    // The game's audio calls (nebula_game_api.h audio_*), on its audio thread. setup opens the output device, or with `device`
    // false nothing pulls and the game's frames go only to the dump (deterministic mode). start: the device pulls, and when it
    // needs more `tick` has the game make its next frame (mix, frame) on the device's thread.
    void setup(NebulaVoice* voices, std::uint32_t count, const NebulaEffectLine* effects,
               bool device);
    void start(std::function<void()> tick);
    void stop();
    void mix(float gain);
    void frame();
    // The movie player's sound (nebula_game_api.h movie_audio*).
    int movie_audio(const void* record, std::uint32_t size);
    double movie_audio_position();
    void movie_audio_control(std::uint32_t what, std::int32_t value, std::int32_t milliseconds);
    // The device stopped while the app is out of sight.
    void set_paused(bool paused);
    // The mixer's counters for the log.
    [[nodiscard]] std::string stats_line() const;

private:
    struct State;
    std::unique_ptr<State> state_;
};

}  // namespace galaxy::host
