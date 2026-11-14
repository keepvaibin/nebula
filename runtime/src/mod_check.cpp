// SPDX-License-Identifier: GPL-3.0-only
// The validator as the game calls it (mod_check.h).

#include "mod_check.h"

#include "galaxy/asset_store.h"
#include "galaxy/luma_game_store.h"
#include "galaxy/mods.h"
#include "galaxy/platform_paths.h"

#include <luma.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif !defined(__APPLE__)
#include <dlfcn.h>
#endif

namespace galaxy::modlog::check {

const Validator* validator() {
    static Validator found;
    static bool looked = false;
    static bool ok = false;
    if (looked) return ok ? &found : nullptr;
    looked = true;
#if defined(__APPLE__)
    found = {&luma_validate, &luma_report_state, &luma_report_invalid, &luma_report_count, &luma_report_at,
             &luma_report_kept, &luma_report_free, &luma_report_read_kept, &luma_report_id, &luma_report_after_start};
#else
#if defined(_WIN32)
    void* const library =
        LoadLibraryExW(L"nebula_import.dll", nullptr, LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    const auto symbol = [library](const char* name) {
        return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(library), name));
    };
#else
    void* const library = dlopen("libnebula_import.so", RTLD_NOW | RTLD_LOCAL);
    const auto symbol = [library](const char* name) { return dlsym(library, name); };
#endif
    if (library == nullptr) {
        std::fprintf(stderr, "[modlog] the check did not run: the importer's library did not load\n");
        return nullptr;
    }
    found.validate = reinterpret_cast<decltype(found.validate)>(symbol("luma_validate"));
    found.state = reinterpret_cast<decltype(found.state)>(symbol("luma_report_state"));
    found.invalid = reinterpret_cast<decltype(found.invalid)>(symbol("luma_report_invalid"));
    found.count = reinterpret_cast<decltype(found.count)>(symbol("luma_report_count"));
    found.at = reinterpret_cast<decltype(found.at)>(symbol("luma_report_at"));
    found.kept = reinterpret_cast<decltype(found.kept)>(symbol("luma_report_kept"));
    found.release = reinterpret_cast<decltype(found.release)>(symbol("luma_report_free"));
    found.read_kept = reinterpret_cast<decltype(found.read_kept)>(symbol("luma_report_read_kept"));
    found.id = reinterpret_cast<decltype(found.id)>(symbol("luma_report_id"));
    found.after_start = reinterpret_cast<decltype(found.after_start)>(symbol("luma_report_after_start"));
#endif
    ok = found.validate != nullptr && found.state != nullptr && found.invalid != nullptr && found.count != nullptr &&
         found.at != nullptr && found.kept != nullptr && found.release != nullptr;
    if (!ok) std::fprintf(stderr, "[modlog] the check did not run: the importer's library lacks luma_validate\n");
    return ok ? &found : nullptr;
}

std::string store_key(const galaxy::assets::AssetStore& store) {
    return "store " + std::to_string(galaxy::assets::kStoreVersion) + " " + std::string(store.game_id());
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

namespace {

// The self-description folder read as luma-validate's Names::from_rows_folder reads it (LumaLink's --names): false,
// with why, when it holds a second types file or services catalog, or a file it names does not read.
bool read_description(const std::filesystem::path& folder, Description& out, std::string& why) {
    std::error_code error;
    std::vector<std::filesystem::path> rows;
    std::filesystem::path types;
    std::filesystem::path services;
    for (std::filesystem::recursive_directory_iterator it(folder, error), end; !error && it != end; it.increment(error)) {
        if (!it->is_regular_file(error)) continue;
        const std::filesystem::path& path = it->path();
        const std::filesystem::path name = path.filename();
        if (path.extension() == ".rows") {
            rows.push_back(path);
        } else if (name == "luma_types.json" || name == "luma_services.json") {
            std::filesystem::path& one = name == "luma_types.json" ? types : services;
            if (!one.empty()) {
                why = "a second " + name.string() + " under " + folder.string();
                return false;
            }
            one = path;
        }
    }
    if (error) {
        why = folder.string() + ": " + error.message();
        return false;
    }
    std::sort(rows.begin(), rows.end(),
              [](const std::filesystem::path& a, const std::filesystem::path& b) { return a.generic_string() < b.generic_string(); });
    const auto read = [&why](const std::filesystem::path& path, std::string& into) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            why = path.string() + " cannot be read";
            return false;
        }
        std::ostringstream text;
        text << in.rdbuf();
        into = text.str();
        return true;
    };
    for (const std::filesystem::path& path : rows) {
        std::string lines;
        if (!read(path, lines)) return false;
        if (!lines.empty() && lines.back() != '\n') lines.push_back('\n');
        out.names += lines;
    }
    if (!types.empty() && !read(types, out.types)) return false;
    if (!services.empty() && !read(services, out.services)) return false;
    const std::filesystem::path placement = folder / "smg_game.luma-placement.json";
    if (std::filesystem::is_regular_file(placement, error) && !read(placement, out.placement)) return false;
    return true;
}

}  // namespace

