// SPDX-License-Identifier: GPL-3.0-only
// The validator as the game calls it (validator 2): its C functions from the importer's library, one luma_validate
// call on a package, and the check at import against the packages placed. One copy for every caller: the start's
// and the import's checks in the game (mod_log.cpp), the mods tool (host_main.cpp, through mod_log.cpp) and the
// Android launcher's import (platform/sdl/mod_import_jni.cpp), so a mod is judged the same at every door.
#pragma once

#include "galaxy/mod_log.h"

#include "../../crates/nebula-import/include/luma_validate.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace galaxy::assets {
class AssetStore;
}

namespace galaxy::modlog::check {

// The validator's C functions, from the importer's library (as mod_plugins.cpp's validate_message finds it): on Apple
// the importer is linked into the program.
struct Validator {
    LumaReport* (*validate)(const LumaValidateInput*) = nullptr;
    std::int32_t (*state)(const LumaReport*) = nullptr;
    const char* (*invalid)(const LumaReport*) = nullptr;
    std::uint32_t (*count)(const LumaReport*) = nullptr;
    const LumaFinding* (*at)(const LumaReport*, std::uint32_t, const char*) = nullptr;
    const std::uint8_t* (*kept)(const LumaReport*, std::uint64_t*) = nullptr;
    void (*release)(LumaReport*) = nullptr;
    // A kept report read as it is, and the id it names (null: the package was refused). Optional: without them every
    // kept report counts as naming its id.
    LumaReport* (*read_kept)(const std::uint8_t*, std::uint64_t) = nullptr;
    const char* (*id)(const LumaReport*) = nullptr;
    // A report's after-start rows (references 3). Optional: without it no placement is checked after the start.
    std::uint32_t (*after_start)(const LumaReport*, std::uint32_t, const char**, const char**, std::uint32_t*) = nullptr;
};

// Null when the importer's library is missing or lacks luma_validate (said once on stderr).
const Validator* validator();

// What a check's findings and kept reports depend on beyond the package: the platform with its processor
// ("windows-x64", luma-format 6), the disc ("RMGE01"; empty: none, and the rules on the game's files go unchecked),
// the played language folder, and the game's own files (validator contract item 8: the rules on game paths, rows and
// folders, L302 and L4xx, as LumaLink's --game checks them; null: those rules go unchecked). Each check reads the
// store through a view of its own (luma_game_store.h), so checks on two threads never share one.
struct Game {
    std::string platform;
    std::string region;
    std::string played;
    const galaxy::assets::AssetStore* store = nullptr;
};

// What identifies a store's content for a kept report (LumaValidateInput.store_key): its version and the game it came
// from. A kept report made with another (or with none) is made again.
std::string store_key(const galaxy::assets::AssetStore& store);

// The game's self-description as LumaLink's check reads its --names folder, so every door judges a mod's code and
// placements as the tool does: every `.rows` file's lines (in path order), the types file (luma_types.json), the
// services catalog (luma_services.json) and the placement facts (smg_game.luma-placement.json, at the folder's top).
// Read once, from the `luma` folder beside the running program (an installed game's versions/<v>/luma); all empty when
// there is none or it does not read as one (said once on stderr): those rules then go unchecked, as before.
struct Description {
    std::string names;
    std::string types;
    std::string services;
    std::string placement;
    std::string key;  // what identifies them for a kept report: "-" when there is none
};
const Description& description();

// One finding as kept: code, level (0 an error), line, column, file, played text, English text.
struct Kept {
    std::uint32_t code = 0;
    std::uint32_t level = 0;
    std::uint32_t line = 0;
    std::uint32_t column = 0;
    std::string file;
    std::string played;
    std::string english;
};

// One luma_validate call on the package in `folder`: `installed` the other packages (with `kept_in` empty: every rule,
// alone), `position` its place among them (UINT32_MAX alone). Its kept report into `kept_out`, its findings (when
// asked) in English and the played language. False: the validator did not run (no library, or the package unreadable).
bool validate_package(const Validator* v, std::string_view id, const std::filesystem::path& folder, const Game& game,
                      const std::vector<LumaInstalledMod>& installed, std::uint32_t position, const std::string& kept_in,
                      std::string& kept_out, std::vector<Kept>* findings);

// A file's bytes; empty when it cannot be read.
std::string read_file(const std::filesystem::path& path);

// The check at import (validator 2: every rule) of the package staged in `folder`, last in the list, against the
// packages placed now (`others`), each by its kept report in the mods folder's .cooked/validate (else one made now,
// alone). A placed package counts as off when `refused_at_start(id)` (the last start refused it), when a report made
// now has an error, or when its report names no id.
ImportCheck import(const std::filesystem::path& folder, const std::vector<StartPackage>& others, const Game& game,
                   const std::function<bool(std::string_view)>& refused_at_start);

}  // namespace galaxy::modlog::check
