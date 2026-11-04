// SPDX-License-Identifier: GPL-3.0-only
#include "galaxy/mod_plugins.h"

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
#else
    return -1;
#endif
}

void* api_find_function(const char* symbol) {
    return symbol != nullptr && g_find_symbol != nullptr ? g_find_symbol(symbol) : nullptr;
}

}  // namespace

std::size_t start_plugins(const std::vector<PluginFile>& plugins, const std::vector<GalaxyToAdd>& galaxies,
                          const std::vector<std::pair<std::string, std::string>>& texts, const NebulaModApi* game_api,
                          void* (*find_symbol)(const char*),
                          void (*log)(const char*)) {
    g_log = log;
    g_find_symbol = find_symbol;
    if (plugins.empty() && galaxies.empty() && texts.empty()) return 0;
    if (game_api == nullptr || game_api->size < sizeof(NebulaModApi)) {
        say("the game module has no plugin interface");
        return 0;
    }
    g_api = *game_api;
    g_api.log = api_log;
    g_api.option = api_option;
    g_api.hook = api_hook;
    g_api.find_function = api_find_function;
    for (const auto& [id, words] : texts) {
        g_api.add_text(id.c_str(), words.c_str());
    }
    for (const GalaxyToAdd& galaxy : galaxies) {
        NebulaGalaxy row{};
        row.size = sizeof(row);
        row.name = galaxy.name.c_str();
        row.map_pane = galaxy.map_pane.empty() ? nullptr : galaxy.map_pane.c_str();
        std::vector<std::string> conditions = galaxy.open_conditions;
        if (galaxy.hungry_slot >= 0 && conditions.size() < 3u) conditions.push_back("StarPiece" + galaxy.name);
        for (std::size_t i = 0; i < conditions.size(); ++i) row.open_condition[i] = conditions[i].c_str();
        row.stars_to_open = galaxy.stars_to_open;
        row.dome = galaxy.dome;
        if (g_api.add_galaxy(&row) != 0) say(galaxy.mod_id + ": the galaxy " + galaxy.name + " can't be added: its name clashes with one the game has");
        if (galaxy.hungry_slot >= 0 && g_api.add_hungry_luma(galaxy.name.c_str(), galaxy.hungry_slot, galaxy.hungry_star_bits) != 0) {
            say(galaxy.mod_id + ": the hungry Luma for " + galaxy.name + " can't be added");
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
