// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/mod_plugins.h"
#include "galaxy/mods.h"

#include "galaxy/luma.h"
#include "galaxy/host_extension.h"
#include "galaxy/luma_guard_host.h"
#include "galaxy/luma_services_host.h"
#include <luma_host.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <cstring>
#include <string>
#include <filesystem>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <map>
#include <memory>
#include <atomic>
#include <mutex>
#include <string_view>

#if defined(_WIN32)
#include "galaxy/windows_api.h"
#else
#include <dlfcn.h>
#endif

namespace galaxy::mods {
namespace {

struct Loaded {
    std::string mod_id;
    std::string library;  // the code's file name (a LumaCore mod's; its Mods page names it once taken out)
    std::string version;
    std::map<std::string, std::string> options;
    void* module = nullptr;
};

std::vector<std::unique_ptr<Loaded>> g_loaded;
NebulaModApi g_api{};
void* (*g_find_symbol)(const char*) = nullptr;
void (*g_log)(const char*) = nullptr;
// The running mods' achievements; those unlocked and not yet shown, in order.
std::vector<AchievementToAdd> g_achievements;
// Each one known to be unlocked (now or in an earlier run): an event that matches it again costs nothing.
std::unique_ptr<std::atomic<bool>[]> g_achievement_done;
bool (*g_achievement_store)(std::string_view key) = nullptr;
std::mutex g_unlocked_mutex;
std::vector<std::int32_t> g_unlocked;
// Z-10: each mod whose code did not start, and why; written by start_plugins, read by the Mods page.
std::mutex g_not_started_mutex;
std::vector<NotStarted> g_not_started;

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

// The game's loader closes a LumaCore mod's library itself, when none of its code can run any more (loader contract
// LR-5).
extern "C" void close_luma_library(void* module) {
    close_library(module);
}

// A LumaCore refusal in the validator's own words (loader contract 3): luma_validate_message from the importer's
// library (nebula_import.dll beside the game on Windows, libnebula_import.so in the app on Android and Linux; on Apple
// the importer is linked into the program, platform/apple/metal.cmake, and called directly). Without
// it the line gives the code and every parameter as the loader gave them, never nothing, and the log says once why.
// On Windows the library's own dependencies (its C runtime) come from System32: with the application folder alone the
// loader does not find them.
std::string load_error();
using ValidateMessage = const char* (*)(std::uint16_t code, const char* const* names, const char* const* values,
                                        std::uint32_t count, const char* language);
#if defined(__APPLE__)
extern "C" const char* luma_validate_message(std::uint16_t code, const char* const* names, const char* const* values,
                                             std::uint32_t count, const char* language);
#endif

ValidateMessage validate_message() {
    static ValidateMessage found = nullptr;
    static bool looked = false;
    if (!looked) {
        looked = true;
#if defined(__APPLE__)
        found = &luma_validate_message;
        void* const importer = nullptr;
        const std::string why;
#elif defined(_WIN32)
        void* const importer =
            LoadLibraryExW(L"nebula_import.dll", nullptr, LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        const std::string why = importer == nullptr ? "nebula_import.dll did not load (" + load_error() + ")" : "";
#else
        void* const importer = dlopen("libnebula_import.so", RTLD_NOW | RTLD_LOCAL);
        const std::string why = importer == nullptr ? "libnebula_import.so did not load (" + load_error() + ")" : "";
#endif
        if (importer != nullptr) {
            found = reinterpret_cast<ValidateMessage>(symbol_in(importer, "luma_validate_message"));
        }
        if (found == nullptr) {
            say("refusals are given as codes: " + (why.empty() ? std::string("the importer has no luma_validate_message") : why));
        }
    }
    return found;
}

std::string& message_language() {
    static std::string language;
    return language;
}

std::string refusal_text(const LumaRefusal& refusal) {
    const char* values[LUMA_REFUSAL_MOST] = {};
    const std::uint32_t count = refusal.count < LUMA_REFUSAL_MOST ? refusal.count : LUMA_REFUSAL_MOST;
    for (std::uint32_t i = 0; i < count; ++i) values[i] = refusal.values[i];
    if (const ValidateMessage message = validate_message(); message != nullptr) {
        const std::string& language = message_language();
        return message(refusal.code, refusal.names, values, count, language.empty() ? "UsEnglish" : language.c_str());
    }
    std::string text = "L" + std::to_string(refusal.code);
    for (std::uint32_t i = 0; i < count; ++i) {
        text += std::string(" ") + (refusal.names[i] != nullptr ? refusal.names[i] : "?") + "=" + values[i];
    }
    return text;
}

// Z-10: a record kept for the Mods page, once per mod (the first reason is the one the log said).
void keep_not_started(NotStarted record) {
    const std::lock_guard<std::mutex> lock(g_not_started_mutex);
    for (const NotStarted& kept : g_not_started) {
        if (kept.mod_id == record.mod_id) return;
    }
    g_not_started.push_back(std::move(record));
}

// A LumaCore mod the game module took out while the game runs (reload.md 2; luma_set_taken_out, on the game thread):
// no longer started, so its Mods page reads on and not running, with why. The module closes its library.
void taken_out(const char* mod_id) {
    NotStarted record;
    record.mod_id = mod_id;
    record.sentence = "TAKEN OUT WHILE THE GAME RUNS";
    for (auto it = g_loaded.begin(); it != g_loaded.end(); ++it) {
        if ((*it)->mod_id == record.mod_id) {
            record.library = (*it)->library;
            g_loaded.erase(it);
            break;
        }
    }
    say(record.mod_id + " taken out while the game runs");
    keep_not_started(std::move(record));
}

// A mod's code did not start for a reason the host words: `logged` to the log as before, `sentence` (host capitals,
// a row of the text table) for the Mods page.
void not_started(const PluginFile& file, const std::string& logged, std::string sentence) {
    say(logged);
    NotStarted record;
    record.mod_id = file.mod_id;
    record.library = file.path.filename().string();
    record.sentence = std::move(sentence);
    keep_not_started(std::move(record));
}

// A mod's code was refused by the loader or the order: the refusal said in the played language, and kept whole.
void not_started(const PluginFile& file, const LumaRefusal& refusal) {
    // An L506 for a permission the player declined is their choice, not a fault: the log says so (consent 2.3).
    std::string declined;
    if (refusal.code == 506u) {
        const std::uint32_t named = refusal.count < LUMA_REFUSAL_MOST ? refusal.count : LUMA_REFUSAL_MOST;
        for (std::uint32_t i = 0; i < named; ++i) {
            if (refusal.names[i] == nullptr || refusal.values[i] == nullptr || std::string_view(refusal.names[i]) != "permission") continue;
            std::string_view permission(refusal.values[i]);
            if (permission.substr(0, 2) == "T:") permission.remove_prefix(2);
            if (std::find(file.declined.begin(), file.declined.end(), permission) != file.declined.end()) declined = permission;
        }
    }
    say(declined.empty() ? refusal_text(refusal) : file.mod_id + " is off: the player did not allow " + declined);
    NotStarted record;
    record.mod_id = file.mod_id;
    record.library = file.path.filename().string();
    record.code = refusal.code;
    const std::uint32_t count = refusal.count < LUMA_REFUSAL_MOST ? refusal.count : LUMA_REFUSAL_MOST;
    for (std::uint32_t i = 0; i < count; ++i) {
        record.values.emplace_back(refusal.names[i] != nullptr ? refusal.names[i] : "?", refusal.values[i] != nullptr ? refusal.values[i] : "");
    }
    keep_not_started(std::move(record));
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

// load_error()'s words as the Mods page says them: the three a mod's author meets by their rows (on Linux and Android
// read from the dynamic loader's message: glibc's and bionic's), a Windows error by its number, anything else as "did
// not load", the loader's own words in the log.
std::string load_sentence(const std::string& why) {
    const auto has = [&why](const char* part) { return why.find(part) != std::string::npos; };
    if (why == "a library it needs isn't there" || has("cannot open shared object file") || (has("library \"") && has("\" not found"))) {
        return "ITS CODE DID NOT START. A LIBRARY IT NEEDS ISN'T THERE";
    }
    if (why == "it was built for another version of the game" || has("undefined symbol") || has("cannot locate symbol")) {
        return "ITS CODE DID NOT START. IT WAS BUILT FOR ANOTHER VERSION OF THE GAME";
    }
    if (why == "it is not for this kind of computer" || has("wrong ELF class") || has("invalid ELF header") || has("bad ELF magic") ||
        has(" instead of EM_") || has("unexpected e_machine")) {
        return "ITS CODE DID NOT START. IT IS NOT FOR THIS KIND OF COMPUTER";
    }
    if (why.starts_with("error ")) return "ITS CODE DID NOT LOAD (ERROR " + why.substr(6) + ")";
    return "ITS CODE DID NOT LOAD";
}

}  // namespace

std::size_t start_plugins(const std::vector<PluginFile>& plugins, const ModRows& rows, const NebulaModApi* game_api,
                          void* (*find_symbol)(const char*),
                          void (*log)(const char*), void (*choose)(const std::vector<ChosenLayers>& chosen)) {
    g_log = log;
    g_find_symbol = find_symbol;
    // LumaCore.service: the module asks the host, which asks its extensions (the harness's own service while a test runs).
    // An extension's service (the harness's) needs no permission; phase 2's services name theirs here (loader D3-2).
    using ServiceFinder = const void* (*)(const char*, std::uint32_t, std::uint32_t, const char**);
    if (const auto give = reinterpret_cast<void (*)(ServiceFinder)>(find_symbol != nullptr ? find_symbol("luma_set_service_finder") : nullptr)) {
        give([](const char* name, std::uint32_t major, std::uint32_t minor, const char** permission) -> const void* {
            if (permission != nullptr) *permission = nullptr;
            return galaxy::extensions::service(name, major, minor);
        });
    }
    // A mod taken out while the game runs (reload.md 2) is told to the host, which forgets it as started.
    if (const auto told = reinterpret_cast<LumaSetTakenOut>(find_symbol != nullptr ? find_symbol("luma_set_taken_out") : nullptr)) {
        told([](const char* mod_id) { taken_out(mod_id); });
    }
    // The guard (plan 1.6): its fault handler before any mod's code runs. Off, a fault in a mod's code reaches the crash
    // report, which names the mod.
    if (!plugins.empty() && !galaxy::luma_guard::install(find_symbol)) {
        if (g_log != nullptr) g_log("mods: the guard is off (the game module or the system did not take it); a fault in a mod's code ends the game with its report");
    }
    // LumaCore's services: the module's own, the host's, then the extensions' test ones (services-registry 13).
    if (!plugins.empty()) {
        galaxy::luma_services::provide_all(find_symbol, log, {});
    }
    if (plugins.empty() && rows.empty()) return 0;
    if (game_api == nullptr || game_api->size < sizeof(NebulaModApi)) {
        say("the game module has no plugin interface");
        return 0;
    }
    g_api = *game_api;
    // The rows of the layers in use: after every mod's layers call was answered and the host kept the start going.
    const auto apply_rows = [&] {
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
        // The tunables lift is gone (plan 1.5): a number is named by LumaCore's number() and set by set_number() from a
        // mod's code. Each old row is said, never dropped unsaid.
        for (const TunableToSet& tunable : rows.tunables) {
            say(tunable.mod_id + ": " + tunable.key + " is not set: tuning numbers are set by a mod's code (LumaCore set_number)");
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
        // Power-ups from data.
        for (const FormToAdd& row : rows.forms) {
            NebulaPowerUpClass form{};
            form.size = sizeof(NebulaPowerUpClass);
            form.name = row.name.c_str();
            form.frames = row.frames;
            form.music = row.music.empty() ? nullptr : row.music.c_str();
            form.color_frame = row.color_frame;
            form.model = row.model.empty() ? nullptr : row.model.c_str();
            form.looks_like = row.looks_like;
            form.acts_like = row.acts_like;
            if (g_api.add_power_up(&form) < 0) {
                say(row.mod_id + ": the form " + row.name + " can't be added");
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
    };
    {
        const std::lock_guard<std::mutex> lock(g_not_started_mutex);
        g_not_started.clear();
    }
    // The needs order (loader D3-3): a mod's needed mods start before it; otherwise the list's order. `index` stays the
    // mod's place in the list, its order for hooks and who wins. A cycle starts none of its mods, each named (L800).
    std::vector<std::size_t> sequence;
    std::vector<bool> placed(plugins.size(), false);
    const auto listed = [&plugins](const std::string& id) {
        return std::any_of(plugins.begin(), plugins.end(), [&id](const PluginFile& p) { return p.mod_id == id; });
    };
    const auto ready = [&](std::size_t i) {
        for (const std::string& need : plugins[i].needs) {
            if (!listed(need)) continue;  // the host's own check (mods.cpp) has said it is missing
            for (std::size_t j = 0; j < plugins.size(); ++j) {
                if (plugins[j].mod_id == need && j != i && !placed[j]) return false;  // a need on itself orders nothing
            }
        }
        return true;
    };
    for (bool more = true; more;) {
        more = false;
        for (std::size_t i = 0; i < plugins.size(); ++i) {
            if (placed[i] || !ready(i)) continue;
            placed[i] = true;
            sequence.push_back(i);
            more = true;
            break;  // again from the top, so the list's order holds wherever the needs allow
        }
    }
    // The shortest cycle of needs from plugins[own] back to it among the listed mods, as the validator's L800 names it
    // (luma-validate need_cycle: breadth first, each mod's needs in its own order): the ids with the mod first and
    // last, or none.
    const auto cycle_through = [&plugins](std::size_t own) {
        const std::string& id = plugins[own].mod_id;
        const auto other_mod = [&plugins, &id](const std::string& want) -> const PluginFile* {
            if (want == id) return nullptr;
            for (const PluginFile& p : plugins) {
                if (p.mod_id == want) return &p;
            }
            return nullptr;
        };
        std::vector<std::pair<std::string, std::string>> from;  // each id reached, with the id it was reached from
        const auto reached_from = [&from](const std::string& want) -> const std::string* {
            for (const auto& f : from) {
                if (f.first == want) return &f.second;
            }
            return nullptr;
        };
        std::vector<std::string> queue;
        for (const std::string& n : plugins[own].needs) {
            if (other_mod(n) != nullptr && reached_from(n) == nullptr) {
                from.emplace_back(n, id);
                queue.push_back(n);
            }
        }
        for (std::size_t q = 0; q < queue.size(); ++q) {
            const std::string at = queue[q];
            for (const std::string& n : other_mod(at)->needs) {
                if (n == id) {
                    std::vector<std::string> chain{id, at};
                    for (std::string walk = at; *reached_from(walk) != id; walk = chain[1]) {
                        chain.insert(chain.begin() + 1, *reached_from(walk));
                    }
                    chain.push_back(id);
                    return chain;
                }
                if (other_mod(n) != nullptr && reached_from(n) == nullptr) {
                    from.emplace_back(n, at);
                    queue.push_back(n);
                }
            }
        }
        return std::vector<std::string>{};
    };
    for (std::size_t i = 0; i < plugins.size(); ++i) {
        if (placed[i]) continue;
        const std::vector<std::string> chain = cycle_through(i);
        if (chain.empty()) {
            // Not in a cycle: it needs a mod that is in one, or that needs one (its own line names that mod).
            std::string other;
            for (const std::string& need : plugins[i].needs) {
                for (std::size_t j = 0; j < plugins.size() && other.empty(); ++j) {
                    if (plugins[j].mod_id == need && !placed[j]) other = need;
                }
            }
            not_started(plugins[i], plugins[i].mod_id + " (" + plugins[i].path.filename().string() + ") not started: it needs " + other +
                ", which did not start", "ITS CODE DID NOT START. IT NEEDS A MOD THAT DID NOT START: " + other);
            continue;
        }
        // Said as the validator says L800 for Validator.Need.Cycle, values "T:" a literal and "P:" a phrase as the
        // loader's: other the next mod in the cycle, expected empty, chain "a -> b -> a".
        std::string joined;
        for (const std::string& id : chain) joined += (joined.empty() ? "" : " -> ") + id;
        LumaRefusal cycle{};
        const std::string mod = "T:" + plugins[i].mod_id;
        const std::string them = "T:" + chain[1];
        const std::string circle = "T:" + joined;
        cycle.code = 800;
        cycle.names[0] = "mod";
        cycle.values[0] = mod.c_str();
        cycle.names[1] = "file";
        cycle.values[1] = "T:luma.json";
        cycle.names[2] = "other";
        cycle.values[2] = them.c_str();
        cycle.names[3] = "expected";
        cycle.values[3] = "T:";
        cycle.names[4] = "found";
        cycle.values[4] = "P:Need.Cycle";
        cycle.names[5] = "chain";
        cycle.values[5] = circle.c_str();
        cycle.count = 6;
        not_started(plugins[i], cycle);
    }
    // Step 1, in the start order: each mod bound and begun, then its layers call asked; none starts yet. A mod waits
    // for its start with its library open; a later mod binds to its exports.
    struct Begun {
        std::size_t index;
        const LumaModInfo* info;
        void* module;
        std::string library;
    };
    const auto begin = reinterpret_cast<LumaBeginMod>(g_find_symbol != nullptr ? g_find_symbol("luma_begin_mod") : nullptr);
    const auto answered = reinterpret_cast<LumaModLayers>(g_find_symbol != nullptr ? g_find_symbol("luma_mod_layers") : nullptr);
    const auto start = reinterpret_cast<LumaStartBegun>(g_find_symbol != nullptr ? g_find_symbol("luma_start_begun") : nullptr);
    const auto drop = reinterpret_cast<LumaDropBegun>(g_find_symbol != nullptr ? g_find_symbol("luma_drop_begun") : nullptr);
    std::vector<Begun> begun;
    const auto has_begun = [&](const std::string& id) {
        return std::any_of(begun.begin(), begun.end(), [&](const Begun& b) { return plugins[b.index].mod_id == id; });
    };
    for (const std::size_t index : sequence) {
        const PluginFile& file = plugins[index];
        const std::string name = file.mod_id + " (" + file.path.filename().string() + ")";
        // A needed mod that did not begin leaves this one off, named (its imports from it could not bind).
        std::string missing;
        for (const std::string& need : file.needs) {
            if (listed(need) && !has_begun(need)) {
                missing = need;
                break;
            }
        }
        if (!missing.empty()) {
            not_started(file, name + " not started: it needs " + missing + ", which did not start",
                        "ITS CODE DID NOT START. IT NEEDS A MOD THAT DID NOT START: " + missing);
            continue;
        }
        const std::filesystem::path running = running_copy(file.path, index);
        if (file.code.empty() || code_hash(running) != file.code) {
            if (file.code.empty()) {
                not_started(file, name + " not started: its code can't be read", "ITS CODE DID NOT START. ITS CODE CAN'T BE READ");
            } else {
                not_started(file, name + " not started: its code changed after it was allowed",
                            "ITS CODE DID NOT START. ITS CODE CHANGED AFTER IT WAS ALLOWED");
            }
            continue;
        }
        void* const module = open_library(running);
        if (module == nullptr) {
            const std::string why = load_error();
            not_started(file, name + " not started: " + why, load_sentence(why));
            continue;
        }
        // A LumaCore mod (luma.h): the game binds what it lists by name, whole or not at all. `plugins` is top of the
        // list first, so `index` is the mod's order: 0 binds first, holds a replace, runs its befores first and its
        // afters last, and its wrap is outermost (loader contract 1). From the call on, the game closes the library.
        if (const void* const info = symbol_in(module, "luma_mod"); info != nullptr) {
            if (begin == nullptr || answered == nullptr || start == nullptr || drop == nullptr) {
                // The game module is this build's: it always has the loader. Said, never silent.
                not_started(file, name + " not started: the game module has no luma_begin_mod", "ITS CODE DID NOT START");
                close_library(module);
                continue;
            }
            LumaRefusal refusal{};
            const std::string library = file.path.filename().string();
            // The permissions the player granted, for the declarations as they are now (consent 1.4): a permissioned
            // service the mod imports without one is refused by name, never allowed by omission (loader D3-2).
            std::vector<const char*> granted;
            granted.reserve(file.granted.size());
            for (const std::string& permission : file.granted) granted.push_back(permission.c_str());
            if (begin(static_cast<const LumaModInfo*>(info), file.mod_id.c_str(), library.c_str(), static_cast<std::int32_t>(index), module,
                      close_luma_library, granted.data(), static_cast<std::uint32_t>(granted.size()), &refusal) == 0) {
                if (refusal.code != 0) {
                    not_started(file, refusal);
                } else {
                    not_started(file, name + " not started: the loader gave no reason", "ITS CODE DID NOT START");
                }
                continue;
            }
            begun.push_back({index, static_cast<const LumaModInfo*>(info), module, library});
            continue;
        }
        // Code that is not a LumaCore mod (no luma_mod): the old plugin entry, nebula_plugin_start, is gone (plan 1.10a).
        not_started(file, name + " not started: it is not a LumaCore mod (no luma_mod); rebuild it with luma.h",
                    "ITS CODE DID NOT START. IT WAS BUILT FOR AN OLDER NEBULA");
        close_library(module);
    }
    // Between the steps: what each begun mod's layers call answered, for the host, which may start the game again here
    // (its content was cooked with other layers). Then the rows.
    if (choose != nullptr) {
        std::vector<ChosenLayers> chosen;
        for (const Begun& b : begun) {
            ChosenLayers one{&plugins[b.index], {}};
            std::uint64_t bytes = 0;
            const char* const names = answered(b.info, &bytes);
            for (std::uint64_t at = 0; names != nullptr && at < bytes;) {
                const std::string_view name(names + at);
                one.names.emplace_back(name);
                at += name.size() + 1u;
            }
            chosen.push_back(std::move(one));
        }
        choose(chosen);
    }
    apply_rows();
    // Step 2, in the same order: each begun mod's start. One whose needed mod did not start is dropped, named, as a
    // refused start is.
    std::vector<std::string> started;
    for (const Begun& b : begun) {
        const PluginFile& file = plugins[b.index];
        const std::string name = file.mod_id + " (" + file.path.filename().string() + ")";
        std::string missing;
        for (const std::string& need : file.needs) {
            if (listed(need) && std::find(started.begin(), started.end(), need) == started.end()) {
                missing = need;
                break;
            }
        }
        if (!missing.empty()) {
            drop(b.info);
            not_started(file, name + " not started: it needs " + missing + ", which did not start",
                        "ITS CODE DID NOT START. IT NEEDS A MOD THAT DID NOT START: " + missing);
            continue;
        }
        LumaRefusal refusal{};
        if (start(b.info, &refusal) == 0) {
            if (refusal.code != 0) {
                not_started(file, refusal);
            } else {
                not_started(file, name + " not started: the loader gave no reason", "ITS CODE DID NOT START");
            }
            continue;
        }
        auto loaded = std::make_unique<Loaded>();
        loaded->mod_id = file.mod_id;
        loaded->library = b.library;
        loaded->version = file.version;
        loaded->options = file.options;
        loaded->module = b.module;
        g_loaded.push_back(std::move(loaded));
        started.push_back(file.mod_id);
        say(name + " started");
    }
    return g_loaded.size();
}

bool take_out(std::string_view mod_id) {
    const auto request = reinterpret_cast<LumaTakeOutRequest>(g_find_symbol != nullptr ? g_find_symbol("luma_take_out_request") : nullptr);
    if (request == nullptr) {
        say(std::string(mod_id) + " not taken out: the game module has no luma_take_out_request");
        return false;
    }
    const std::string id(mod_id);
    return request(id.c_str()) != 0;
}

std::vector<NotStarted> plugins_not_started() {
    const std::lock_guard<std::mutex> lock(g_not_started_mutex);
    return g_not_started;
}

NotStartedText plugin_not_started_text(std::string_view mod_id) {
    NotStarted record;
    {
        const std::lock_guard<std::mutex> lock(g_not_started_mutex);
        const auto it = std::find_if(g_not_started.begin(), g_not_started.end(), [&](const NotStarted& r) { return r.mod_id == mod_id; });
        if (it == g_not_started.end()) return {};
        record = *it;
    }
    if (record.code == 0) return {record.sentence, false};
    // The refusal as the validator words it, in the played language (set_message_language): already the player's.
    LumaRefusal refusal{};
    refusal.code = static_cast<std::uint16_t>(record.code);
    for (const auto& [name, value] : record.values) {
        if (refusal.count == LUMA_REFUSAL_MOST) break;
        refusal.names[refusal.count] = name.c_str();
        refusal.values[refusal.count++] = value.c_str();
    }
    return {refusal_text(refusal), true};
}

ValidatorText validator_text(std::uint32_t code, const std::vector<std::pair<std::string, std::string>>& values,
                             std::string_view language) {
    const char* names[LUMA_REFUSAL_MOST] = {};
    const char* texts[LUMA_REFUSAL_MOST] = {};
    std::uint32_t count = 0;
    for (const auto& [name, value] : values) {
        if (count == LUMA_REFUSAL_MOST) break;
        names[count] = name.c_str();
        texts[count++] = value.c_str();
    }
    if (const ValidateMessage message = validate_message(); message != nullptr) {
        const std::string folder = !language.empty() ? std::string(language)
                                   : !message_language().empty() ? message_language() : std::string("UsEnglish");
        const char* const words = message(static_cast<std::uint16_t>(code), names, texts, count, folder.c_str());
        if (words != nullptr) return {words, true};
    }
    std::string text = "L" + std::to_string(code);
    for (std::uint32_t i = 0; i < count; ++i) text += std::string(" ") + names[i] + "=" + texts[i];
    return {text, false};
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

void set_message_language(std::string folder) {
    message_language() = std::move(folder);
}

}  // namespace galaxy::mods
