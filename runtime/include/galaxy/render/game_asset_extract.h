#pragma once

// The game's UI art for styling the native settings overlay, read from the cooked content (nebula-import): an archive's
// file, and a layout texture file's first texture record (nebula_cooked_texture.h).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy::render {

// A texture's first level: RGBA8, top-left origin.
struct DecodedTexture {
    unsigned width{};
    unsigned height{};
    std::vector<std::uint8_t> rgba8;  // width*height*4 bytes
};

// The first image of a cooked TPL (its texture record's first level); std::nullopt for a TPL that is not cooked or a
// malformed container.
[[nodiscard]] std::optional<DecodedTexture> tpl_texture(
    std::span<const std::byte> data) noexcept;

// The named texture below the folder `folder` ("LayoutData/FileSelect.arc") of <content_root>/game.pak. std::nullopt on any
// failure; callers then draw without it.
[[nodiscard]] std::optional<DecodedTexture> load_game_texture(
    const std::string& content_root_utf8,
    std::string_view folder,
    std::string_view texture_entry_name) noexcept;

}  // namespace galaxy::render
