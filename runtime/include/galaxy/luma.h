// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Reads a .luma's luma.json. The format is described in LumaLink's docs/format.md. Unknown keys are ignored; anything
// that can't be honoured refuses the mod with a reason.

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy::luma {

// Player-facing text: a default plus translations keyed by language ("ja", "fr-CA").
struct Text {
    std::string fallback;
    std::map<std::string, std::string> languages;
    // The text for a language: exact match, then the language without its region, then the default.
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

// A mod's own achievement. It appears on the Mods tab of the achievement board and is saved in achievements.ini as
// "mod:<mod id>/<id>". A plugin unlocks it (unlock_achievement), or the game does when `event` happens.
struct Achievement {
    std::string id;
    Text name;
    Text description;  // what earns it, shown on its card
    bool hidden = false;  // listed only once unlocked
    std::string event;       // "star_get" (NEBULA_MOD_EVENT_STAR_GET, lower case); empty: only a plugin unlocks it
    std::string event_name;  // required event name (a galaxy, a cutscene, an object); empty: any
    bool has_value = false;
    std::int64_t event_value = 0;  // required event value when has_value
};

struct When {
    std::vector<std::string> versions;   // E, P, J, K, W, C
    std::vector<std::string> languages;  // the game's folders: UsEnglish
    std::vector<std::string> platforms;  // windows, android, linux, macos, ios
    std::map<std::string, std::vector<std::string>> options;
    std::vector<std::string> mods;  // all of these mods are on
    std::vector<When> not_any;  // none of these hold
    std::vector<When> any_of;  // at least one holds (when not empty)
    bool unknown = false;  // has a condition this game doesn't know, so the layer is not used
};

// Replaces one of the game's objects with another, or removes it.
struct ObjectChange {
    int where = 0;  // NEBULA_OBJECTS_*
    std::string object;
    std::string becomes;  // empty: removed
};

// A comet mission's time limit.
struct CometLimit {
    std::string galaxy;
    int mission = 0;
    int seconds = 0;
};

// Makes a stage use another stage's rules.
struct StageRule {
    std::string stage;
    int scenario = 0;
    std::vector<std::string> acts_as;
    bool own_rules = true;
};

// One value in the player's constants table.
struct PlayerConstant {
    int character = 0;  // 0 Mario, 1 Luigi
    std::string name;
    int index = 0;
    float value = 0.0f;
    std::string option;  // when set, the value comes from this option's choice
};

// A tuning value, keyed "<file stem>.<name>" ("GameRules.cPlayerHealth").
struct Tunable {
    std::string key;
    double value = 0.0;
    std::string option;  // when set, the value comes from this option's choice
};

// A new object class that is built like one of the game's.
struct ObjectClass {
    std::string name;
    std::string like;    // the game's class it is made like; empty: `name` is one of the game's own classes
    std::string script;  // what runs on every actor of it (scripts/<script>.lumascript); empty: none
};

// A power-up from data ("forms"): the game's form it acts like and looks like (NEBULA_POWER_UP_*), its colour frame,
// whole-body model, time, music and script on the player.
struct Form {
    std::string name;
    int acts_like = 0;
    int looks_like = 0;
    int color_frame = 0;
    int frames = 0;
    std::string model;   // ObjectData/<model>.arc; empty: the player's own
    std::string music;   // a BGM_ name; empty: the acts_like form's
    std::string script;  // empty: none
};

// An object a script runs ("objects"): its shape, and the script (scripts/<script>.lumascript, compiled by LumaLink
// into files/LumaScripts/<mod id>/<script>.lumascript).
struct ScriptObject {
    std::string name;
    std::string script;
    std::string archive;  // model and animations: ObjectData/<archive>.arc; empty: invisible
    double radius = 60.0;
    int kind = 0;  // NEBULA_OBJECT_*: "enemy", "map", "npc", "ride", "decoration"
    bool collision = false;
    double shadow_radius = 0.0;
    double clip_radius = 0.0;
};

// A named sound effect loaded from a file.
struct SoundEffect {
    std::string name;
    std::string file;
};

// A player animation: `name` plays the bck `file` (ObjectData/MarioAnime.arc) at `rate`, blended in over
// `blend_frames` (0 uses the game's blend). A name the game already has replaces that animation.
struct PlayerAnimation {
    std::string name;
    std::string file;
    double rate = 1.0;
    int blend_frames = 0;
};

// A new galaxy, and the hungry Luma that opens it.
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

// A new music track and where it plays.
struct MusicPlace {
    std::string stage;
    int scenario = 0;  // a mission; 0 means every mission without its own entry
    int change_area = -1;  // when not -1, plays in a ChangeBgmCube with this Obj_arg1 instead of as the stage's music
};
struct Music {
    std::string name;
    std::string file;  // path in the game
    std::vector<MusicPlace> places;
};

// A named row of the game's progression table.
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
    std::vector<std::pair<std::string, int>> comets;  // galaxy, and 1 for the purple coin comet
};

// What a mod or layer adds besides files. Any new field here must also be counted in rows_set_game.
struct Rows {
    std::vector<Galaxy> galaxies;
    std::vector<Music> music;
    std::vector<EventFlag> event_flags;
    std::vector<CometSchedule> comet_schedule;
    std::map<std::string, Text> text;  // message id -> text
    // New message id -> an existing message whose camera, sound, talk and balloon type it copies (`"like"` in the
    // message's JSON).
    std::map<std::string, std::string> text_like;
    std::map<std::string, std::string> story;  // first_stage, first_mission, hub, story_scenes
};

// Any new field here must also be counted in layer_sets_game.
struct Layer {
    std::string path;
    Rows rows;
    std::map<std::string, std::string> materials;  // material name -> its shader
    std::vector<ObjectChange> object_changes;
    std::vector<CometLimit> comet_time_limits;
    std::vector<StageRule> stages;
    std::vector<PlayerConstant> player_constants;
    std::vector<Tunable> tunables;
    std::vector<ObjectClass> object_classes;
    std::vector<Form> forms;
    std::vector<ScriptObject> objects;
    std::vector<std::string> scripts;  // mod-wide scripts ("scripts"): scripts/<name>.lumascript, one instance each
    std::vector<SoundEffect> sound_effects;
    std::vector<PlayerAnimation> player_animations;
    When when;
};

struct Dependency {
    std::string id;
    std::string version;  // needs: the oldest accepted; conflicts: only versions below this
};

// A mod's code: one native library per platform.
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
    // "regions": the discs it is made for ("US", "PAL", "JP", "KR"); empty for "any" (region-free) or none given.
    std::vector<std::string> regions;
    std::string nebula;
    std::vector<std::string> requires_features;
    std::vector<std::string> authors;
    Text description;
    std::string license;
    std::string homepage;
    std::string icon;
    std::vector<std::string> pictures;  // extra PNG pictures shown after the icon (up to kMaxPictures)
    std::vector<Achievement> achievements;  // up to kMaxAchievements, unique ids
    std::vector<Dependency> needs;
    std::vector<Dependency> conflicts;
    std::vector<std::string> load_after;
    std::vector<std::string> load_before;
    std::vector<Option> options;
    std::vector<Layer> layers;
    SaveMode save_mode = SaveMode::Shared;
    std::string save_tag;   // mods with the same tag share saves; defaults to the mod id
    std::string save_path;  // folder in the mod with the starting save files
    bool save_player_can_change = false;
    std::vector<Plugin> plugins;
    // Set when the manifest uses a retired key (a layer's "switches"): what to write instead. The mod is refused.
    std::string retired;
};

