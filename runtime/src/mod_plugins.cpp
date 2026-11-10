// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/mod_plugins.h"
#include "galaxy/mods.h"

#include "galaxy/luma.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <cstring>
#include <string>
#include <filesystem>
#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <cstdio>
#include <map>
#include <memory>
#include <atomic>
#include <mutex>
#include <string_view>

#if defined(_WIN32)
#include "galaxy/windows_api.h"
#if defined(_M_X64) || defined(__x86_64__)
#include <MinHook.h>
#define NEBULA_HAS_FUNCTION_HOOKS 1
#endif
#else
#include <dlfcn.h>
#include "galaxy/mod_hook.h"
#define NEBULA_HAS_POSIX_HOOKS 1
#endif

namespace galaxy::mods {
namespace {

struct Loaded {
    NebulaPlugin self{};
    std::string mod_id;
    std::string version;
    std::map<std::string, std::string> options;
    void* module = nullptr;
};

std::vector<std::unique_ptr<Loaded>> g_loaded;
// Data mods with scripts: their records, one per mod, for the run.
std::vector<std::unique_ptr<Loaded>> g_data_mods;

Loaded* data_mod(std::unique_ptr<Loaded> made) {
    for (const auto& mod : g_data_mods) {
        if (mod->mod_id == made->mod_id) return mod.get();
    }
    g_data_mods.push_back(std::move(made));
    return g_data_mods.back().get();
}
NebulaModApi g_api{};
void* (*g_find_symbol)(const char*) = nullptr;
void (*g_log)(const char*) = nullptr;
bool g_starting = false;
// The last replacement on each hooked function: the next plugin's hook goes in front of it.
std::map<void*, void*> g_hook_tops;
// The running mods' achievements; those unlocked and not yet shown, in order.
std::vector<AchievementToAdd> g_achievements;
// Each one known to be unlocked (now or in an earlier run): an event that matches it again costs nothing.
std::unique_ptr<std::atomic<bool>[]> g_achievement_done;
bool (*g_achievement_store)(std::string_view key) = nullptr;
std::mutex g_unlocked_mutex;
std::vector<std::int32_t> g_unlocked;

// NebulaModEvent's names in lower case, in its order: what an achievement's "when" names.
constexpr const char* kEventNames[] = {
    "frame", "stage_frame", "stage_start", "player_damage", "player_miss", "power_up", "coin", "star_bit", "star_get",
    "cutscene_start", "cutscene_end", "actor_appear", "actor_kill", "player_spin", "player_jump", "player_land", "pause",
    "stage_end", "game_save", "power_star_appear", "talk_line", "talk_choice", "cutscene_part", "camera_change",
    "sound_start", "effect_start", "screen", "actor_message", "player_action",
};
static_assert(sizeof(kEventNames) / sizeof(kEventNames[0]) == NEBULA_MOD_EVENT_COUNT, "an event without its name");

bool unlock_achievement_at(std::size_t index) {
    if (index >= g_achievements.size() || g_achievement_done[index].exchange(true)) return false;
    if (g_achievement_store != nullptr && !g_achievement_store(g_achievements[index].key)) return false;
    const std::lock_guard<std::mutex> lock(g_unlocked_mutex);
    g_unlocked.push_back(static_cast<std::int32_t>(index));
    return true;
}

// 1 when it was unlocked now; 0 when it was already, or the mod has no achievement of that id.
int32_t api_unlock_achievement(const NebulaPlugin* self, const char* id) {
    if (self == nullptr || self->id == nullptr || id == nullptr) return 0;
    for (std::size_t i = 0; i < g_achievements.size(); ++i) {
        const AchievementToAdd& achievement = g_achievements[i];
        const std::string_view key = achievement.key;
        if (achievement.mod_id != self->id || !key.ends_with(id) || key.size() <= std::strlen(id) || key[key.size() - std::strlen(id) - 1u] != '/') continue;
        return unlock_achievement_at(i) ? 1 : 0;
    }
    return 0;
}

// An event some achievement's "when" names: each one it matches is unlocked. What the event carries is not changed.
int32_t achievement_event(const NebulaModEventData* data, void* user) {
    const int event = static_cast<int>(reinterpret_cast<std::intptr_t>(user));
    for (std::size_t i = 0; i < g_achievements.size(); ++i) {
        const AchievementToAdd& achievement = g_achievements[i];
        if (mod_event_by_name(achievement.event) != event) continue;
        if (!achievement.event_name.empty() && (data->name == nullptr || achievement.event_name != data->name)) continue;
        if (achievement.has_value && achievement.event_value != data->value) continue;
        (void)unlock_achievement_at(i);
    }
    return data->value;
}

void say(const std::string& text) {
    if (g_log != nullptr) g_log(text.c_str());
}

void* open_library(const std::filesystem::path& path) {
#if defined(_WIN32)
    // Beside it first, for what it brings; the game module is found already loaded.
    return LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
#else
    return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

// The library a plugin runs from. On Windows, a copy of its folder's files (mods/.cooked/code/<process>-<n>): Windows
// holds a loaded library's file, and the mod's own folder then stays free to update or remove while the game runs.
// The copies of runs that have ended go when the first plugin starts; a copy that can't be made runs the mod's own.
std::filesystem::path running_copy(const std::filesystem::path& library, std::size_t index) {
#if defined(_WIN32)
    namespace fs = std::filesystem;
    std::error_code error;
    const fs::path root = galaxy::mods::mods_folder() / ".cooked" / "code";
    const std::string mine = std::to_string(GetCurrentProcessId()) + "-";
    if (index == 0u) {
        for (auto it = fs::directory_iterator(root, error); !error && it != fs::directory_iterator(); it.increment(error)) {
            std::error_code remove_error;
            fs::remove_all(it->path(), remove_error);  // a running game's copies are held and stay
        }
        error.clear();
    }
    const fs::path folder = root / (mine + std::to_string(index));
    fs::remove_all(folder, error);
    fs::create_directories(folder, error);
    if (error) return library;
    for (auto it = fs::directory_iterator(library.parent_path(), error); !error && it != fs::directory_iterator(); it.increment(error)) {
        std::error_code copy_error;
        if (it->is_regular_file(copy_error)) {
            fs::copy_file(it->path(), folder / it->path().filename(), fs::copy_options::overwrite_existing, copy_error);
            if (copy_error) return library;
        }
    }
    if (!error && fs::is_regular_file(folder / library.filename(), error)) return folder / library.filename();
#else
    (void)index;
#endif
    return library;
}

void close_library(void* module) {
#if defined(_WIN32)
    FreeLibrary(static_cast<HMODULE>(module));
#else
    dlclose(module);
#endif
}

void* symbol_in(void* module, const char* name) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(module), name));
#else
    return dlsym(module, name);
#endif
}