const Description& description() {
    static const Description found = [] {
        Description d;
        const std::filesystem::path program = galaxy::program_directory();
        const std::filesystem::path folder = program.empty() ? std::filesystem::path{} : program / "luma";
        std::error_code error;
        std::string why;
        if (folder.empty() || !std::filesystem::is_directory(folder, error)) {
            // A development build or a device with no self-description: as before, those rules go unchecked.
        } else if (!read_description(folder, d, why) || d.names.empty()) {
            if (why.empty()) why = "no function rows under " + folder.string();
            std::fprintf(stderr, "[modlog] the game's self-description is not used: %s\n", why.c_str());
            d = Description{};
        }
        // FNV-1a over each part and its length: a game of other rows, types, services or facts checks every rule again.
        std::uint64_t h = 0xcbf29ce484222325ull;
        const auto mix = [&h](std::string_view bytes) {
            for (const char c : bytes) h = (h ^ static_cast<unsigned char>(c)) * 0x100000001b3ull;
        };
        for (const std::string* part : {&d.names, &d.types, &d.services, &d.placement}) {
            mix(std::to_string(part->size()) + ":");
            mix(*part);
        }
        char hex[17];
        std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(h));
        d.key = d.names.empty() ? std::string("-") : std::string(hex);
        return d;
    }();
    return found;
}

bool validate_package(const Validator* v, std::string_view id, const std::filesystem::path& folder, const Game& game,
                      const std::vector<LumaInstalledMod>& installed, std::uint32_t position, const std::string& kept_in,
                      std::string& kept_out, std::vector<Kept>* findings) {
    if (v == nullptr) return false;
    const std::string path = folder.string();
    LumaValidateInput input{};
    input.size = sizeof(input);
    input.path = path.c_str();
    input.platform = game.platform.c_str();
    input.nebula = NEBULA_VERSION;
    input.region = game.region.empty() ? nullptr : game.region.c_str();
    input.luma_api = LUMA_API;
    input.installed = installed.empty() ? nullptr : installed.data();
    input.installed_count = static_cast<std::uint32_t>(installed.size());
    input.position = position;
    input.kept = kept_in.empty() ? nullptr : reinterpret_cast<const std::uint8_t*>(kept_in.data());
    input.kept_size = kept_in.size();
    // The game's self-description, as LumaLink's check reads it: the code's names and types, the services catalog, and
    // the factory's classes, so only a placement of a class no list names waits for the check after the mods' start.
    // The validator takes the others only with the names.
    const Description& self = description();
    if (!self.names.empty()) {
        input.names = self.names.data();
        input.names_size = self.names.size();
        if (!self.types.empty()) {
            input.types = self.types.data();
            input.types_size = self.types.size();
        }
        if (!self.services.empty()) {
            input.services = self.services.data();
            input.services_size = self.services.size();
        }
        if (!self.placement.empty()) {
            input.placement = self.placement.data();
            input.placement_size = self.placement.size();
        }
    }
    // The game's own files, through a view for this call alone.
    std::optional<galaxy::LumaGameStoreView> view;
    std::string key;
    if (game.store != nullptr) {
        view.emplace(*game.store);
        key = store_key(*game.store);
        input.store = view->get();
        input.store_key = key.c_str();
    }
    LumaReport* report = v->validate(&input);
    if (v->state(report) < 0 && input.names != nullptr) {
        // The self-description is the game's own: one the validator does not take (a file of another release) is a
        // fault of the install, said; the mod is still checked as without it.
        const char* why = v->invalid(report);
        std::fprintf(stderr, "[modlog] %.*s: checked without the game's self-description, which the validator refuses: %s\n",
                     static_cast<int>(id.size()), id.data(), why != nullptr ? why : "?");
        v->release(report);
        input.names = nullptr;
        input.names_size = 0;
        input.types = nullptr;
        input.types_size = 0;
        input.services = nullptr;
        input.services_size = 0;
        input.placement = nullptr;
        input.placement_size = 0;
        report = v->validate(&input);
    }
    if (v->state(report) < 0) {
        const char* why = v->invalid(report);
        std::fprintf(stderr, "[modlog] %.*s: the check did not run: %s\n", static_cast<int>(id.size()), id.data(),
                     why != nullptr ? why : "?");
        v->release(report);
        return false;
    }
    std::uint64_t size = 0;
    const std::uint8_t* bytes = v->kept(report, &size);
    kept_out.assign(reinterpret_cast<const char*>(bytes), bytes != nullptr ? static_cast<std::size_t>(size) : 0u);
    if (findings != nullptr) {
        findings->clear();
        const std::uint32_t count = v->count(report);
        for (std::uint32_t i = 0; i < count; ++i) {
            Kept k;
            const LumaFinding* f = v->at(report, i, "UsEnglish");
            if (f == nullptr) break;
            k.code = f->code;
            k.level = f->level;
            k.line = f->line;
            k.column = f->column;
            k.file = f->file != nullptr ? f->file : "";
            k.english = f->text != nullptr ? f->text : "";
            f = v->at(report, i, game.played.c_str());
            k.played = f != nullptr && f->text != nullptr ? f->text : k.english;
            findings->push_back(std::move(k));
        }
    }
    v->release(report);
    return true;
}

