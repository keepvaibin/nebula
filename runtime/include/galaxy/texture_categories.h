// SPDX-License-Identifier: GPL-3.0-only
// What each of the game's textures is: the category of the game file that holds it, so that a texture pack's textures
// can be sorted and turned on and off by kind. A pack names a texture by its hash; the index maps the hashes of this
// game's own textures to their categories. It is built once from the game's asset store on a thread of its own and kept
// beside the settings; a texture the game does not hold goes by the words of the pack's own folders.
#pragma once

#include <array>
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
[[nodiscard]] constexpr std::uint32_t category_bit(Category category) noexcept {
    return 1u << static_cast<unsigned>(category);
}

// The category of a game file by its path in the store ("ObjectData/Kuribo/Kuribo.bdl", "LayoutData/Icon/...").
[[nodiscard]] Category category_of_path(std::string_view path) noexcept;
// The category of an object's archive by its name, without its extension.
[[nodiscard]] Category category_of_object(std::string_view archive) noexcept;
// What the words of a path inside a pack say ("HD/Characters/Mario/tex1_..."): nullopt when they say nothing.
[[nodiscard]] std::optional<Category> category_of_pack_folder(std::string_view folder) noexcept;
// The texture hash a tex1_ name holds; nullopt when it is not one.
[[nodiscard]] std::optional<std::uint64_t> hash_of_pack_name(std::string_view stem) noexcept;

// Starts the index, once: read from `cache_file` when it was made from this store, else built from the store at
// `store_file` on its own thread and written there. Any thread.
void start_index(const std::filesystem::path& store_file, const std::filesystem::path& cache_file);
[[nodiscard]] bool index_ready() noexcept;
// Waits until the index is ready; false when none was started or it failed.
bool wait_for_index();
// The category of a hash in the game's own textures; nullopt when the game does not hold it (or no index is ready).
[[nodiscard]] std::optional<Category> category_of_hash(std::uint64_t hash) noexcept;
// A pack's texture: by the game's index, else the words of its folder, else Other.
[[nodiscard]] Category category_of_pack_texture(std::uint64_t hash, std::string_view folder) noexcept;

// For tests: the index as these (hash, category) pairs.
void set_index_for_tests(std::initializer_list<std::pair<std::uint64_t, Category>> entries);

}  // namespace galaxy::textures
