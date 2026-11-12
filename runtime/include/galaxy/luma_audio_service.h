// SPDX-License-Identifier: GPL-3.0-only
// The host's side of LumaCore's audio service (contract services-audio; plan 2.6): the provider of `audio` (layer 1, a
// mod's cooked sound effects; layer 2, the streams it fills) and the mix the game's audio frame adds.
#pragma once

#include <luma_service_provider.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace galaxy::assets {
class AssetStore;
}

namespace galaxy::luma_audio {

// The game's store (host_main open_pak, as luma_assets::set_store): the mods' cooked sounds (Luma/<id>/<path>.nsfx)
// are read in place from it. Until it is given, play answers LUMA_E_NOT_FOUND.
void set_store(const galaxy::assets::AssetStore* store) noexcept;

// The game module's half of the game's own sounds and of where the camera hears from (LumaGameSound.cpp), looked up
// by name (luma_services::provide_all, game thread, before the first mod). Without all of it, game_play answers
// LUMA_E_ABSENT and a placed voice is heard as one with no place.
void set_game(void* (*find_symbol)(const char*)) noexcept;

// The providers, in catalog order, for luma_services::provide_all (game thread, once, before the first mod).
[[nodiscard]] std::span<const LumaServiceProvider* const> providers(void (*log)(const char*)) noexcept;

// The audio frame's thread (native_audio.cpp NativeGameAudio::frame), before the master volume: adds every mod's voices
// and streams into `stereo` (`frames` interleaved left, right, at `rate`), each bus times `player` (the player's
// volume for effects, ui, voice, music: 0 to 4). False, with `stereo` untouched: no mod sound playing. No mod's code
// runs here; it waits for the game thread only while a call of the service holds the service's lock.
bool mix(float* stereo, std::size_t frames, std::uint32_t rate, const float player[4]) noexcept;

// The same thread, after the output is final (master, mute, mono, the remote speaker): the output's taps
// (audio.tap_open LUMA_AUDIO_TAP_OUTPUT) get it. No tap: one atomic load.
void tap_output(const float* stereo, std::size_t frames, std::uint32_t rate) noexcept;

// The game's own music's share, 1 heard to 0 replaced by a mod's (audio.music), ramped by mix: the audio frame gives
// the game's mixer the player's music volume times this.
[[nodiscard]] float game_music_share() noexcept;

}  // namespace galaxy::luma_audio
