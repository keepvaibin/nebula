// SPDX-License-Identifier: GPL-3.0-only
// Dolphin texture packs, converted to the game's texture names. A Dolphin pack names a file by its source texture's
// contents (a data convention, implemented from its public description):
//   tex1_<w>x<h>[_m]_<hash>[_<palette hash>|_$]_<fmt>[_arb][_mip<n>].png|.dds
// <hash>: XXH64 of the source texture's base level (the names table's `image` column); <palette hash>: of the palette
// entries it uses ($: any palette); <w>x<h> and <fmt>: the source's size and format. Nebula names textures by what they
// are (galaxy/texture_categories.h); a Dolphin pack is converted once, when the mod is imported or first found, and the
// renderer never reads a tex1_ name.
#pragma once

#include "galaxy/texture_categories.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy::textures {

// The format codes of the names (the source texture's format).
enum class DolphinFormat : std::uint8_t {
    Gray4 = 0x0, Gray8 = 0x1, GrayAlpha4 = 0x2, GrayAlpha8 = 0x3, Rgb565 = 0x4, Packed16 = 0x5, Rgba8 = 0x6,
    Palette4 = 0x8, Palette8 = 0x9, Palette14 = 0xA, Block4 = 0xE,
};
[[nodiscard]] bool dolphin_format_palettized(std::uint8_t format) noexcept;

struct DolphinName {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint8_t format = 0;
    std::uint8_t level = 0;      // _mip<n>
    bool mipmapped = false;      // _m
    bool any_palette = false;    // _$
    bool arbitrary = false;      // _arb: levels with content of their own
    std::uint64_t hash = 0;
    std::uint64_t palette = 0;
};
// A file name's stem (no folder, no extension); nullopt when it is not a Dolphin texture name. A letter typed after the
// format number ("_14v", a slip in some packs) reads as the number.
[[nodiscard]] std::optional<DolphinName> parse_dolphin_name(std::string_view stem) noexcept;
// The stem of a name, as Dolphin writes it.
[[nodiscard]] std::string dolphin_name(const DolphinName& name);
// Whether a game texture is the one a Dolphin name stands for: its image, size, format and (for a palette format, unless
// any palette) palette.
[[nodiscard]] bool dolphin_matches(const DolphinName& name, const TextureName& texture) noexcept;

// The file a converted texture is linked as, inside the pack: "<name>[_arb][_mip<n>]<extension>" ("Kuribo/KuriboBody.png",
// "IconA.png"). LumaLink converts to the same files (lumalink-core texture_pack.rs).
[[nodiscard]] std::string converted_file(const TextureName& texture, bool arbitrary, std::uint8_t level,
                                         std::string_view extension);

// Whether `folder` holds a tex1_ file outside its .dolphin/ folder (a Dolphin pack not converted yet).
[[nodiscard]] bool has_dolphin_files(const std::filesystem::path& folder);

// Converts the Dolphin pack in `folder` to the game's names `names` (sorted by hash, as names() gives them). Each tex1_
// file moves to <folder>/.dolphin/ (keeping its subfolders) and is linked (a hard link, else a copy) as converted_file
// for every game texture it stands for, in the game folder that held it (<folder>/RMGE01, ...), else in `folder`. A
// file that stands for none stays in .dolphin/ and is listed in .dolphin/unmatched.txt. Folders left empty are removed.
struct DolphinConversion {
    std::uint64_t files = 0;      // tex1_ files converted
    std::uint64_t textures = 0;   // files linked under the game's names
    std::uint64_t taken = 0;      // names an earlier file of the pack already gave
    std::vector<std::string> unmatched;  // files standing for none of the game's textures (paths in the pack)
    std::vector<std::string> failed;     // files that could not be moved or linked
};
DolphinConversion convert_dolphin_pack(const std::filesystem::path& folder, std::span<const TextureName> names);

}  // namespace galaxy::textures
