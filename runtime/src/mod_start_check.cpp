// SPDX-License-Identifier: GPL-3.0-only
// The game's own parts a start with mods needs (contract start-check, section 2; mod_start_check.h).

#include "galaxy/mod_start_check.h"

#include "galaxy/asset_store.h"
#include "galaxy/mod_log.h"
#include "galaxy/render/mod_shaders.h"

#include <luma_modlog.h>

#include <fstream>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include "galaxy/windows_api.h"
#include "galaxy/windows_paths.h"
#elif !defined(__APPLE__)
#include <dlfcn.h>
#endif

#if defined(__APPLE__)
// The importer is linked into the program on Apple (platform/apple/metal.cmake).
extern "C" std::uint32_t nebula_import_store_version();
#endif

namespace galaxy::startcheck {
namespace {

namespace fs = std::filesystem;

using StoreVersion = std::uint32_t (*)();

// The placeholder data the game itself loads (contract references 4), as store paths. The cooker's own placeholders
// (the checker texture) are the cook's to check; the sound and the effect join this list with 3.7 and 3.5.
constexpr const char* kPlaceholders[] = {"/ObjectData/NebulaPlaceholder.arc"};

constexpr std::uint64_t kSpareBytes = 64ull * 1024ull * 1024ull;

// The importer: its library loads, it has the store version export, the version is this game's; on Windows the cook's
// program is beside the game too. "" when it is all there.
std::string importer_missing() {
    StoreVersion version = nullptr;
#if defined(__APPLE__)
    version = &nebula_import_store_version;
#elif defined(_WIN32)
    const fs::path folder = fs::path(windows::executable_path()).parent_path();
    std::error_code error;
    if (!fs::is_regular_file(folder / "nebula-import.exe", error)) {
        return "nebula-import.exe is not beside the game";
    }
    // As mod_plugins.cpp loads it: its own C runtime comes from System32.
    HMODULE const library =
        LoadLibraryExW(L"nebula_import.dll", nullptr, LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (library == nullptr) {
        return "nebula_import.dll did not load (error " + std::to_string(GetLastError()) + ")";
    }
    version = reinterpret_cast<StoreVersion>(reinterpret_cast<void*>(GetProcAddress(library, "nebula_import_store_version")));
#else
    void* const library = dlopen("libnebula_import.so", RTLD_NOW | RTLD_LOCAL);
    if (library == nullptr) {
        const char* const why = dlerror();
        return std::string("libnebula_import.so did not load (") + (why != nullptr ? why : "no reason given") + ")";
    }
    version = reinterpret_cast<StoreVersion>(dlsym(library, "nebula_import_store_version"));
#endif
    // The library stays loaded: the start's check and cook use it next.
    if (version == nullptr) {
        return "the importer has no nebula_import_store_version";
    }
    const std::uint32_t found = version();
    if (found != galaxy::assets::kStoreVersion) {
        return "the importer writes store version " + std::to_string(found) + ", the game reads " +
               std::to_string(galaxy::assets::kStoreVersion);
    }
    return {};
}

std::string placeholders_missing(const galaxy::assets::AssetStore& store) {
    for (const char* path : kPlaceholders) {
        if (!store.find_game(galaxy::assets::store_path(path))) {
            return std::string(path) + " is not in the game's store";
        }
    }
    return {};
}

// "214 MB" for Start.Part.Space's {needed}: whole megabytes, rounded up.
std::string megabytes(std::uint64_t bytes) {
    const std::uint64_t mb = (bytes + 1024ull * 1024ull - 1ull) / (1024ull * 1024ull);
    return std::to_string(mb) + " MB";
}

// The cook folder can be made and written, and has the room the plan needs plus 64 MiB.
std::string space_missing(const fs::path& folder, std::uint64_t cook_bytes, std::string& needed) {
    const std::uint64_t want = cook_bytes + kSpareBytes;
    needed = megabytes(want);
    std::error_code error;
    fs::create_directories(folder, error);
    if (error) {
        return "the cook folder " + folder.string() + " cannot be made (" + error.message() + ")";
    }
    const fs::path probe = folder / "write.probe";
    {
        std::ofstream out(probe, std::ios::binary | std::ios::trunc);
        out << "nebula\n";
        if (!out) {
            return "the cook folder " + folder.string() + " cannot be written";
        }
    }
    fs::remove(probe, error);
    const fs::space_info room = fs::space(folder, error);
    if (error) {
        return "the free space of " + folder.string() + " cannot be read (" + error.message() + ")";
    }
    if (room.available < want) {
        return "the cook folder has " + megabytes(room.available) + " free; the cook needs " + needed;
    }
    needed.clear();
    return {};
}

}  // namespace

std::optional<MissingPart> missing_part(const galaxy::assets::AssetStore& store, const fs::path& cook_folder,
                                        std::uint64_t cook_bytes) {
    MissingPart part;
    if (part.why = importer_missing(); !part.why.empty()) {
        part.row = "Start.Part.Importer";
        return part;
    }
    if (part.why = galaxy::render::mod_shader_compiler_missing(cook_folder); !part.why.empty()) {
        part.row = "Start.Part.Compiler";
        return part;
    }
    if (part.why = placeholders_missing(store); !part.why.empty()) {
        part.row = "Start.Part.Placeholders";
        return part;
    }
    std::string needed;
    if (part.why = space_missing(cook_folder, cook_bytes, needed); !part.why.empty()) {
        part.row = "Start.Part.Space";
        part.needed = std::move(needed);
        return part;
    }
    return std::nullopt;
}

void report(const MissingPart& part) {
    std::vector<std::pair<std::string, std::string>> values;
    values.emplace_back("part", std::string("R:") + (part.row != nullptr ? part.row : "Start.Part.Importer"));
    if (!part.needed.empty()) values.emplace_back("needed", part.needed);
    galaxy::modlog::add_row(LUMA_MODLOG_COOK, LUMA_MODLOG_ERROR, "nebula", 0u, "", "Start.PartMissing", values);
}

}  // namespace galaxy::startcheck