std::string load_error() {
#if defined(_WIN32)
    const DWORD code = GetLastError();
    // The two a plugin's author meets: it needs a library that isn't there, or a game function this game doesn't have.
    if (code == ERROR_MOD_NOT_FOUND) return "a library it needs isn't there";
    if (code == ERROR_PROC_NOT_FOUND) return "it was built for another version of the game";
    if (code == ERROR_BAD_EXE_FORMAT) return "it is not for this kind of computer";
    return "error " + std::to_string(code);
#else
    const char* text = dlerror();
    return text != nullptr ? text : "unknown";
#endif
}

void* api_alloc(uint64_t bytes) {
    return bytes == 0 || bytes > SIZE_MAX ? nullptr : std::malloc(static_cast<std::size_t>(bytes));
}

void api_release(void* memory) {
    std::free(memory);
}

int32_t api_format(char* buffer, uint64_t size, const char* format, ...) {
    if (format == nullptr || (buffer == nullptr && size != 0)) return -1;
    std::va_list args;
    va_start(args, format);
    const int written = std::vsnprintf(buffer, static_cast<std::size_t>(size > SIZE_MAX ? SIZE_MAX : size), format, args);
    va_end(args);
    return written;
}

void api_log(const NebulaPlugin* self, const char* text) {
    say((self != nullptr && self->id != nullptr ? std::string(self->id) : std::string("plugin")) + ": " + (text != nullptr ? text : ""));
}

