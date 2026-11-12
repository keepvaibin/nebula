// SPDX-License-Identifier: GPL-3.0-only
// The host's side of LumaCore's assets service (contract services-assets; plan 2.8): the provider of `assets`
// (layer 1, the game's files by path; layers 2 and 3, the files mods provide, as the game is given them).
#pragma once

#include <luma_service_provider.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace galaxy::assets {
class AssetStore;
struct Entry;
}

namespace galaxy::luma_assets {

// The game's store, once it is open (host_main open_pak), before the first mod starts. The mods' store is attached to
// it before the game starts (attach_overlay), and neither changes after: every mapped byte stays where it is until the
// process ends. Until it is given, every call answers LUMA_E_NOT_FOUND.
void set_store(const galaxy::assets::AssetStore* store) noexcept;

// The providers, in catalog order, for luma_services::provide_all (game thread, once, before the first mod).
[[nodiscard]] std::span<const LumaServiceProvider* const> providers(void (*log)(const char*)) noexcept;

// The files mods provide (assets 1.1 provide and cook), for the host's asset functions the game calls (host_main
// svc_asset_*). Any thread. They are numbered from kProvidedFirst up, above every store's index; an entry once given
// keeps its index, name and bytes until the process ends (a withdrawn one too: the game may hold it), so what the game
// was told stays true.
inline constexpr std::int32_t kProvidedFirst = 0x60000000;
[[nodiscard]] inline bool is_provided(std::int32_t index) noexcept {
    return index >= kProvidedFirst;
}
// What a mod provides at the game's path (any spelling the store takes), or nothing: one atomic load while no mod
// provides anything.
[[nodiscard]] std::optional<galaxy::assets::Entry> provided_find(std::string_view path);
[[nodiscard]] std::optional<galaxy::assets::Entry> provided_entry(std::int32_t index);
[[nodiscard]] std::optional<galaxy::assets::Entry> provided_child(std::int32_t folder, std::uint32_t child);
// The bytes of a provided entry (a file's; a folder's block, its files side by side as Entry.position says), read only:
// null for an index not provided.
[[nodiscard]] const std::uint8_t* provided_view(std::int32_t index) noexcept;
// size bytes from offset of a provided entry into dst: false outside it.
[[nodiscard]] bool provided_read(std::int32_t index, std::uint64_t offset, std::uint32_t size, void* dst) noexcept;

}  // namespace galaxy::luma_assets
