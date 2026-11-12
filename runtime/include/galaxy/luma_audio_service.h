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

// The providers, in catalog order, for luma_services::provide_all (game thread, once, before the first mod).
[[nodiscard]] std::span<const LumaServiceProvider* const> providers(void (*log)(const char*)) noexcept;

// The audio frame's thread (native_audio.cpp NativeGameAudio::frame), before the master volume: adds every mod's voices
// and streams into `stereo` (`frames` interleaved left, right, at `rate`), each bus times `player` (the player's
// volume for effects, ui, voice, music: 0 to 4). False, with `stereo` untouched: no mod sound playing. No mod's code
// runs here; it waits for the game thread only while a call of the service holds the service's lock.
bool mix(float* stereo, std::size_t frames, std::uint32_t rate, const float player[4]) noexcept;

}  // namespace galaxy::luma_audio
