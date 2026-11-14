// SPDX-License-Identifier: GPL-3.0-only
// The host's side of LumaCore's file services (docs/modding/services/fs.md): the providers of `fs` and `fs.exchange`, the
// folders they use, and what save.game's offer, mods.install and the Mods page's REMOVED MODS line read of them.
#pragma once

#include <luma_service_provider.h>

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy::luma_fs {

// The two providers, in catalog order, for luma_services::provide_all (game thread, once, before the first mod).
// `log` says what goes wrong outside any mod's call (a worker that cannot start).
[[nodiscard]] std::span<const LumaServiceProvider* const> providers(void (*log)(const char*)) noexcept;

// Any thread. A mod's data folder (<Nebula's data folder>/mod-data/<id>) and the Exchange folder
// (<Nebula's data folder>/Exchange); empty when the platform gives no data folder or `id` is outside the id rule.
[[nodiscard]] std::filesystem::path data_folder(std::string_view id);
[[nodiscard]] std::filesystem::path exchange_folder();

// Any thread. `path` keeps the path rule (services/fs.md); "" only when `empty_ok`.
[[nodiscard]] bool path_in_rule(std::string_view path, bool empty_ok) noexcept;

// Any thread; it touches the disk (a few metadata reads), so on the game thread only for a request the player
// answers on a screen that pauses the game: mods.install and save.game's offer, which take a file the mod names in
// its data folder at the call. The file at `path` (in the rule) in id's data folder: LUMA_OK and its place in *out;
// LUMA_E_NOT_FOUND (none, or a folder); LUMA_E_DENIED (through a link or junction).
[[nodiscard]] std::int32_t find_data_file(std::string_view id, std::string_view path, std::filesystem::path* out);

// Any thread, off the game thread. The bytes of the files in id's data folder (0 when there is none), for the Mods
// page's REMOVED MODS line; whether it has one; the ids that have one (folder names in the id rule).
[[nodiscard]] std::uint64_t data_bytes(std::string_view id);
[[nodiscard]] bool has_data_folder(std::string_view id);
void data_ids(std::vector<std::string>& out);

// Any thread: the player's FORGET. id's data folder is removed on the fs worker, after every request queued before.
void forget_data(std::string_view id);

}  // namespace galaxy::luma_fs
