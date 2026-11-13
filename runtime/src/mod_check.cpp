// SPDX-License-Identifier: GPL-3.0-only
// The validator as the game calls it (mod_check.h).

#include "mod_check.h"

#include "galaxy/mods.h"

#include <luma.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
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
             &luma_report_kept, &luma_report_free, &luma_report_read_kept, &luma_report_id};
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
#endif
    ok = found.validate != nullptr && found.state != nullptr && found.invalid != nullptr && found.count != nullptr &&
         found.at != nullptr && found.kept != nullptr && found.release != nullptr;
    if (!ok) std::fprintf(stderr, "[modlog] the check did not run: the importer's library lacks luma_validate\n");
    return ok ? &found : nullptr;
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
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
    LumaReport* const report = v->validate(&input);
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
    // One call at import (validator 2: no kept report of its own, every rule), last in the list, against them all.
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
