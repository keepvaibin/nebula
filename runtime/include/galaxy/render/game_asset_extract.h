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

// A named file in a RARC archive.
struct RarcFile {
    std::string name;
    std::span<const std::byte> data;
};

// Every regular file of a RARC archive. The spans alias `data`, which must outlive the result.
[[nodiscard]] std::vector<RarcFile> rarc_list_files(
    std::span<const std::byte> data);

// The file of that exact (case-sensitive) name in a RARC archive, or std::nullopt.
[[nodiscard]] std::optional<std::span<const std::byte>> rarc_find_file(
    std::span<const std::byte> data, std::string_view name);

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

// Reads <content_root>/<arc_relative_path> and the named texture from the RARC. std::nullopt on any failure (missing
// file or entry, content packed before cooking, over 64 MiB, allocation or I/O error); callers then draw solid colour.
[[nodiscard]] std::optional<DecodedTexture> load_game_texture(
    const std::string& content_root_utf8,
    std::string_view arc_relative_path,
    std::string_view texture_entry_name) noexcept;

}  // namespace galaxy::render
