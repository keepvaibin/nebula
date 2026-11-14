// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// Reads a .luma's luma.json in the one form (luma-format; the validator, crates/luma-validate rules/manifest.rs, is the
// rule). The validator decides what refuses a mod (its report's errors); this reads what the game uses. The previous
// engine's rows are read only under the top-level key "pre_standard", which luma-format does not define, each until its
// general replacement is proven (agents/luma/board/OLD-ROWS.md).

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

// A `key` option (luma-format revision 5): one of the mod's input actions, following the game control `fallback`
// (input_bindings.cpp kTargetIds, lower case) until the player binds it in Controls.
struct KeyOption {
    std::string id;
    Text name;
    std::string fallback;
};

struct Option {
    std::string id;
    Text name;
    Text description;
    std::string kind = "choice";  // luma-format: choice, number, toggle, text or key
    // choice, toggle: the choice it starts on; number: its default as number_text writes it; text: the default text;
    // key: the default control's id (input_bindings kTargetIds).
    std::string fallback;
    std::vector<Choice> choices;  // choice and toggle only
    std::uint32_t max_length = 0;  // text: at most this many code points (1 to 256)
    // number: a range, never a list. Its values are min + k * step for whole k from 0 while not above max.
    double min = 0.0;
    double max = 0.0;
    double step = 1.0;
};

// A number as the Mods page shows it and a mod reads it: the shortest decimal that reads back as the same double.
[[nodiscard]] std::string number_text(double value);
// A number option's value from its text (number_text's, or typed): the value as number_text writes it when the text is
// a whole decimal number within the option's range and on one of its steps; nothing otherwise.
[[nodiscard]] std::optional<std::string> number_value(const Option& option, std::string_view text);
// A number option's value `steps` steps from `text` (its default when `text` is not one of its values), kept within the
// range.
[[nodiscard]] std::string number_stepped(const Option& option, std::string_view text, double steps);

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

// A layer's `when` (luma-format): every condition given holds. versions, mods, not_any and any_of come only from
// pre_standard (OLD-ROWS.md); a pre_standard `when` is one any_of entry, so it is and-ed with the form's.
struct When {
    std::vector<std::string> regions;    // the discs: RMGE01, RMGP01, RMGJ01
    std::vector<std::string> versions;   // pre_standard: the disc's letter E, P, J, K
    std::vector<std::string> languages;  // the game's folders: UsEnglish
    std::vector<std::string> platforms;  // windows-x64...; an operating system's name alone (pre_standard) is each of its
    std::map<std::string, std::vector<std::string>> options;  // a toggle's values are "on" and "off"
    bool code = false;  // only while the mod's code runs here
    std::vector<std::string> mods;  // pre_standard: all of these mods are on
    std::vector<When> not_any;  // pre_standard: none of these hold
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
    std::string like;  // the game's class it is made like
};

// A power-up from data ("forms"): the game's form it acts like and looks like (NEBULA_POWER_UP_*), its colour frame,
// whole-body model, time and music on the player.
struct Form {
    std::string name;
    int acts_like = 0;
    int looks_like = 0;
    int color_frame = 0;
    int frames = 0;
    std::string model;   // ObjectData/<model>.arc; empty: the player's own
    std::string music;   // a BGM_ name; empty: the acts_like form's
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
    // pre_standard rows that ran scripts on the interpreter plan 1.10b removed ("script" on a class or form, "objects",
    // "scripts"): nothing of them is read, and the mod is refused, said on its Mods page (mods.cpp).
    bool old_scripts = false;
    std::vector<SoundEffect> sound_effects;
    std::vector<PlayerAnimation> player_animations;
    When when;
};

struct Dependency {
    std::string id;
    std::string version;  // needs: the range as written (the validator's L800 checks it); conflicts: empty
};

// A mod's code (luma-format `code`): its entry library per platform, code/<platform>/mod.<ending> (code_entry).
struct Plugin {
    std::map<std::string, std::string> binaries;  // by plugin_target()
};

// A permission the mod asks the player for (luma.json "permissions"; services-registry 5, consent 1.1).
struct Permission {
    std::string name;                // one of services-registry 5.1's names
    std::string why;                 // the manifest's line, shown as it is (the mod's own words)
    std::vector<std::string> hosts;  // net.internet only: as written, in order
    std::vector<std::string> roots;  // net.local and net.internet: PEM files in the package the mod's TLS also trusts
};

enum class SaveMode {
    Shared,  // the player's normal saves
    Own,     // saves of its own, from nothing or from its starting files
    Copy,    // saves of its own, from a copy of the player's
};

