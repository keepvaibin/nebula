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

// An achievement of the mod's own: on the achievement board's Mods tab, kept with Nebula's in achievements.ini as
// "mod:<mod id>/<id>". A plugin unlocks it (unlock_achievement), or the game does when the event `when` names comes.
struct Achievement {
    std::string id;
    Text name;
    Text description;  // what earns it: shown on its card
    bool hidden = false;  // listed only once unlocked
    std::string event;       // "star_get" (NEBULA_MOD_EVENT_STAR_GET, lower case); empty: only a plugin unlocks it
    std::string event_name;  // the event's name must be this (a galaxy, a cutscene, an object); empty: any
    bool has_value = false;
    std::int64_t event_value = 0;  // ... and its value this
};

struct When {
    std::vector<std::string> versions;   // E, P, J, K, W, C
    std::vector<std::string> languages;  // the game's folders: UsEnglish
    std::vector<std::string> platforms;  // windows, android, linux, macos, ios
    std::map<std::string, std::vector<std::string>> options;
    std::vector<std::string> mods;  // each of these mods is on
    std::vector<When> not_any;  // none of these holds
    std::vector<When> any_of;  // one of these holds, if there are any
    bool unknown = false;  // has a condition this game doesn't know: the layer is not used
};

// One of the game's objects as another, or gone.
struct ObjectChange {
    int where = 0;  // NEBULA_OBJECTS_*
    std::string object;
    std::string becomes;  // empty: gone
};

// A comet mission's time limit.
struct CometLimit {
    std::string galaxy;
    int mission = 0;
    int seconds = 0;
};

// A stage playing by another's rules.
struct StageRule {
    std::string stage;
    int scenario = 0;
    std::vector<std::string> acts_as;
    bool own_rules = true;
};

// A number of the player's table.
struct PlayerConstant {
    int character = 0;  // 0 Mario, 1 Luigi
    std::string name;
    int index = 0;
    float value = 0.0f;
    std::string option;  // when set: the number is this option's choice
};

// A file's tuning number by "<file stem>.<name>" ("GameRules.cPlayerHealth").
struct Tunable {
    std::string key;
    double value = 0.0;
    std::string option;  // when set: the number is this option's choice
};

// A new object, made as one of the game's.
struct ObjectClass {
    std::string name;
    std::string like;
};

// A sound by its name, from a file.
struct SoundEffect {
    std::string name;
    std::string file;
};

// A player animation: `name` plays the player's bck `file` (ObjectData/MarioAnime.arc) at `rate`, blended in over
// `blend_frames` (0: the game's); a name the game has is played from it.
struct PlayerAnimation {
    std::string name;
    std::string file;
    double rate = 1.0;
    int blend_frames = 0;
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

// What a whole mod, or one of its layers, adds beside files. A new kind here is one rows_set_game counts.
struct Rows {
    std::vector<Galaxy> galaxies;
    std::vector<Music> music;
    std::vector<EventFlag> event_flags;
    std::vector<CometSchedule> comet_schedule;
    std::map<std::string, Text> text;  // a message's id: its words
    // A new message's id: the game's message whose talk it takes (camera, sound, talk and balloon type): `"like"` in its
    // words' object.
    std::map<std::string, std::string> text_like;
    std::map<std::string, std::string> story;  // first_stage, first_mission, hub, story_scenes
};

// A new kind here is one layer_sets_game counts.
struct Layer {
    std::string path;
    Rows rows;
    std::map<std::string, std::string> materials;  // a material's name: the shader written for it
    std::vector<ObjectChange> object_changes;
    std::vector<CometLimit> comet_time_limits;
    std::vector<StageRule> stages;
    std::vector<PlayerConstant> player_constants;
    std::vector<Tunable> tunables;
    std::vector<ObjectClass> object_classes;
    std::vector<SoundEffect> sound_effects;
    std::vector<PlayerAnimation> player_animations;
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

enum class SaveMode {
    Shared,  // the player's normal saves
    Own,     // saves of its own, from nothing or from its starting files
    Copy,    // saves of its own, from a copy of the player's
};

struct Manifest : Rows {
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
    std::vector<std::string> pictures;  // more pictures of it (PNG files in it), shown after the icon; kMaxPictures
    std::vector<Achievement> achievements;  // kMaxAchievements, each id once
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
    // A form this game no longer reads (a layer's "switches"): what to write instead. The mod is refused with it.
    std::string retired;
};

// What this game reads.
inline constexpr std::uint32_t kFormat = 1;
// The pictures a mod's page shows besides its icon: the first ones listed.
inline constexpr std::size_t kMaxPictures = 16;
// The achievements one mod adds: the first ones listed.
inline constexpr std::size_t kMaxAchievements = 64;
// The plugin interface this game has: NEBULA_MOD_API_VERSION.
inline constexpr std::uint32_t kPluginApi = 2;

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
    std::vector<std::string> mods;  // the mods on
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

// The tunable that took the place of a switch ("player_health": "GameRules.cPlayerHealth"), and whether its sense is
// the switch's opposite ("no_low_health_warning" 1 is "MarioEffect.cLowHealthWarning" false). Empty: no such switch.
struct SwitchTunable {
    std::string_view key;
    bool opposite = false;
};
[[nodiscard]] SwitchTunable switch_tunable(std::string_view name) noexcept;

// Whether rows, a layer (besides its folder's files) or a whole mod (besides its layers) set anything the game takes as
// it starts: a mod that does is in the plan, and turning it on or off asks for a restart. A mod's own: its rows, its
// achievements, its own saves, and code for this device.
[[nodiscard]] bool rows_set_game(const Rows& rows) noexcept;
[[nodiscard]] bool layer_sets_game(const Layer& layer) noexcept;
[[nodiscard]] bool manifest_sets_game(const Manifest& manifest) noexcept;

// What a mod sets that another mod could set too, one key per thing, fields apart by tabs: "tunable\tGameRules.cPlayerHealth",
// "line\t<message id>\t<language folder>", "cell\t<table>\t<row>\t<column>". Two mods with a key in common change the same
// thing; the one higher in the list wins it. Rows that add something new (a table's new rows, particles) have none.
[[nodiscard]] std::vector<std::string> setting_keys(const Rows& rows);
[[nodiscard]] std::vector<std::string> setting_keys(const Layer& layer);
// The same for a file of a layer's merge/ folder at `path` (as under merge/, folders apart by "/"): a table's cells a
// "change" sets, the rows a "remove" takes ("row\t<table>\t<row>"), the lines and flows of a messages merge. A row is its
// "where" columns. A file that does not read as JSON has none.
[[nodiscard]] std::vector<std::string> merge_keys(std::string_view path, std::string_view json);

}  // namespace galaxy::luma
