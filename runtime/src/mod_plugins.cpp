// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/mod_plugins.h"
#include "galaxy/mods.h"

#include "galaxy/luma.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <filesystem>
#include <cstdlib>
#include <fstream>
#include <cstdio>
#include <map>
#include <memory>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
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
NebulaModApi g_api{};
void* (*g_find_symbol)(const char*) = nullptr;
void (*g_log)(const char*) = nullptr;
bool g_starting = false;
// The last replacement on each hooked function: the next plugin's hook goes in front of it.
std::map<void*, void*> g_hook_tops;

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
        if (end != nullptr && end != line.c_str() + equals + 1) values[line.substr(0, equals)] = static_cast<std::int32_t>(value);
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

// A setting by the name a layer's switches give it. The game answers by number.
std::int32_t (*g_setting_by_number)(std::uint32_t, std::int32_t) = nullptr;

std::int32_t api_setting(const char* name, std::int32_t missing) {
    if (name == nullptr || g_setting_by_number == nullptr) return missing;
    const std::uint32_t number = luma::switch_number(name);
    return number != 0u ? g_setting_by_number(number, missing) : missing;
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
    g_setting_by_number = game_api->setting_by_number;
    g_api.setting = api_setting;
    for (const auto& [id, words] : rows.texts) {
        g_api.add_text(id.c_str(), words.c_str());
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
        if (g_api.add_galaxy(&row) != 0) say(galaxy.mod_id + ": the galaxy " + galaxy.name + " can't be added: its name clashes with one the game has");
        if (galaxy.hungry_slot >= 0 && g_api.add_hungry_luma(galaxy.name.c_str(), galaxy.hungry_slot, galaxy.hungry_star_bits) != 0) {
            say(galaxy.mod_id + ": the hungry Luma for " + galaxy.name + " can't be added");
        }
    }
    for (const EventFlagToSet& flag : rows.event_flags) {
        if (g_api.set_event_flag(flag.name.c_str(), flag.kind, flag.a, flag.b, flag.first.empty() ? nullptr : flag.first.c_str(), flag.second.empty() ? nullptr : flag.second.c_str()) != 0) {
            say(flag.mod_id + ": the row " + flag.name + " can't be set");
        }
    }
    for (const CometsToSet& dome : rows.comets) {
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
    for (const PlayerConstantToSet& constant : rows.player_constants) {
        if (g_api.set_player_constant(constant.character, constant.name.c_str(), constant.index, constant.value) != 0) {
            say(constant.mod_id + ": the player has no number " + constant.name);
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
    for (const ObjectClassToSet& object : rows.object_classes) {
        if (g_api.set_object_class(object.name.c_str(), object.like.c_str()) != 0) {
            say(object.mod_id + ": the object " + object.name + " can't be added");
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
    for (const PluginFile& file : plugins) {
        const std::string name = file.mod_id + " (" + file.path.filename().string() + ")";
        void* const module = open_library(file.path);
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
        // A plugin built with the game's headers says which ones; any others have other shapes.
        if (const auto game_abi = reinterpret_cast<std::uint64_t (*)()>(symbol_in(module, "nebula_plugin_game_abi"));
            game_abi != nullptr && game_abi() != g_api.game_abi) {
            say(name + " not started: it was built for another version of the game");
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
    if (dladdr(address, &info) == 0) return nullptr;
    for (const std::unique_ptr<Loaded>& loaded : g_loaded) {
        void* const here = dlopen(info.dli_fname, RTLD_NOW | RTLD_NOLOAD);
        if (here != nullptr) dlclose(here);
        if (here == loaded->module) return loaded->mod_id.c_str();
    }
#endif
    return nullptr;
}

}  // namespace galaxy::mods
