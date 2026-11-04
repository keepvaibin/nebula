// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// A .luma's luma.json as the game reads it. The format is in LumaLink's docs/format.md: a reader ignores keys it doesn't know,
// and refuses by name anything it can't honour.

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy::luma {

// A piece of text for players: a default and its translations, by language ("ja", "fr-CA").
struct Text {
    std::string fallback;
    std::map<std::string, std::string> languages;
    // In a language: the language, then the language without its region, then the default.
    [[nodiscard]] const std::string& in(std::string_view language) const;
};

struct Choice {
    std::string id;
    Text name;
};

struct Option {
    std::string id;
    Text name;
    Text description;
    std::string fallback;  // the choice it starts on
    std::vector<Choice> choices;
};

struct When {
    std::vector<std::string> versions;   // E, P, J, K, W, C
    std::vector<std::string> languages;  // the game's folders: UsEnglish
    std::vector<std::string> platforms;  // windows, android, linux, macos, ios
    std::map<std::string, std::vector<std::string>> options;
    bool unknown = false;  // has a condition this game doesn't know: the layer is not used
};

struct Layer {
    std::string path;
    std::map<std::string, std::uint32_t> switches;
    std::map<std::string, std::string> materials;  // a material's name: the shader written for it
    When when;
};

struct Dependency {
    std::string id;
    std::string version;  // needs: the oldest that will do; conflicts: only those below this
};

// A mod's code: one native library per device.
struct Plugin {
    std::map<std::string, std::string> binaries;  // by plugin_target()
    std::uint32_t api = 0;
    When when;
};

// A galaxy beside the game's, and the hungry Luma that opens it.
struct Galaxy {
    std::string name;
    std::string map_pane;
    std::vector<std::string> open_conditions;
    int stars_to_open = 0;
    int dome = 0;
    int hungry_slot = -1;
    int hungry_star_bits = 0;
    bool has_map_position = false;
    float map_x = 0.0f;
    float map_y = 0.0f;
};

// A track beside the game's, and where it plays.
struct MusicPlace {
    std::string stage;
    int scenario = 0;  // a mission; 0: all it has no row for
    int change_area = -1;  // when not -1: what a ChangeBgmCube with this Obj_arg1 starts, not the stage's own
};
struct Music {
    std::string name;
    std::string file;  // its place in the game
    std::vector<MusicPlace> places;
};

// A row of the game's progression, by its name.
struct EventFlag {
    std::string name;
    int kind = 0;
    int a = 0;
    int b = 0;
    std::string first;
    std::string second;
};

// A dome's comets, in the order they come.
struct CometSchedule {
    int dome = 0;
    std::vector<std::pair<std::string, int>> comets;  // a galaxy, and 1 for the hundred purple coins
};

enum class SaveMode {
    Shared,  // the player's normal saves
    Own,     // saves of its own, from nothing or from its starting files
    Copy,    // saves of its own, from a copy of the player's
};

struct Manifest {
    std::uint32_t format = 0;
    std::string id;
    Text name;
    std::string version;
    std::string game;
    std::vector<std::string> versions;
    std::string nebula;
    std::vector<std::string> requires_features;
    std::vector<std::string> authors;
    Text description;
    std::string license;
    std::string homepage;
    std::string icon;
    std::vector<Dependency> needs;
    std::vector<Dependency> conflicts;
    std::vector<std::string> load_after;
    std::vector<std::string> load_before;
    std::vector<Option> options;
    std::vector<Layer> layers;
    SaveMode save_mode = SaveMode::Shared;
    std::string save_tag;   // the family of saves; the mod's id when it names none
    std::string save_path;  // starting files in the mod
    bool save_player_can_change = false;
    std::vector<Plugin> plugins;
    std::vector<Galaxy> galaxies;
    std::vector<Music> music;
    std::vector<EventFlag> event_flags;
    std::vector<CometSchedule> comet_schedule;
    std::map<std::string, Text> text;  // a message's id: its words
};

// What this game reads.
inline constexpr std::uint32_t kFormat = 1;
// The plugin interface this game has: NEBULA_MOD_API_VERSION.
inline constexpr std::uint32_t kPluginApi = 1;

[[nodiscard]] std::optional<Manifest> parse_manifest(std::string_view text);
[[nodiscard]] std::optional<Manifest> read_manifest(const std::filesystem::path& folder);

// Numbers with dots, number by number: 2.10 is above 2.9, and 2.1 equals 2.1.0.
[[nodiscard]] int compare_versions(std::string_view a, std::string_view b);

// What the mod can't do without, for this game: the line for the Mods page, or empty when it can be used.
[[nodiscard]] std::string refusal(const Manifest& manifest, const std::string& game_id, std::string_view nebula_version,
                                  std::string_view platform);

// Whether a layer is used.
struct Situation {
    std::string version;   // the game's version letter
    std::string language;  // the folder of the language played
    std::string platform;
    std::map<std::string, std::string> options;  // each option's choice
};
[[nodiscard]] bool layer_used(const Layer& layer, const Situation& situation);
[[nodiscard]] bool when_holds(const When& when, const Situation& situation);
// This device, as a plugin's `binaries` key: windows-x64, windows-arm64, linux-x64, linux-arm64, macos-x64, macos-arm64,
// android-arm64, android-x64.
[[nodiscard]] std::string_view plugin_target() noexcept;

// The language code of a game language folder, for a Text.
[[nodiscard]] std::string language_code(std::string_view folder);

// The device this game is built for, as luma.json names it.
[[nodiscard]] std::string_view this_platform() noexcept;

// A switch's number (NEBULA_PATCH_*) by its name; 0: none of that name.
[[nodiscard]] std::uint32_t switch_number(std::string_view name) noexcept;

}  // namespace galaxy::luma
