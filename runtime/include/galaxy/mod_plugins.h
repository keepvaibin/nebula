// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// A .luma's code: native libraries the host loads next to the game module, through nebula_mod_api.h.

#include <nebula_mod_api.h>

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace galaxy::mods {

struct PluginFile {
    std::filesystem::path path;
    std::string mod_id;
    std::string version;
    std::map<std::string, std::string> options;  // each option's choice
};

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
};

// `game_api`: the game module's nebula_game_mod_api(); `find_symbol`: a name in the game module, or null. Each plugin that
// can't start is named in the log and left out; the game carries on without it. What's returned: their count.
std::size_t start_plugins(const std::vector<PluginFile>& plugins, const std::vector<GalaxyToAdd>& galaxies,
                          const std::vector<std::pair<std::string, std::string>>& texts, const NebulaModApi* game_api,
                          void* (*find_symbol)(const char*),
                          void (*log)(const char*));
// The plugin an address is in: its mod's id, or null.
[[nodiscard]] const char* plugin_at(const void* address) noexcept;

}  // namespace galaxy::mods
