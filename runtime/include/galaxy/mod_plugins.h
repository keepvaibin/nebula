// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// A .luma's code: native libraries the host loads next to the game module. They talk to the game through
// nebula_mod_api.h.

#include <nebula_mod_api.h>

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy::mods {

struct PluginFile {
    std::filesystem::path path;
    std::string mod_id;
    std::string version;
    std::map<std::string, std::string> options;  // option id -> choice id
    std::string code;  // code_hash taken at start; a library that changed before it loads is not run
    std::vector<std::string> needs;  // the ids of the mods its manifest needs (loader D3-3: those start before it)
    // The permissions the player granted it, for its declarations as they are now (mod_consent.h granted_permissions):
    // what luma_load_mod is given. Filled once when the start's plan is made.
    std::vector<std::string> granted;
};

// A hex hash of every file in a plugin library's folder (names and contents), so the files it ships with count too.
// Empty when the folder can't be read. The player's permission is tied to this hash.
[[nodiscard]] std::string code_hash(const std::filesystem::path& library);

// A galaxy from a .luma's luma.json.
struct GalaxyToAdd {
    std::string mod_id;
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

// A music track from a .luma's luma.json.
struct MusicToAdd {
    struct Place {
        std::string stage;
        int scenario = 0;
        int change_area = -1;
    };
    std::string mod_id;
    std::string name;
    std::string file;
    std::vector<Place> places;
};

// A progression table row, and a dome's comets.
struct EventFlagToSet {
    std::string mod_id;
    std::string name;
    int kind = 0;
    int a = 0;
    int b = 0;
    std::string first;
    std::string second;
};
struct CometsToSet {
    std::string mod_id;
    int dome = 0;
    std::vector<std::pair<std::string, int>> comets;
};
struct ObjectChangeToSet {
    std::string mod_id;
    int where = 0;
    std::string object;
    std::string becomes;  // empty: removed
};
struct CometLimitToSet {
    std::string mod_id;
    std::string galaxy;
    int mission = 0;
    int seconds = 0;
};

struct StageRuleToSet {
    std::string mod_id;
    std::string stage;
    int scenario = 0;
    std::vector<std::string> acts_as;
    bool own_rules = true;
};
struct PlayerConstantToSet {
    std::string mod_id;
    int character = 0;
    std::string name;
    int index = 0;
    float value = 0.0f;
};
struct TunableToSet {
    std::string mod_id;
    std::string key;  // "GameRules.cPlayerHealth"
    double value = 0.0;
};
struct SoundEffectToAdd {
    std::string mod_id;
    std::string name;
    std::string file;
};
struct PlayerAnimationToAdd {
    std::string mod_id;
    std::string name;
    std::string file;
    double rate = 1.0;
    int blend_frames = 0;
};
struct ObjectClassToSet {
    std::string mod_id;
    std::string mod_version;
    std::string name;
    std::string like;    // empty: one of the game's classes
    std::string script;  // empty: none
};

// A power-up from data (luma::Form), for the mod that adds it.
struct FormToAdd {
    std::string mod_id;
    std::string mod_version;
    std::string name;
    int acts_like = 0;
    int looks_like = 0;
    int color_frame = 0;
    int frames = 0;
    std::string model;
    std::string music;
    std::string script;
};

// A mod-wide script (luma.json "scripts"), for the mod that runs it.
struct ModScriptToAdd {
    std::string mod_id;
    std::string mod_version;
    std::string script;
};

// An object a script runs (luma::ScriptObject), for the mod that adds it.
struct ScriptObjectToAdd {
    std::string mod_id;
    std::string mod_version;
    std::string name;
    std::string script;
    std::string archive;
    double radius = 60.0;
    int kind = 0;
    bool collision = false;
    double shadow_radius = 0.0;
    double clip_radius = 0.0;
};

// A mod's achievement for this run (luma::Achievement), with text in the language played.
struct AchievementToAdd {
    std::string mod_id;  // the mod's folder name, which is also its plugin's NebulaPlugin id
    std::string key;     // "mod:<.luma id>/<id>", the key in achievements.ini
    std::string name;
    std::string description;
    std::string mod_name;
    bool hidden = false;
    std::string event;
    std::string event_name;
    bool has_value = false;
    std::int64_t event_value = 0;
};

// Everything the .luma mods' luma.json files set, handed to the game before it starts.
struct ModRows {
    std::vector<GalaxyToAdd> galaxies;
    std::vector<MusicToAdd> music;
    struct TextToAdd {
        std::string id;
        std::string words;  // in the language played
        std::string like;   // existing message whose talk settings a new message copies; empty: none
    };
    std::vector<TextToAdd> texts;
    std::vector<EventFlagToSet> event_flags;
    std::vector<CometsToSet> comets;
    std::vector<ObjectChangeToSet> object_changes;
    std::vector<CometLimitToSet> comet_limits;
    std::vector<StageRuleToSet> stage_rules;
    std::vector<PlayerConstantToSet> player_constants;
    std::vector<TunableToSet> tunables;
    std::vector<ObjectClassToSet> object_classes;
    std::vector<FormToAdd> forms;
    std::vector<ScriptObjectToAdd> script_objects;
    std::vector<ModScriptToAdd> mod_scripts;
    std::vector<SoundEffectToAdd> sound_effects;
    std::vector<PlayerAnimationToAdd> player_animations;
    std::vector<AchievementToAdd> achievements;
    std::map<std::string, std::string> story;

    [[nodiscard]] bool empty() const noexcept { return achievements.empty() && story.empty() && sound_effects.empty() && player_animations.empty() && stage_rules.empty() && player_constants.empty() && tunables.empty() && object_classes.empty() && forms.empty() && script_objects.empty() && mod_scripts.empty() && object_changes.empty() && comet_limits.empty() && galaxies.empty() && music.empty() && texts.empty() && event_flags.empty() && comets.empty(); }
};

// The language refusals are said in (loader contract 3): the played language's folder name, as language_folder gives
// it ("EuGerman"); empty or unknown to the validator: UsEnglish. Set before start_plugins.
void set_message_language(std::string folder);
// Starts the plugins and returns how many started. `game_api` is the game module's nebula_game_mod_api(); `find_symbol`
// looks up a name in the game module (null when absent). A plugin that can't start is logged and skipped.
std::size_t start_plugins(const std::vector<PluginFile>& plugins, const ModRows& rows, const NebulaModApi* game_api,
                          void* (*find_symbol)(const char*),
                          void (*log)(const char*));
// Writes every tunable mods can set to `list_file` (--dump-tunables), one "<key>\t<kind>\t<default>" line each (list
// entries as <key>[i]). Returns false when the game module has no list or the file can't be written.
bool dump_tunables(const NebulaModApi* game_api, const std::filesystem::path& list_file);
// Sets where unlocked mod achievements are saved (galaxy::achievements::unlock_mod, which returns true when new). When
// unset (the headless host), unlocks last only for the run.
void set_achievement_store(bool (*unlock)(std::string_view key)) noexcept;
// The running mods' achievements, in the order start_plugins received them.
[[nodiscard]] std::size_t mod_achievement_count() noexcept;
[[nodiscard]] const AchievementToAdd* mod_achievement(std::size_t index) noexcept;
// The index of an achievement unlocked since the last call (by a plugin or by its event), for the pop-up; -1 if none.
[[nodiscard]] std::int32_t next_mod_achievement_unlocked() noexcept;
// The NEBULA_MOD_EVENT_* value for an event name ("star_get"), or -1.
[[nodiscard]] int mod_event_by_name(std::string_view name) noexcept;

// The mod id of the plugin containing `address`, or null.
[[nodiscard]] const char* plugin_at(const void* address) noexcept;

}  // namespace galaxy::mods
