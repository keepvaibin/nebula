// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// A .luma's code: native libraries the host loads next to the game module, through nebula_mod_api.h.

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
    std::map<std::string, std::string> options;  // each option's choice
    std::string code;  // code_hash of it as the start read it: a library that differs when it loads is not run
};

// A plugin library as it is: a hash of the files in its folder (their names and bytes: what it brings beside it too),
// as hex; empty when the folder can't be read. The player's permission names these.
[[nodiscard]] std::string code_hash(const std::filesystem::path& library);

// A .luma's galaxies, from its luma.json.
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

// A .luma's tracks, from its luma.json.
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

// A row of the game's progression, and a dome's comets.
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
    std::string becomes;  // empty: gone
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
    std::string name;
    std::string like;
};

// A mod's achievement for this run (luma::Achievement), its words in the language of play.
struct AchievementToAdd {
    std::string mod_id;  // the mod's folder: the plugin's NebulaPlugin id
    std::string key;     // "mod:<the .luma's id>/<id>": kept in achievements.ini by it
    std::string name;
    std::string description;
    std::string mod_name;
    bool hidden = false;
    std::string event;
    std::string event_name;
    bool has_value = false;
    std::int64_t event_value = 0;
};

// What .luma files say in their luma.json, for the game before it starts.
struct ModRows {
    std::vector<GalaxyToAdd> galaxies;
    std::vector<MusicToAdd> music;
    struct TextToAdd {
        std::string id;
        std::string words;  // in the language of play
        std::string like;   // the game message whose talk a new line takes; empty: none
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
    std::vector<SoundEffectToAdd> sound_effects;
    std::vector<PlayerAnimationToAdd> player_animations;
    std::vector<AchievementToAdd> achievements;
    std::map<std::string, std::string> story;

    [[nodiscard]] bool empty() const noexcept { return achievements.empty() && story.empty() && sound_effects.empty() && player_animations.empty() && stage_rules.empty() && player_constants.empty() && tunables.empty() && object_classes.empty() && object_changes.empty() && comet_limits.empty() && galaxies.empty() && music.empty() && texts.empty() && event_flags.empty() && comets.empty(); }
};

// `game_api`: the game module's nebula_game_mod_api(); `find_symbol`: a name in the game module, or null. Each plugin that
// can't start is named in the log and left out; the game carries on without it. What's returned: their count.
std::size_t start_plugins(const std::vector<PluginFile>& plugins, const ModRows& rows, const NebulaModApi* game_api,
                          void* (*find_symbol)(const char*),
                          void (*log)(const char*));
// Every tuning number mods can set, one "<key>\t<kind>\t<game's number>" line each (a list's places as <key>[i]),
// written to `list_file` (--dump-tunables). False when the game module has no list or the file can't be written.
bool dump_tunables(const NebulaModApi* game_api, const std::filesystem::path& list_file);
// Where a mod's achievement is kept once unlocked (galaxy::achievements::unlock_mod: true when it is new). Unset (the
// headless host), an unlock lasts the run.
void set_achievement_store(bool (*unlock)(std::string_view key)) noexcept;
// The running mods' achievements, as start_plugins was given them, in their order.
[[nodiscard]] std::size_t mod_achievement_count() noexcept;
[[nodiscard]] const AchievementToAdd* mod_achievement(std::size_t index) noexcept;
// One unlocked since the last call (a plugin's unlock_achievement, or its event), for its pop-up: its index; -1: none.
[[nodiscard]] std::int32_t next_mod_achievement_unlocked() noexcept;
// The event an achievement's "when" names, by its name ("star_get"): NEBULA_MOD_EVENT_*, or -1.
[[nodiscard]] int mod_event_by_name(std::string_view name) noexcept;

// The plugin an address is in: its mod's id, or null.
[[nodiscard]] const char* plugin_at(const void* address) noexcept;

}  // namespace galaxy::mods