ImportCheck import(const std::filesystem::path& folder, const std::vector<StartPackage>& others, const Game& game,
                   const std::function<bool(std::string_view)>& refused_at_start) {
    ImportCheck out;
    const Validator* const v = validator();
    if (v == nullptr) return out;
    // The packages placed now, each by its kept report: the last start's, else (placed since, or before any start's
    // check) one made now, alone. On as the list has it, but off when it would not start: refused at the last start, a
    // report made now with an Error, or a report naming no id (the validator takes those only off).
    const std::filesystem::path validate = galaxy::mods::mods_folder() / ".cooked" / "validate";
    std::vector<std::string> ids;
    std::vector<std::string> reports;
    std::vector<bool> on;
    for (const StartPackage& p : others) {
        std::string report = read_file(validate / (p.id + ".kept"));
        bool starts = p.on && !(refused_at_start && refused_at_start(p.id));
        if (report.empty()) {
            std::vector<Kept> alone;
            if (!validate_package(v, p.id, p.folder, game, {}, UINT32_MAX, {}, report, &alone) || report.empty()) continue;
            starts = starts && std::none_of(alone.begin(), alone.end(), [](const Kept& k) { return k.level == 0; });
        }
        if (starts && v->read_kept != nullptr && v->id != nullptr) {
            LumaReport* const read = v->read_kept(reinterpret_cast<const std::uint8_t*>(report.data()), report.size());
            const char* const named = v->state(read) == 2 ? v->id(read) : "";
            starts = named != nullptr && named[0] != 0;
            v->release(read);
        }
        ids.push_back(p.id);
        reports.push_back(std::move(report));
        on.push_back(starts);
    }
    std::vector<LumaInstalledMod> installed(ids.size());
    for (std::size_t i = 0; i < ids.size(); ++i) {
        installed[i].file = ids[i].c_str();
        installed[i].on = on[i] ? 1u : 0u;
        installed[i].kept = reinterpret_cast<const std::uint8_t*>(reports[i].data());
        installed[i].kept_size = reports[i].size();
    }
    // One call at import (validator.md 2: no kept report of its own, every rule), last in the list, against them all.
    std::string kept;
    std::vector<Kept> findings;
    if (!validate_package(v, "import", folder, game, installed, UINT32_MAX, {}, kept, &findings)) return out;
    out.ran = true;
    for (const Kept& k : findings) {
        out.findings.push_back({k.code, k.level == 0, k.file, k.played, k.line, k.column});
        out.refused = out.refused || k.level == 0;
    }
    return out;
}

}  // namespace galaxy::modlog::check
