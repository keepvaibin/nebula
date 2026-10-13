#pragma once

// Read-only extraction of the game's UI assets (Yaz0-compressed RARC archives with BRLYT layouts and
// TPL textures). The native settings overlay uses them so it can be styled with the game's own art
// instead of solid placeholder rectangles. This never executes or hooks guest code. It only parses
// static asset files already present in the user's own game dump, the same class of access that
// build-pak and DVDLowRead already perform. Implemented formats: Yaz0 (Nintendo LZ), Nintendo RARC
// (archive directory), and the TPL texel formats used by this runtime's layout archives (I4, IA4,
// RGB5A3). This is not a full GX texture decoder.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy::gx {

// Decompresses a Yaz0-compressed buffer. `data` must begin with the "Yaz0"
// magic. Returns an empty vector if the buffer is malformed (never reads or
// writes out of bounds -- every copy is clamped against the declared
// decompressed size and the recorded source cursor).
[[nodiscard]] std::vector<std::byte> yaz0_decompress(
    std::span<const std::byte> data);

// A single named file extracted from a parsed RARC archive.
struct RarcFile {
    std::string name;
    std::span<const std::byte> data;
};

// Parses a (already Yaz0-decompressed) RARC archive and returns every
// regular file it contains (directories excluded). The returned spans
// alias into `data`, which must outlive the result.
[[nodiscard]] std::vector<RarcFile> rarc_list_files(
    std::span<const std::byte> data);

// Finds one named file within a parsed RARC archive (case-sensitive, exact
// match against the stored entry name). Returns std::nullopt if absent.
[[nodiscard]] std::optional<std::span<const std::byte>> rarc_find_file(
    std::span<const std::byte> data, std::string_view name);

// A decoded TPL texture, always normalized to top-left-origin RGBA8.
struct DecodedTexture {
    unsigned width{};
    unsigned height{};
    std::vector<std::uint8_t> rgba8;  // width*height*4 bytes
};

// Decodes the first image in a TPL texture container. Supports the GX texel formats 0 (I4), 2 (IA4) and
// 5 (RGB5A3), the formats used by the layout archives this runtime reads. Returns std::nullopt for any
// other format or a malformed container, rather than guessing at an unverified decode. Complete tiled
// storage is required even when the dimensions cut a tile short. Allocation failure also returns std::nullopt.
[[nodiscard]] std::optional<DecodedTexture> decode_tpl(
    std::span<const std::byte> data) noexcept;

// Reads `<content_root>/<relative_arc_path>` from disk, Yaz0-decompresses it if needed, and decodes one
// named texture from the parsed RARC. Returns std::nullopt on any failure (missing file, unsupported
// format, entry not found). Callers treat a missing or unsupported asset as the solid-color fallback,
// not a fatal error, since this only affects overlay styling. Raw archives and Yaz0 output are each
// limited to 64 MiB. Oversized files and allocation or I/O exceptions take the same fallback.
[[nodiscard]] std::optional<DecodedTexture> load_game_texture(
    const std::string& content_root_utf8,
    std::string_view arc_relative_path,
    std::string_view texture_entry_name) noexcept;

}  // namespace galaxy::gx