// Rows and the fields below marked pre_standard are read only from "pre_standard" (OLD-ROWS.md).
struct Manifest : Rows {
    std::uint32_t format = 0;        // "format": "1.0" is major 1, minor 0
    std::uint32_t format_minor = 0;
    std::string id;
    Text name;         // "name", and text.<language>.name
    std::string version;
    std::string game;  // game.id: "RMG"
    // game.regions: the discs it is made for (RMGE01, RMGP01, RMGJ01); empty when not given (every disc).
    std::vector<std::string> regions;
    std::vector<std::string> authors;
    Text description;  // "summary", and text.<language>.summary
    std::string license;
    std::string homepage;  // links.home
    std::string icon;
    std::vector<std::string> pictures;  // extra PNG pictures shown after the icon (up to kMaxPictures)
    std::vector<Achievement> achievements;  // pre_standard; up to kMaxAchievements, unique ids
    std::vector<Dependency> needs;
    std::vector<Dependency> conflicts;      // id only (the form's `why` is the mod's own words)
    std::vector<std::string> load_after;    // load.after
    std::vector<std::string> load_before;   // load.before
    std::vector<std::string> takes_over;    // earlier ids of this mod: what they kept moves to it (services-save 6)
    std::vector<Option> options;            // choice, number, toggle, text and key options (mods.option reads each)
    std::vector<KeyOption> keys;            // key options: the mod's input actions (services-input), in Controls
    std::vector<Layer> layers;              // layer.path is layers/<name>
    // False when luma.json has no "layers": read_manifest then uses layers/base when the folder exists (luma-format 4).
    bool layers_listed = false;
    SaveMode save_mode = SaveMode::Shared;  // pre_standard save.mode
    std::string save_tag;   // pre_standard; mods with the same tag share saves; defaults to the mod id
    std::string save_path;  // pre_standard; folder in the mod with the starting save files
    bool save_player_can_change = false;
    // The mod's code: `code.platforms` read as one Plugin whose binaries are code/<platform>/mod.<ending> (code_entry).
    std::vector<Plugin> plugins;
    // luma-format 6 `code.libraries`: per platform, the extra libraries in load order, each a file name in
    // code/<platform>/ (they need the permission `native`; the loader loads them before the entry library).
    std::map<std::string, std::vector<std::string>> code_libraries;
    std::vector<Permission> permissions;  // in the manifest's order
};

// The luma.json format this game reads: "1.0". A newer one is read no further than its format and id (the validator's
// L205 refuses it).
inline constexpr std::uint32_t kFormat = 1;
inline constexpr std::uint32_t kFormatMinor = 0;
// Most pictures a mod page shows besides the icon; extra ones are ignored.
inline constexpr std::size_t kMaxPictures = 16;
// Most achievements one mod can add; extra ones are ignored.
inline constexpr std::size_t kMaxAchievements = 64;

// Null when luma.json is no JSON object, its format is not "<major>.<minor>", its id is not an id, or a pre_standard row
// cannot be read. What else the form requires is the validator's to refuse.
[[nodiscard]] std::optional<Manifest> parse_manifest(std::string_view text);
[[nodiscard]] std::optional<Manifest> read_manifest(const std::filesystem::path& folder);

// Compares dotted versions part by part: 2.10 > 2.9, and 2.1 == 2.1.0 (the import's update message).
[[nodiscard]] int compare_versions(std::string_view a, std::string_view b);

// Whether `version` is inside a semver range as luma-format's `needs` writes it: comparators (>=, >, <=, <, =, a bare
// version, ^ and ~, *, x) joined by spaces or commas, alternatives by ||; prerelease tags are not compared. 0 inside
// (and for an empty range or one this cannot read: the validator's L800 judges those), -1 below the first
// alternative's floor, 1 otherwise.
[[nodiscard]] int version_in_range(std::string_view version, std::string_view range);

// The current game, used to decide whether a layer applies.
struct Situation {
    std::string region;    // the disc: RMGE01
    std::string version;   // the game's version letter (pre_standard `version`)
    std::string language;  // the folder of the language played
    std::string platform;  // plugin_target()
    std::map<std::string, std::string> options;  // option id -> choice id ("off" when off)
    std::vector<std::string> mods;  // ids of the mods that are on
    bool code = false;  // the mod's code runs here (allowed, with an entry library for this platform)
    // The layers with `"code": true` its code chose, by name, as the content was cooked with them (mods.cpp
    // code_layers_record): such a layer is used only when named here.
    std::vector<std::string> code_layers;
};
[[nodiscard]] bool layer_used(const Layer& layer, const Situation& situation);
// A layer's name: its folder under layers/.
[[nodiscard]] std::string layer_name(const Layer& layer);
[[nodiscard]] bool when_holds(const When& when, const Situation& situation);
// This platform as luma-format 6 names it (a `code.platforms` entry, a folder under code/, a plugin `binaries` key):
// windows-x64, windows-arm64, linux-x64, linux-arm64, macos-x64, macos-arm64, android-arm64, android-x64, ios-arm64.
[[nodiscard]] std::string_view plugin_target() noexcept;
// The entry library of `platform` in a .luma (luma-format 6): "code/<platform>/mod.dll" (Windows), "mod.so" (Linux,
// Android), "mod.dylib" (macOS, iOS); empty for a platform the format does not name.
[[nodiscard]] std::string code_entry(std::string_view platform);

// The language code (for Text) of a game language folder.
[[nodiscard]] std::string language_code(std::string_view folder);

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