// The luma.json format version this game reads.
inline constexpr std::uint32_t kFormat = 1;
// Most pictures a mod page shows besides the icon; extra ones are ignored.
inline constexpr std::size_t kMaxPictures = 16;
// Most achievements one mod can add; extra ones are ignored.
inline constexpr std::size_t kMaxAchievements = 64;
// The plugin API version this game provides (NEBULA_MOD_API_VERSION).
inline constexpr std::uint32_t kPluginApi = 3;
// The oldest plugin API this game runs: each version only adds calls at the end of the table.
inline constexpr std::uint32_t kOldestPluginApi = 2;

[[nodiscard]] std::optional<Manifest> parse_manifest(std::string_view text);
[[nodiscard]] std::optional<Manifest> read_manifest(const std::filesystem::path& folder);

// Compares dotted versions part by part: 2.10 > 2.9, and 2.1 == 2.1.0.
[[nodiscard]] int compare_versions(std::string_view a, std::string_view b);

// Why the mod can't be used with this game, as shown on the Mods page; empty when it can be used.
[[nodiscard]] std::string refusal(const Manifest& manifest, const std::string& game_id, std::string_view nebula_version,
                                  std::string_view platform);

// For the log, beside a refusal: the file, the field and what this game expects, when the refusal comes from one field.
[[nodiscard]] std::string refusal_detail(const Manifest& manifest);

