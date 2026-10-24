#pragma once

// Read-only extraction of the game's UI art from its dump, for styling the native settings overlay: Yaz0
// (Nintendo LZ), RARC archives, and the TPL texel formats the layout archives use (I4, IA4, RGB5A3).

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy::gx {

// Decompresses a buffer that starts with the "Yaz0" magic; empty when it is malformed or declares more than
// 64 MiB. Every copy is bounded by the declared size and the source.
[[nodiscard]] std::vector<std::byte> yaz0_decompress(
    std::span<const std::byte> data);

// A named file in a RARC archive.
struct RarcFile {
    std::string name;
    std::span<const std::byte> data;
};

// Every regular file of a decompressed RARC archive. The spans alias `data`, which must outlive the result.
[[nodiscard]] std::vector<RarcFile> rarc_list_files(
    std::span<const std::byte> data);

// The file of that exact (case-sensitive) name in a RARC archive, or std::nullopt.
[[nodiscard]] std::optional<std::span<const std::byte>> rarc_find_file(
    std::span<const std::byte> data, std::string_view name);

// A decoded TPL texture: RGBA8, top-left origin.
struct DecodedTexture {
    unsigned width{};
    unsigned height{};
    std::vector<std::uint8_t> rgba8;  // width*height*4 bytes
};

// Decodes the first image of a TPL in GX format 0 (I4), 2 (IA4) or 5 (RGB5A3); complete tiles are required even
// where the dimensions cut one short. std::nullopt for any other format, a malformed container or allocation failure.
[[nodiscard]] std::optional<DecodedTexture> decode_tpl(
    std::span<const std::byte> data) noexcept;

// Reads <content_root>/<arc_relative_path>, Yaz0-decompresses it if needed and decodes the named texture from the
// RARC. std::nullopt on any failure (missing file or entry, unsupported format, over 64 MiB raw or decompressed,
// allocation or I/O error); callers then draw solid colour.
[[nodiscard]] std::optional<DecodedTexture> load_game_texture(
    const std::string& content_root_utf8,
    std::string_view arc_relative_path,
    std::string_view texture_entry_name) noexcept;

}  // namespace galaxy::gx
