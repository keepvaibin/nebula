#pragma once

// Read-only extraction of the game's own UI assets (Yaz0-compressed RARC
// archives containing BRLYT layouts + TPL textures) so the native settings
// overlay can be styled using Super Mario Galaxy's own art instead of solid
// placeholder rectangles. This never executes or hooks any guest code --
// it only parses static asset files already present in the user's own game
// dump, exactly the class of asset access build-pak/DVDLowRead already do
// elsewhere in this runtime. Formats implemented: Yaz0 (Nintendo LZ),
// Nintendo RARC (archive directory), and the TPL texel formats actually
// used by the layout archives this runtime reads (I4, IA4, RGB5A3) --
// intentionally not a full GX texture format decoder.

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

// Decodes the first image in a TPL texture container. Supports the GX
// texel formats 0 (I4), 2 (IA4), and 5 (RGB5A3) -- the formats actually
// used by the layout archives this runtime reads today. Returns
// std::nullopt for any other format or a malformed container, rather than
// guessing at an unverified decode (hard-fail-never-stub for asset data).
// Complete tiled storage is required even when dimensions cut a tile short.
// Allocation failure also returns std::nullopt.
[[nodiscard]] std::optional<DecodedTexture> decode_tpl(
    std::span<const std::byte> data) noexcept;

// Convenience: read `<content_root>/<relative_arc_path>` from disk, Yaz0
// decompress if needed, and decode one named texture entry from the parsed
// RARC. Returns std::nullopt on any failure (missing file, format not
// supported, entry not found) -- callers must treat a missing/unsupported
// asset as "fall back to the existing solid-color rendering", never as a
// fatal error, since this path only affects cosmetic overlay styling.
// Raw archives and Yaz0 output are each limited to 64 MiB; oversized files
// and allocation/I/O exceptions use the same missing-asset fallback.
[[nodiscard]] std::optional<DecodedTexture> load_game_texture(
    const std::string& content_root_utf8,
    std::string_view arc_relative_path,
    std::string_view texture_entry_name) noexcept;

}  // namespace galaxy::gx