const char* api_option(const NebulaPlugin* self, const char* id) {
    if (self == nullptr || id == nullptr) return nullptr;
    for (const std::unique_ptr<Loaded>& loaded : g_loaded) {
        if (&loaded->self != self) continue;
        const auto it = loaded->options.find(id);
        return it != loaded->options.end() ? it->second.c_str() : nullptr;
    }
    return nullptr;
}

// A number kept for a mod across plays: one small text file per mod, key=value, beside the mods.
std::filesystem::path store_file(const NebulaPlugin* self) {
    return mods_folder() / ".plugin-data" / (std::string(self->id) + ".txt");
}

std::map<std::string, std::int32_t> read_store(const NebulaPlugin* self) {
    std::map<std::string, std::int32_t> values;
    std::ifstream file(store_file(self));
    std::string line;
    while (std::getline(file, line)) {
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) continue;
        char* end = nullptr;
        const long value = std::strtol(line.c_str() + equals + 1, &end, 10);
        if (end != nullptr && end != line.c_str() + equals + 1) {
            values[line.substr(0, equals)] = static_cast<std::int32_t>(
                std::clamp<long>(value, std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max()));
        }
    }
    return values;
}

// A key is one line's left side.
bool store_key_ok(const char* key) {
    if (key == nullptr || key[0] == '\0') return false;
    for (const char* at = key; *at != '\0'; ++at) {
        if (*at == '=' || *at == '\n' || *at == '\r') return false;
    }
    return true;
}

std::int32_t api_store_get(const NebulaPlugin* self, const char* key, std::int32_t missing) {
    if (self == nullptr || self->id == nullptr || !store_key_ok(key)) return missing;
    const std::map<std::string, std::int32_t> values = read_store(self);
    const auto it = values.find(key);
    return it != values.end() ? it->second : missing;
}

void api_store_set(const NebulaPlugin* self, const char* key, std::int32_t value) {
    if (self == nullptr || self->id == nullptr || !store_key_ok(key)) return;
    std::map<std::string, std::int32_t> values = read_store(self);
    values[key] = value;
    const std::filesystem::path path = store_file(self);
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    // Written whole and moved over, so that a crash in the middle leaves the last good file.
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::trunc);
        for (const auto& [name, number] : values) file << name << '=' << number << '\n';
        if (!file) return;
    }
    std::filesystem::rename(temporary, path, error);
}

// Text beside numbers: each its own file line, "key:text", with what would break a line escaped.
std::filesystem::path text_store_file(const NebulaPlugin* self) {
    return mods_folder() / ".plugin-data" / (std::string(self->id) + ".text.txt");
}

std::map<std::string, std::string> read_text_store(const NebulaPlugin* self) {
    std::map<std::string, std::string> values;
    std::ifstream file(text_store_file(self));
    std::string line;
    while (std::getline(file, line)) {
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) continue;
        std::string text;
        for (std::size_t i = equals + 1u; i < line.size(); ++i) {
            if (line[i] == '\\' && i + 1u < line.size()) {
                ++i;
                text += line[i] == 'n' ? '\n' : line[i] == 'r' ? '\r' : line[i];
            } else {
                text += line[i];
            }
        }
        values[line.substr(0, equals)] = std::move(text);
    }
    return values;
}

std::int32_t api_store_get_text(const NebulaPlugin* self, const char* key, char* out, std::int32_t size) {
    if (self == nullptr || self->id == nullptr || !store_key_ok(key) || out == nullptr || size <= 0) return 0;
    out[0] = '\0';
    const std::map<std::string, std::string> values = read_text_store(self);
    const auto it = values.find(key);
    if (it == values.end()) return 0;
    const std::size_t count = std::min(it->second.size(), static_cast<std::size_t>(size - 1));
    std::memcpy(out, it->second.data(), count);
    out[count] = '\0';
    return static_cast<std::int32_t>(count);
}

