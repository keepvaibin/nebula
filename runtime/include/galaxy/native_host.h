// The native game's Windows host services (native_host.cpp): the game's audio output, its files and the Mii
// database; the controls are galaxy/input/host_input.h.
#pragma once

#include "galaxy/host_env.h"
#include "galaxy/input/host_input.h"
#include "galaxy/controller_roster.h"
#include "galaxy/frame_cadence_diagnostics.h"
#include "galaxy/host_pointer_events.h"
#include "galaxy/input_bindings.h"
#include "galaxy/native_input.h"
#include "galaxy/wiimote_speaker.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <set>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct NebulaJAudioFxLine;  // nebula_game_api.h
struct NebulaJAudioChannel;  // nebula_game_api.h

namespace galaxy::host {

struct GamePaths {
    std::filesystem::path content_root;
};

GamePaths locate_game(const std::filesystem::path& input);

// A valid, empty Mii database (RFL_DB.dat) for a NAND that has none.
[[nodiscard]] std::vector<std::byte> native_game_empty_mii_database();

// The game's sound: the JAudio DSP program run on the host (JAudioHostMixer) over the game's channel records, and the
// audio interface's blocks played on the output device (XAudio2, master volume, WAV dump).
class NativeGameAudio {
public:
    NativeGameAudio();
    ~NativeGameAudio();
    NativeGameAudio(const NativeGameAudio&) = delete;
    NativeGameAudio& operator=(const NativeGameAudio&) = delete;

    // The game's JAudio mixer calls (nebula_game_api.h jaudio_*). mix returns true when it completed a subframe (the
    // game then posts its AUDIOMSG_DSP).
    void setup(std::uint32_t channels, NebulaJAudioChannel* channel_records, const std::int16_t* resample_filter,
               const std::int16_t* adpcm_filter, const NebulaJAudioFxLine* fx_lines);
    void set_aux(const void* base, std::uint32_t size);
    void frame(std::uint32_t subframes, std::uint32_t mixer_level, std::int16_t* left, std::int16_t* right);
    bool mix(std::uint32_t group, std::uint32_t active);
    // One AI block: 16-bit (right, left) pairs in host byte order. Call from one thread; the first call opens the
    // device.
    bool play(std::span<const std::byte> block, std::uint32_t sample_rate);
    // Blocks submitted and not yet played (the sink's pending queue and the device's).
    [[nodiscard]] unsigned queued_blocks() const;
    // The blocks the device holds before it plays (or plays again after running dry): the level to keep queued.
    [[nodiscard]] unsigned target_blocks() const;
    [[nodiscard]] bool output_available() const;
    // The device stopped while the app is out of sight (SDL hosts), queued blocks kept for its return.
    void set_paused(bool paused);
    // The game's name for the sound on a DSP channel (nebula_game_audio_channel_owner), for the volume categories.
    void set_channel_owner(std::uint32_t (*owner)(std::uint32_t channel, std::uint32_t* route));
    // The DSP program's counters for the [jaudio] log line.
    [[nodiscard]] std::string stats_line() const;

private:
    struct State;
    std::unique_ptr<State> state_;
};

}  // namespace galaxy::host
