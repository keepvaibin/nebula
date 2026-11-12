// SPDX-License-Identifier: GPL-3.0-only
// The host's side of LumaCore's assets service (contract services-assets; plan 2.8): the provider of `assets`
// (layer 1, the game's files by path).
#pragma once

#include <luma_service_provider.h>

#include <span>

namespace galaxy::assets {
class AssetStore;
}

namespace galaxy::luma_assets {

// The game's store, once it is open (host_main open_pak), before the first mod starts. The mods' store is attached to
// it before the game starts (attach_overlay), and neither changes after: every mapped byte stays where it is until the
// process ends. Until it is given, every call answers LUMA_E_NOT_FOUND.
void set_store(const galaxy::assets::AssetStore* store) noexcept;

// The providers, in catalog order, for luma_services::provide_all (game thread, once, before the first mod).
[[nodiscard]] std::span<const LumaServiceProvider* const> providers(void (*log)(const char*)) noexcept;

}  // namespace galaxy::luma_assets
