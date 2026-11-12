// SPDX-License-Identifier: GPL-3.0-only
// The host's side of LumaCore's mods services (contract services-mods): the providers of `mods`, `mods.ids` and
// `mods.install`, and the calls the Mods page and the start make on the numbers' record.
#pragma once

#include <luma_service_provider.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy::luma_mods {

// The three providers, in catalog order, for luma_services::provide_all (game thread, once, before the first mod).
// `log` says what goes wrong with the record outside any mod's call.
[[nodiscard]] std::span<const LumaServiceProvider* const> providers(void (*log)(const char*)) noexcept;

// A removed mod's kept data, for the Mods page's REMOVED MODS line (contract item 8, J-SM7): a mod is removed when it
// is in no list this session and its folder is gone.
struct KeptMod {
    std::string id;
    std::uint32_t numbers = 0;    // its keys in the record, all spaces
    std::uint64_t save_bytes = 0; // its saved values (services-save 5), from the save half's counter
    std::uint64_t data_bytes = 0; // the files in its fs data folder (services-fs 3)
};
// Any thread but the game thread (it reads the fs data folders' sizes from the disk). The record is read first when
// this session has not read it yet.
void kept(std::vector<KeptMod>& out);
// Any thread: the player's FORGET. Drops `id`'s keys from the record (written at once on the record's writer thread),
// asks the save half to drop its values and fs to remove its data folder; one Note Mods.Forgotten.
void forget(std::string_view id);
// Game thread, at a renamed mod's first start (services-save 6, J-SM12): `old_id`'s keys become `new_id`'s with the
// same numbers, in every space. Returns how many moved; a key the new id already holds keeps the new id's number.
std::uint32_t take_over_ids(std::string_view old_id, std::string_view new_id);

// The save half (services-save 5): the ids it keeps values for, their bytes and its forget, each callable from any
// thread. kept() walks its ids with the record's, so a removed mod with saved values and no numbers has its line.
// Until it is given, kept() walks the record alone.
struct SaveHalf {
    void (*ids)(std::vector<std::string>& out) = nullptr;
    std::uint64_t (*bytes)(std::string_view id) = nullptr;
    void (*forget)(std::string_view id) = nullptr;
};
void set_save_half(const SaveHalf& half) noexcept;
// The game module's half (luma_service_provider.h luma_save_kept, luma_save_forget): values in the user files in
// memory. Given by luma_services_host at the services' start; null until then (or with a module without them).
void set_chunk_half(LumaSaveKept kept_ids, LumaSaveForget forget) noexcept;

}  // namespace galaxy::luma_mods