// The current game, used to decide whether a layer applies.
struct Situation {
    std::string version;   // the game's version letter
    std::string language;  // the folder of the language played
    std::string platform;
    std::map<std::string, std::string> options;  // option id -> choice id
    std::vector<std::string> mods;  // ids of the mods that are on
};
[[nodiscard]] bool layer_used(const Layer& layer, const Situation& situation);
[[nodiscard]] bool when_holds(const When& when, const Situation& situation);
// This platform as a plugin `binaries` key: windows-x64, windows-arm64, linux-x64, linux-arm64, macos-x64, macos-arm64,
// android-arm64, android-x64.
[[nodiscard]] std::string_view plugin_target() noexcept;

// The language code (for Text) of a game language folder.
[[nodiscard]] std::string language_code(std::string_view folder);

// The platform this build is for, as luma.json names it.
[[nodiscard]] std::string_view this_platform() noexcept;

// The tunable that replaced a retired switch ("player_health" -> "GameRules.cPlayerHealth"), and whether its meaning is
// inverted ("no_low_health_warning" 1 is "MarioEffect.cLowHealthWarning" false). Empty key: no such switch.
struct SwitchTunable {
    std::string_view key;
    bool opposite = false;
};
[[nodiscard]] SwitchTunable switch_tunable(std::string_view name) noexcept;

// True when rows, a layer (apart from its files) or a whole mod (apart from its layers) set something the game reads at
// start. Such a mod goes into the plan, and turning it on or off needs a restart. For a whole mod this covers its rows,
// achievements, own saves, and code for this platform.
[[nodiscard]] bool rows_set_game(const Rows& rows) noexcept;
[[nodiscard]] bool layer_sets_game(const Layer& layer) noexcept;
[[nodiscard]] bool manifest_sets_game(const Manifest& manifest) noexcept;

// One key per setting a mod changes, with tab-separated fields: "tunable\tGameRules.cPlayerHealth",
// "line\t<message id>\t<language folder>", "cell\t<table>\t<row>\t<column>". Two mods sharing a key change the same
// thing, and the higher one wins. Additions (new table rows, particles) have no key.
[[nodiscard]] std::vector<std::string> setting_keys(const Rows& rows);
[[nodiscard]] std::vector<std::string> setting_keys(const Layer& layer);
// The same for a file in a layer's merge/ folder (`path` relative to merge/, using "/"): cells set by a "change", rows
// taken by a "remove" ("row\t<table>\t<row>"), and the lines and flows of a messages merge. A row is identified by its
// "where" columns. Invalid JSON gives no keys.
[[nodiscard]] std::vector<std::string> merge_keys(std::string_view path, std::string_view json);

}  // namespace galaxy::luma