void api_store_set_text(const NebulaPlugin* self, const char* key, const char* text) {
    if (self == nullptr || self->id == nullptr || !store_key_ok(key) || text == nullptr) return;
    std::map<std::string, std::string> values = read_text_store(self);
    values[key] = text;
    const std::filesystem::path path = text_store_file(self);
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    std::filesystem::path temporary = path;
    temporary += ".tmp";
    {
        std::ofstream file(temporary, std::ios::trunc | std::ios::binary);
        for (const auto& [name, words] : values) {
            file << name << '=';
            for (const char c : words) {
                if (c == '\n') file << "\\n";
                else if (c == '\r') file << "\\r";
                else if (c == '\\') file << "\\\\";
                else file << c;
            }
            file << '\n';
        }
        if (!file) return;
    }
    std::filesystem::rename(temporary, path, error);
}

int32_t api_hook(void* target, void* replacement, void** original) {
    if (!g_starting || target == nullptr || replacement == nullptr || original == nullptr) return -1;
#if defined(NEBULA_HAS_FUNCTION_HOOKS)
    static const bool ready = MH_Initialize() == MH_OK;
    if (!ready) return -1;
    // A second hook on one function goes on the first's replacement, so that each one's `original` calls the one before.
    const auto top = g_hook_tops.find(target);
    void* const place = top != g_hook_tops.end() ? top->second : target;
    if (MH_CreateHook(place, replacement, original) != MH_OK || MH_EnableHook(place) != MH_OK) return -1;
    g_hook_tops[target] = replacement;
    return 0;
#elif defined(NEBULA_HAS_POSIX_HOOKS)
    const auto top = g_hook_tops.find(target);
    void* const place = top != g_hook_tops.end() ? top->second : target;
    if (!hook_function(place, replacement, original)) return -1;
    g_hook_tops[target] = replacement;
    return 0;
#else
    return -1;
#endif
}

void* api_find_function(const char* symbol) {
    return symbol != nullptr && g_find_symbol != nullptr ? g_find_symbol(symbol) : nullptr;
}

}  // namespace

