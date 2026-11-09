// SPDX-License-Identifier: GPL-3.0-only
// The game's textures by name, and what each is: the category of the game file that holds it, so that a texture pack's
// textures can be sorted and turned on and off by kind. A texture's name is "<model stem>/<texture>" or "<file stem>"
// (nebula-import texture::name_record); the store lists every one in NebulaData/TextureNames.tsv, which the index reads
// once, on a thread of its own. A texture the game does not hold goes by the words of the pack's own folders.
#pragma once

#include <array>
#include <span>
#include <string>
#include <vector>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>

namespace galaxy::textures {

enum class Category : std::uint8_t {
    MarioLuigi,
    Characters,
    Enemies,
    Bosses,
    Items,
    Planets,
    Objects,
    Skies,
    Particles,
    Hud,
    Menus,
    GalaxyMap,
    Icons,
    Fonts,
    Story,
    Other,
    Count,
};
inline constexpr std::size_t kCategoryCount = static_cast<std::size_t>(Category::Count);
using CategoryCounts = std::array<unsigned, kCategoryCount>;

// A category's name in the settings (upper case; its entry in the settings' text table).
[[nodiscard]] const char* category_name(Category category) noexcept;
// The folder of a category's textures in dumps and converted packs ("Enemies"); its words say the category.
[[nodiscard]] const char* category_folder(Category category) noexcept;
[[nodiscard]] constexpr std::uint32_t category_bit(Category category) noexcept {
    return 1u << static_cast<unsigned>(category);
}

// The category of a game file by its path in the store ("ObjectData/Kuribo/Kuribo.bdl", "LayoutData/Icon/...").
[[nodiscard]] Category category_of_path(std::string_view path) noexcept;
// The category of an object's archive by its name, without its extension.
[[nodiscard]] Category category_of_object(std::string_view archive) noexcept;
// What the words of a path inside a pack say ("HD/Characters/Mario/tex1_..."): nullopt when they say nothing.
[[nodiscard]] std::optional<Category> category_of_pack_folder(std::string_view folder) noexcept;

// A name's key, as a record's name_hash holds it: FNV-1a 64 of its bytes in ASCII lower case.
[[nodiscard]] std::uint64_t name_hash(std::string_view name) noexcept;

// One row of the store's names table. image: the same for every texture of one source image; palette, width, height,
// format: the source's, as a Dolphin pack's file names it (dolphin_pack.h).
struct TextureName {
    std::uint64_t hash = 0;
    std::uint64_t image = 0;
    std::uint64_t palette = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint8_t format = 0;
    Category category = Category::Other;
    std::string name;
};
// The table's text as rows, sorted by hash; rows it can't read are left out.
[[nodiscard]] std::vector<TextureName> parse_names(std::string_view text);

// Starts the index, once: the names table of the store at `store_file`, read on its own thread. Any thread.
void start_index(const std::filesystem::path& store_file);
[[nodiscard]] bool index_ready() noexcept;
// Waits until the index is ready; false when none was started or it failed.
bool wait_for_index();
// Every name, sorted by hash; empty until the index is ready. It lives as long as the program.
[[nodiscard]] std::span<const TextureName> names() noexcept;
// The game's texture of this name hash; null when the game has none (or no index is ready).
[[nodiscard]] const TextureName* find_name(std::uint64_t hash) noexcept;
// A pack's file by its folder inside the pack and its stem: "<folder>/<texture>.png" names a model's texture
// ("kuribo/KuriboBody") and "<texture>.png" a texture file's, under any folders of the pack's own above them. `keys`:
// the name with its last folder, then alone, hashed; `level` from a "_mip<n>" ending, `arbitrary` from "_arb" (levels with
// content of their own). nullopt: no name (a Dolphin pack's tex1_ file, which mod import converts).
struct PackFileName {
    std::array<std::uint64_t, 2> keys{};
    std::uint8_t level = 0;
    bool arbitrary = false;
};
[[nodiscard]] std::optional<PackFileName> parse_pack_file(std::string_view folder, std::string_view stem) noexcept;
[[nodiscard]] bool is_dolphin_pack_name(std::string_view stem) noexcept;
// Which of a file's keys names its texture: the one the game has (the longer first), else the first.
[[nodiscard]] std::uint64_t pack_file_key(const PackFileName& name) noexcept;

// A pack's texture: by the game's name, else the words of its folder, else Other.
[[nodiscard]] Category category_of_pack_texture(std::uint64_t hash, std::string_view folder) noexcept;

// For tests: the index as these rows.
void set_index_for_tests(std::vector<TextureName> rows);

}  // namespace galaxy::textures
