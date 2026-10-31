// SPDX-License-Identifier: GPL-3.0-only
// The cooker (crates/nebula-import, D-01) as the runtime calls it for files the game did not have at import: a mod's
// replacement files are cooked when the mods are applied, as the disc's were when it was imported (D-04).
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy::cook {

// Whether the cooker reads a file of this name (archives, textures, models, layouts' textures, fonts, particles).
[[nodiscard]] bool cooks(std::string_view name);

// The cooked bytes of a file (`name` its disc path), nullopt when it stays as it is. Sets `*error` (and returns
// nullopt) when the cooker is missing or the file cannot be cooked.
[[nodiscard]] std::optional<std::vector<std::uint8_t>> cook_file(std::string_view name, std::span<const std::uint8_t> bytes,
                                                                 std::string* error);

}  // namespace galaxy::cook