bool dump_tunables(const NebulaModApi* game_api, const std::filesystem::path& list_file) {
    if (game_api == nullptr || game_api->size < sizeof(NebulaModApi) || game_api->tunable_at == nullptr) return false;
    std::ofstream out(list_file, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    static constexpr const char* kKinds[] = {"?", "f32", "f64", "s32", "u32", "s16", "u16", "s8", "u8", "bool"};
    const char* file = nullptr;
    const char* name = nullptr;
    int32_t place = -1;
    int32_t kind = 0;
    double game_value = 0.0;
    double value = 0.0;
    for (int32_t i = 0; game_api->tunable_at(i, &file, &name, &place, &kind, &game_value, &value) == 0; ++i) {
        std::string stem = file != nullptr ? file : "";
        if (const std::size_t dot = stem.rfind('.'); dot != std::string::npos) stem.resize(dot);
        char number[64];
        std::snprintf(number, sizeof(number), "%.9g", game_value);
        out << stem << '.' << (name != nullptr ? name : "");
        if (place >= 0) out << '[' << place << ']';
        out << '\t' << kKinds[(kind >= 1 && kind <= 9) ? kind : 0] << '\t' << number << '\n';
    }
    return static_cast<bool>(out.flush());
}

std::size_t start_plugins(const std::vector<PluginFile>& plugins, const ModRows& rows, const NebulaModApi* game_api,
                          void* (*find_symbol)(const char*),
                          void (*log)(const char*)) {
    g_log = log;
    g_find_symbol = find_symbol;
    if (plugins.empty() && rows.empty()) return 0;
    if (game_api == nullptr || game_api->size < sizeof(NebulaModApi)) {
        say("the game module has no plugin interface");
        return 0;
    }
    g_api = *game_api;
    g_api.log = api_log;
    g_api.option = api_option;
    g_api.hook = api_hook;
    g_api.find_function = api_find_function;
    g_api.store_get = api_store_get;
    g_api.store_set = api_store_set;
    g_api.store_get_text = api_store_get_text;
    g_api.store_set_text = api_store_set_text;
    g_api.alloc = api_alloc;
    g_api.release = api_release;
    g_api.format = api_format;
    g_api.unlock_achievement = api_unlock_achievement;
    // The game's scripts log, keep values and format through the completed table.
    if (g_api.luma_start != nullptr) g_api.luma_start(&g_api);
    g_achievements = rows.achievements;
    g_achievement_done = std::make_unique<std::atomic<bool>[]>(g_achievements.size());
    {
        bool subscribed[NEBULA_MOD_EVENT_COUNT] = {};
        for (const AchievementToAdd& achievement : g_achievements) {
            if (achievement.event.empty()) continue;
            const int event = mod_event_by_name(achievement.event);
            if (event < 0) {
                say(achievement.mod_id + ": the achievement " + achievement.key + " waits for an event the game has not: " + achievement.event);
                continue;
            }
            if (subscribed[event]) continue;
            subscribed[event] = true;
            if (g_api.subscribe(static_cast<NebulaModEvent>(event), achievement_event, reinterpret_cast<void*>(static_cast<std::intptr_t>(event))) != 0) {
                say(achievement.mod_id + ": the achievement " + achievement.key + " cannot wait for " + achievement.event);
            }
        }
    }
    for (const ModRows::TextToAdd& text : rows.texts) {
        if (text.like.empty()) {
            g_api.add_text(text.id.c_str(), text.words.c_str());
        } else {
            g_api.add_text_like(text.id.c_str(), text.words.c_str(), text.like.c_str());
        }
    }
    for (const GalaxyToAdd& galaxy : rows.galaxies) {
        NebulaGalaxy row{};
        row.size = sizeof(row);
        row.name = galaxy.name.c_str();
        row.map_pane = galaxy.map_pane.empty() ? nullptr : galaxy.map_pane.c_str();
        std::vector<std::string> conditions = galaxy.open_conditions;
        if (galaxy.hungry_slot >= 0 && conditions.size() < 3u) conditions.push_back("StarPiece" + galaxy.name);
        for (std::size_t i = 0; i < conditions.size(); ++i) row.open_condition[i] = conditions[i].c_str();
        row.stars_to_open = galaxy.stars_to_open;
        row.dome = galaxy.dome;
        row.has_map_position = galaxy.has_map_position ? 1 : 0;
        row.map_position[0] = galaxy.map_x;
        row.map_position[1] = galaxy.map_y;
        const int32_t added = g_api.add_galaxy(&row);
        if (added == -2) {
            say(galaxy.mod_id + ": the galaxy " + galaxy.name + " is left out: it has no StageData/" + galaxy.name + "/" + galaxy.name +
                "Scenario.arc");
            continue;
        }
        if (added != 0) {
            say(galaxy.mod_id + ": the galaxy " + galaxy.name + " can't be added: its name clashes with one the game has");
            continue;
        }
        if (galaxy.hungry_slot >= 0 && g_api.add_hungry_luma(galaxy.name.c_str(), galaxy.hungry_slot, galaxy.hungry_star_bits) != 0) {
            say(galaxy.mod_id + ": the hungry Luma for " + galaxy.name + " can't be added");
        }
    }
    for (const EventFlagToSet& flag : rows.event_flags) {
        if (g_api.set_event_flag(flag.name.c_str(), flag.kind, flag.a, flag.b, flag.first.empty() ? nullptr : flag.first.c_str(), flag.second.empty() ? nullptr : flag.second.c_str()) != 0) {
            say(flag.mod_id + ": the row " + flag.name + " can't be set");
        }
    }
    // By dome number, so a mod's further domes come in the order the game data takes them (the next dome after the last).
    std::vector<const CometsToSet*> comet_domes;
    for (const CometsToSet& dome : rows.comets) comet_domes.push_back(&dome);
    std::stable_sort(comet_domes.begin(), comet_domes.end(), [](const CometsToSet* a, const CometsToSet* b) { return a->dome < b->dome; });
    for (const CometsToSet* dome_row : comet_domes) {
        const CometsToSet& dome = *dome_row;
        std::vector<const char*> galaxies;
        std::vector<std::int32_t> coins;
        for (const auto& [galaxy, coin] : dome.comets) {
            galaxies.push_back(galaxy.c_str());
            coins.push_back(coin);
        }
        if (g_api.set_comet_schedule(dome.dome, galaxies.data(), coins.data(), static_cast<std::int32_t>(galaxies.size())) != 0) {
            say(dome.mod_id + ": the comets of dome " + std::to_string(dome.dome) + " can't be set");
        }
    }
    for (const ObjectChangeToSet& change : rows.object_changes) {
        if (g_api.set_object_change(change.where, change.object.c_str(), change.becomes.empty() ? nullptr : change.becomes.c_str()) != 0) {
            say(change.mod_id + ": the object " + change.object + " can't be changed");
        }
    }
    for (const CometLimitToSet& limit : rows.comet_limits) {
        if (g_api.set_comet_time_limit(limit.galaxy.c_str(), limit.mission, limit.seconds) != 0) {
            say(limit.mod_id + ": the time limit of " + limit.galaxy + " can't be set");
        }
    }
    for (const StageRuleToSet& rule : rows.stage_rules) {
        for (const std::string& name : rule.acts_as) g_api.set_stage_rules(rule.stage.c_str(), rule.scenario, name.c_str(), 1);
        if (!rule.own_rules) g_api.set_stage_rules(rule.stage.c_str(), rule.scenario, nullptr, 0);
    }
    // A number past what a number holds (1e999 in luma.json reads as infinite) changes nothing, and is named as such.
    for (const PlayerConstantToSet& constant : rows.player_constants) {
        if (!std::isfinite(constant.value)) {
            say(constant.mod_id + ": " + constant.name + " is set to a number too large to hold");
        } else if (g_api.set_player_constant(constant.character, constant.name.c_str(), constant.index, constant.value) != 0) {
            say(constant.mod_id + ": the player has no number " + constant.name);
        }
    }
    for (const TunableToSet& tunable : rows.tunables) {
        if (!std::isfinite(tunable.value)) {
            say(tunable.mod_id + ": " + tunable.key + " is set to a number too large to hold");
        } else if (g_api.set_tunable(tunable.key.c_str(), tunable.value) != 0) {
            say(tunable.mod_id + ": the game has no tuning number " + tunable.key);
        }
    }
    for (const auto& [key, value] : rows.story) {
        if (g_api.set_story(key.c_str(), value.c_str()) != 0) say("the story has no " + key);
    }
    for (const SoundEffectToAdd& sound : rows.sound_effects) {
        if (g_api.add_sound_effect(sound.name.c_str(), sound.file.c_str()) != 0) {
            say(sound.mod_id + ": the sound " + sound.name + " can't be added");
        }
    }
    for (const PlayerAnimationToAdd& animation : rows.player_animations) {
        if (g_api.add_player_animation(animation.name.c_str(), animation.file.c_str(), static_cast<float>(animation.rate), animation.blend_frames) != 0) {
            say(animation.mod_id + ": the player animation " + animation.name + " can't be added");
        }
    }
    for (const ObjectClassToSet& object : rows.object_classes) {
        if (!object.like.empty() && g_api.set_object_class(object.name.c_str(), object.like.c_str()) != 0) {
            say(object.mod_id + ": the object " + object.name + " can't be added");
        }
    }
    for (const ScriptObjectToAdd& row : rows.script_objects) {
        // A data mod has no plugin record of its own: the scripts' mod is this one (its id names their folder and store).
        auto loaded = std::make_unique<Loaded>();
        loaded->mod_id = row.mod_id;
        loaded->version = row.mod_version;
        loaded->self.size = sizeof(NebulaPlugin);
        loaded->self.id = loaded->mod_id.c_str();
        loaded->self.version = loaded->version.c_str();
        Loaded* const mod = data_mod(std::move(loaded));
        NebulaObjectClass shape{};
        shape.size = sizeof(NebulaObjectClass);
        shape.name = row.name.c_str();
        shape.archive = row.archive.empty() ? nullptr : row.archive.c_str();
        shape.radius = static_cast<float>(row.radius);
        shape.kind = row.kind;
        shape.collision = row.collision ? 1 : 0;
        shape.shadow_radius = static_cast<float>(row.shadow_radius);
        shape.clip_radius = static_cast<float>(row.clip_radius);
        if (g_api.add_script_object == nullptr || g_api.add_script_object(&mod->self, &shape, row.script.c_str()) != 0) {
            say(row.mod_id + ": the object " + row.name + " can't be added (its script " + row.script + ")");
        }
    }
    // Class scripts once every class exists (a mod's "like" classes above).
    for (const ObjectClassToSet& object : rows.object_classes) {
        if (object.script.empty()) continue;
        auto loaded = std::make_unique<Loaded>();
        loaded->mod_id = object.mod_id;
        loaded->version = object.mod_version;
        loaded->self.size = sizeof(NebulaPlugin);
        loaded->self.id = loaded->mod_id.c_str();
        loaded->self.version = loaded->version.c_str();
        Loaded* const mod = data_mod(std::move(loaded));
        if (g_api.set_class_script == nullptr || g_api.set_class_script(&mod->self, object.name.c_str(), object.script.c_str()) != 0) {
            say(object.mod_id + ": the script " + object.script + " can't run on " + object.name);
        }
    }
    // Power-ups from data: before the mod-wide scripts, which find them in the registry ("form/<name>").
    for (const FormToAdd& row : rows.forms) {
        auto loaded = std::make_unique<Loaded>();
        loaded->mod_id = row.mod_id;
        loaded->version = row.mod_version;
        loaded->self.size = sizeof(NebulaPlugin);
        loaded->self.id = loaded->mod_id.c_str();
        loaded->self.version = loaded->version.c_str();
        Loaded* const mod = data_mod(std::move(loaded));
        NebulaPowerUpClass form{};
        form.size = sizeof(NebulaPowerUpClass);
        form.name = row.name.c_str();
        form.frames = row.frames;
        form.music = row.music.empty() ? nullptr : row.music.c_str();
        form.color_frame = row.color_frame;
        form.model = row.model.empty() ? nullptr : row.model.c_str();
        form.looks_like = row.looks_like;
        form.acts_like = row.acts_like;
        if (g_api.add_form == nullptr || g_api.add_form(&mod->self, &form, row.script.empty() ? nullptr : row.script.c_str()) < 0) {
            say(row.mod_id + ": the form " + row.name + " can't be added");
        }
    }
    // Mod-wide scripts after the objects: a script that names a class finds it.
    for (const ModScriptToAdd& row : rows.mod_scripts) {
        auto loaded = std::make_unique<Loaded>();
        loaded->mod_id = row.mod_id;
        loaded->version = row.mod_version;
        loaded->self.size = sizeof(NebulaPlugin);
        loaded->self.id = loaded->mod_id.c_str();
        loaded->self.version = loaded->version.c_str();
        Loaded* const mod = data_mod(std::move(loaded));
        if (g_api.add_mod_script == nullptr || g_api.add_mod_script(&mod->self, row.script.c_str()) != 0) {
            say(row.mod_id + ": the script " + row.script + " can't start");
        }
    }
    for (const MusicToAdd& track : rows.music) {
        if (g_api.add_music(track.name.c_str(), track.file.c_str()) != 0) {
            say(track.mod_id + ": the music " + track.name + " can't be added: its name is the game's");
            continue;
        }
        for (const MusicToAdd::Place& place : track.places) {
            if (place.change_area >= 0) {
                g_api.set_area_music(place.stage.c_str(), place.change_area, track.name.c_str());
            } else {
                g_api.set_stage_music(place.stage.c_str(), place.scenario, track.name.c_str());
            }
        }
    }
    g_starting = true;
    for (std::size_t index = 0; index < plugins.size(); ++index) {
        const PluginFile& file = plugins[index];
        const std::string name = file.mod_id + " (" + file.path.filename().string() + ")";
        const std::filesystem::path running = running_copy(file.path, index);
        if (file.code.empty() || code_hash(running) != file.code) {
            say(name + (file.code.empty() ? " not started: its code can't be read" : " not started: its code changed after it was allowed"));
            continue;
        }
        void* const module = open_library(running);
        if (module == nullptr) {
            say(name + " not started: " + load_error());
            continue;
        }
        const auto api_version = reinterpret_cast<std::uint32_t (*)()>(symbol_in(module, "nebula_plugin_api"));
        const auto start = reinterpret_cast<void (*)(const NebulaModApi*, const NebulaPlugin*)>(symbol_in(module, "nebula_plugin_start"));
        if (api_version == nullptr || start == nullptr) {
            say(name + " not started: it is not a Nebula plugin");
            close_library(module);
            continue;
        }
        if (api_version() > NEBULA_MOD_API_VERSION) {
            say(name + " not started: it needs a newer Nebula");
            close_library(module);
            continue;
        }
        // Version 3 only added calls at the end: a version 2 plugin runs as it is.
        if (api_version() < 2u) {
            say(name + " not started: it was built for plugin interface " + std::to_string(api_version()) +
                "; rebuild it with this nebula_mod_api.h (setting() is now get_tunable(), object_arg(index) is "
                "placement_number(\"Obj_argN\"))");
            close_library(module);
            continue;
        }
        // A plugin built with the game's own headers (an earlier kit's nebula_game_sdk.h): no longer offered, and its
        // classes would not match this build.
        if (symbol_in(module, "nebula_plugin_game_abi") != nullptr) {
            say(name + " not started: it was built with the game's headers, which plugins no longer use; rebuild it with "
                       "nebula_mod_api.h only");
            close_library(module);
            continue;
        }
        auto loaded = std::make_unique<Loaded>();
        loaded->mod_id = file.mod_id;
        loaded->version = file.version;
        loaded->options = file.options;
        loaded->module = module;
        loaded->self.size = sizeof(NebulaPlugin);
        loaded->self.id = loaded->mod_id.c_str();
        loaded->self.version = loaded->version.c_str();
        Loaded* const mine = loaded.get();
        g_loaded.push_back(std::move(loaded));
        start(&g_api, &mine->self);
        say(name + " started");
    }
    g_starting = false;
    return g_loaded.size();
}

void set_achievement_store(bool (*unlock)(std::string_view key)) noexcept {
    g_achievement_store = unlock;
}

std::size_t mod_achievement_count() noexcept {
    return g_achievements.size();
}

const AchievementToAdd* mod_achievement(std::size_t index) noexcept {
    return index < g_achievements.size() ? &g_achievements[index] : nullptr;
}

std::int32_t next_mod_achievement_unlocked() noexcept {
    const std::lock_guard<std::mutex> lock(g_unlocked_mutex);
    if (g_unlocked.empty()) return -1;
    const std::int32_t index = g_unlocked.front();
    g_unlocked.erase(g_unlocked.begin());
    return index;
}

int mod_event_by_name(std::string_view name) noexcept {
    for (int i = 0; i < NEBULA_MOD_EVENT_COUNT; ++i) {
        if (name == kEventNames[i]) return i;
    }
    return -1;
}

const char* plugin_at(const void* address) noexcept {
#if defined(_WIN32)
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, static_cast<LPCWSTR>(address), &module)) {
        return nullptr;
    }
    for (const std::unique_ptr<Loaded>& loaded : g_loaded) {
        if (loaded->module == module) return loaded->mod_id.c_str();
    }
#else
    Dl_info info{};
    if (dladdr(address, &info) == 0 || info.dli_fname == nullptr) return nullptr;
    void* const here = dlopen(info.dli_fname, RTLD_NOW | RTLD_NOLOAD);
    if (here == nullptr) return nullptr;
    dlclose(here);
    for (const std::unique_ptr<Loaded>& loaded : g_loaded) {
        if (here == loaded->module) return loaded->mod_id.c_str();
    }
#endif
    return nullptr;
}

}  // namespace galaxy::mods
