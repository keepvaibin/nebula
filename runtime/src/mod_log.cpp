// SPDX-License-Identifier: GPL-3.0-only
// The mod log's host half (contract mod-log 1, 3, 9; runtime/include/galaxy/mod_log.h).

#include "galaxy/mod_log.h"

#include "galaxy/host_extension.h"
#include "galaxy/mod_consent.h"
#include "galaxy/mod_plugins.h"
#include "galaxy/mods.h"

#include <luma_harness.h>
#include <luma_modlog.h>

#include <functional>
#include <vector>
#include <atomic>
#include <cstddef>
#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>

#include "galaxy/luma.h"
#include "galaxy/mods.h"
#include <luma.h>
#include "../../crates/nebula-import/include/luma_validate.h"

#if defined(_WIN32)
#include <windows.h>
#elif !defined(__APPLE__)
#include <dlfcn.h>
#endif

namespace galaxy::modlog {
namespace {

static_assert(kSourceCook == LUMA_MODLOG_COOK, "mod_log.h's kSourceCook is luma_modlog.h's");

std::atomic<LumaModLogAdd> g_add{nullptr};
// The page's reads (luma_modlog.h), found with g_add.
std::atomic<LumaModLogNext> g_next{nullptr};
std::atomic<LumaModLogNextMod> g_next_mod{nullptr};
std::atomic<LumaModLogWords> g_words{nullptr};
std::atomic<LumaModLogRowWords> g_row_words{nullptr};
// The world clock's page exports (services-time 11, native/include/luma_time.h), found with g_add; any thread.
using LumaTimeClockOf = std::int32_t (*)(const char* id, std::uint32_t* rate, std::int32_t* holds);
using LumaTimeLetGo = std::int32_t (*)(const char* id);
std::atomic<LumaTimeClockOf> g_clock_of{nullptr};
std::atomic<LumaTimeLetGo> g_let_go{nullptr};

// The harness's fact writer, when this run has the harness (its service exists only then).
LumaModLogFact harness_fact() {
    const auto* harness = static_cast<const LumaHarness*>(galaxy::extensions::service(LUMA_HARNESS_NAME, LUMA_HARNESS_MAJOR, 0));
    if (harness == nullptr || harness->size < offsetof(LumaHarness, fact) + sizeof(harness->fact)) return nullptr;
    return harness->fact;
}

// `text` as a JSON string, quotes included.
void append_json(std::string& out, std::string_view text) {
    out += '"';
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) < 0x20u) {
            char escaped[8];
            std::snprintf(escaped, sizeof(escaped), "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
            out += escaped;
        } else {
            out += c;
        }
    }
    out += '"';
}

void add(const LumaModLogEntry& entry) {
    if (const LumaModLogAdd give = g_add.load(std::memory_order_acquire)) give(&entry);
}

// after_begin's queue: what waits for begin, and whether it has run. Under g_after_mutex.
std::mutex g_after_mutex;
bool g_begun = false;
std::vector<std::function<void()>> g_after;

// begin's end, whatever the module has: what waited runs now, in order.
void begun() {
    std::vector<std::function<void()>> waiting;
    {
        const std::lock_guard<std::mutex> lock(g_after_mutex);
        g_begun = true;
        waiting.swap(g_after);
    }
    for (std::function<void()>& then : waiting) then();
}

}  // namespace

void after_begin(std::function<void()> then) {
    {
        const std::lock_guard<std::mutex> lock(g_after_mutex);
        if (!g_begun) {
            g_after.push_back(std::move(then));
            return;
        }
    }
    then();
}

void begin(void* (*find_symbol)(const char*), std::uint32_t mods_on) {
    if (find_symbol == nullptr) {
        begun();
        return;
    }
    const auto session = reinterpret_cast<LumaModLogSession>(find_symbol("luma_modlog_session"));
    g_next.store(reinterpret_cast<LumaModLogNext>(find_symbol("luma_modlog_next")), std::memory_order_release);
    g_next_mod.store(reinterpret_cast<LumaModLogNextMod>(find_symbol("luma_modlog_next_mod")), std::memory_order_release);
    g_words.store(reinterpret_cast<LumaModLogWords>(find_symbol("luma_modlog_words")), std::memory_order_release);
    g_row_words.store(reinterpret_cast<LumaModLogRowWords>(find_symbol("luma_modlog_row_words")), std::memory_order_release);
    g_add.store(reinterpret_cast<LumaModLogAdd>(find_symbol("luma_modlog_add")), std::memory_order_release);
    g_clock_of.store(reinterpret_cast<LumaTimeClockOf>(find_symbol("luma_time_clock_of")), std::memory_order_release);
    g_let_go.store(reinterpret_cast<LumaTimeLetGo>(find_symbol("luma_time_let_go")), std::memory_order_release);
    if (session != nullptr) session(mods_on, harness_fact());
    begun();
}

void add_finding(std::uint32_t source, std::uint32_t level, std::string_view mod, std::uint32_t code, std::string_view file,
                 std::uint32_t line, std::uint32_t column, const std::vector<std::pair<std::string, std::string>>& values) {
    if (g_add.load(std::memory_order_acquire) == nullptr) return;
    const galaxy::mods::ValidatorText played = galaxy::mods::validator_text(code, values);
    const galaxy::mods::ValidatorText english = galaxy::mods::validator_text(code, values, "UsEnglish");
    const std::string mod_id(mod);
    const std::string place(file);
    LumaModLogEntry entry{};
    entry.size = sizeof(entry);
    entry.source = source;
    entry.level = level;
    entry.code = code;
    entry.mod = mod_id.c_str();
    entry.file = place.c_str();
    entry.line = line;
    entry.column = column;
    for (const auto& [name, value] : values) {
        if (entry.count == LUMA_MODLOG_ARGS) {
            entry.count++;  // the log says once that the rest are not shown (ModLog.ArgsCut)
            break;
        }
        entry.names[entry.count] = name.c_str();
        entry.values[entry.count++] = value.c_str();
    }
    // Without the validator's words the log renders the code and values itself and says once why (ModLog.NoText).
    entry.text = played.worded ? played.text.c_str() : nullptr;
    entry.english = played.worded && english.worded ? english.text.c_str() : nullptr;
    add(entry);
}

void add_row(std::uint32_t source, std::uint32_t level, std::string_view mod, std::uint32_t code, std::string_view file,
             std::string_view row, const std::vector<std::pair<std::string, std::string>>& values) {
    if (g_add.load(std::memory_order_acquire) == nullptr) return;
    const std::string mod_id(mod);
    const std::string place(file);
    const std::string row_id(row);
    LumaModLogEntry entry{};
    entry.size = sizeof(entry);
    entry.source = source;
    entry.level = level;
    entry.code = code;
    entry.mod = mod_id.c_str();
    entry.file = place.c_str();
    for (const auto& [name, value] : values) {
        if (entry.count == LUMA_MODLOG_ARGS) {
            entry.count++;  // the log says once that the rest are not shown (ModLog.ArgsCut)
            break;
        }
        entry.names[entry.count] = name.c_str();
        entry.values[entry.count++] = value.c_str();
    }
    entry.row = row_id.c_str();
    add(entry);
}

namespace {
// One declared permission's answer as the start's plan read it.
struct KeptAnswer {
    std::string name;   // "net.internet"
    std::string shown;  // its short name in the played language at the start (the name when it has no row)
    galaxy::mods::Answer answer = galaxy::mods::Answer::NotAsked;
};
std::mutex g_answers_mutex;
std::map<std::string, std::vector<KeptAnswer>> g_answers;  // mod id -> its permissions, for mods on with a manifest

// The permission an L506 names ("permission", a "T:" text value as the loader puts it); "" for another refusal.
std::string refused_permission(const galaxy::mods::NotStarted& record) {
    if (record.code != 506u) return {};
    for (const auto& [name, value] : record.values) {
        if (name == "permission") return value.rfind("T:", 0) == 0 ? value.substr(2) : value;
    }
    return {};
}

// The kept answer for the L506's permission; false when the record is not an L506 or the answer was not kept.
bool kept_answer(const galaxy::mods::NotStarted& record, KeptAnswer& out) {
    const std::string permission = refused_permission(record);
    if (permission.empty()) return false;
    const std::lock_guard<std::mutex> lock(g_answers_mutex);
    const auto mod = g_answers.find(record.mod_id);
    if (mod == g_answers.end()) return false;
    for (const KeptAnswer& kept : mod->second) {
        if (kept.name == permission) {
            out = kept;
            return true;
        }
    }
    return false;
}
}  // namespace

void keep_permission_answers(const std::vector<galaxy::mods::ModInfo>& mods) {
    std::map<std::string, std::vector<KeptAnswer>> answers;
    for (const galaxy::mods::ModInfo& mod : mods) {
        if (!mod.enabled || !mod.luma) continue;
        std::vector<KeptAnswer> kept;
        for (const galaxy::mods::PermissionFacts& permission : galaxy::mods::consent_facts(mod).permissions) {
            std::string shown = row_words(permission.row.c_str(), mod.id, {});
            if (shown.empty()) shown = permission.name;
            kept.push_back({permission.name, std::move(shown), permission.answer});
        }
        if (!kept.empty()) answers[mod.id] = std::move(kept);
    }
    const std::lock_guard<std::mutex> lock(g_answers_mutex);
    g_answers = std::move(answers);
}

std::string declined_at_start(std::string_view mod_id) {
    for (const galaxy::mods::NotStarted& record : galaxy::mods::plugins_not_started()) {
        if (record.mod_id != mod_id) continue;
        KeptAnswer kept;
        return kept_answer(record, kept) && kept.answer == galaxy::mods::Answer::Declined ? kept.shown : std::string();
    }
    return {};
}

void add_not_started() {
    if (g_add.load(std::memory_order_acquire) == nullptr) return;
    for (const galaxy::mods::NotStarted& record : galaxy::mods::plugins_not_started()) {
        KeptAnswer kept;
        if (kept_answer(record, kept)) {
            // The player declined it (on the mod's page): their choice, not a fault, and the mod is off until they
            // allow it. Granted or never asked, yet refused: the store and the facts disagree (a Nebula fault).
            if (kept.answer == galaxy::mods::Answer::Declined) {
                add_row(LUMA_MODLOG_LOADER, LUMA_MODLOG_NOTE, record.mod_id, record.code, record.library,
                        "Consent.OffDeclined", {{"permission", kept.shown}});
            } else {
                const char* const answer = kept.answer == galaxy::mods::Answer::Granted ? "Consent.Allowed" : "Consent.NotAsked";
                add_row(LUMA_MODLOG_LOADER, LUMA_MODLOG_ERROR, record.mod_id, record.code, record.library,
                        "Consent.RefusedAnyway",
                        {{"permission", kept.shown}, {"answer", row_words(answer, record.mod_id, {})}});
            }
            continue;
        }
        if (record.code != 0) {
            add_finding(LUMA_MODLOG_LOADER, LUMA_MODLOG_ERROR, record.mod_id, record.code, record.library, 0, 0, record.values);
            continue;
        }
        // A host sentence: the module translates it as the Mods page does.
        LumaModLogEntry entry{};
        entry.size = sizeof(entry);
        entry.source = LUMA_MODLOG_LOADER;
        entry.level = LUMA_MODLOG_ERROR;
        entry.code = 0;
        entry.mod = record.mod_id.c_str();
        entry.file = record.library.c_str();
        entry.host = record.sentence.c_str();
        add(entry);
    }
}

std::vector<PageEntry> entries() {
    std::vector<PageEntry> out;
    const LumaModLogNext next = g_next.load(std::memory_order_acquire);
    if (next == nullptr) return out;
    LumaModLogView view{};
    view.size = sizeof(view);
    for (const void* at = next(nullptr, &view); at != nullptr; at = next(at, &view)) {
        out.push_back({view.source, view.level, view.code, view.line, view.column, view.count, view.mod != nullptr ? view.mod : "",
                       view.file != nullptr ? view.file : "", view.text != nullptr ? view.text : "",
                       view.code_name != nullptr ? view.code_name : ""});
    }
    return out;
}

std::vector<PageMod> mods() {
    std::vector<PageMod> out;
    const LumaModLogNextMod next = g_next_mod.load(std::memory_order_acquire);
    if (next == nullptr) return out;
    const char* id = nullptr;
    std::uint32_t past = 0;
    for (const void* at = next(nullptr, &id, &past); at != nullptr; at = next(at, &id, &past)) {
        out.push_back({id != nullptr ? id : "", past});
    }
    return out;
}

std::string words(const char* row, std::string_view mod, std::uint32_t count) {
    const LumaModLogWords give = g_words.load(std::memory_order_acquire);
    if (give == nullptr) return {};
    const std::string mod_id(mod);
    char text[512];
    const std::uint32_t bytes = give(row, mod_id.c_str(), count, text, sizeof(text));
    return std::string(text, bytes);
}

std::string row_words(const char* row, std::string_view mod, const std::vector<std::pair<std::string, std::string>>& values) {
    const LumaModLogRowWords give = g_row_words.load(std::memory_order_acquire);
    if (give == nullptr) return {};
    const std::string mod_id(mod);
    LumaModLogEntry entry{};
    entry.size = sizeof(entry);
    entry.mod = mod_id.c_str();
    for (const auto& [name, value] : values) {
        if (entry.count == LUMA_MODLOG_ARGS) break;
        entry.names[entry.count] = name.c_str();
        entry.values[entry.count++] = value.c_str();
    }
    char text[1024];
    const std::uint32_t bytes = give(&entry, row, text, sizeof(text));
    return std::string(text, bytes);
}

std::uint32_t kept() {
    const LumaModLogNext next = g_next.load(std::memory_order_acquire);
    std::uint32_t count = 0;
    if (next == nullptr) return count;
    for (const void* at = next(nullptr, nullptr); at != nullptr; at = next(at, nullptr)) ++count;
    return count;
}

std::optional<ModClock> mod_clock(std::string_view mod) {
    const LumaTimeClockOf clock_of = g_clock_of.load(std::memory_order_acquire);
    if (clock_of == nullptr) return std::nullopt;
    const std::string id(mod);
    std::uint32_t rate = 1000u;
    std::int32_t holds = 0;
    if (clock_of(id.c_str(), &rate, &holds) != 1) return std::nullopt;
    return ModClock{rate, holds > 0};
}

bool let_go(std::string_view mod) {
    const LumaTimeLetGo ask = g_let_go.load(std::memory_order_acquire);
    if (ask == nullptr) return false;
    const std::string id(mod);
    if (ask(id.c_str()) != 1) return false;
    // The player's act, not the mod's mistake: a Note worded by its row (mod-log 3.1, draft 7: the host's row keeps the
    // level), a service's code (121) so the page names it by the row, not as a validator's L-code.
    add_row(LUMA_MODLOG_CODE, LUMA_MODLOG_NOTE, id, 121u, "", "Time.LetGo", {});
    return true;
}

void fact(const std::string& key, const std::string& json) {
    const LumaModLogFact write = harness_fact();
    if (write != nullptr) write(key.c_str(), json.c_str());
}

bool facts_wanted() {
    return harness_fact() != nullptr;
}

void append_json_text(std::string& out, std::string_view text) {
    append_json(out, text);
}

void page_fact(std::uint32_t row, std::string_view label, std::string_view value) {
    const LumaModLogFact write = harness_fact();
    if (write == nullptr) return;
    std::string json = "{\"row\":" + std::to_string(row) + ",\"label\":";
    append_json(json, label);
    json += ",\"value\":";
    append_json(json, value);
    json += '}';
    write("modlog.page", json.c_str());
}

void entry_fact(std::string_view mod, std::uint32_t pages, std::string_view text) {
    const LumaModLogFact write = harness_fact();
    if (write == nullptr) return;
    std::string json = "{\"mod\":";
    append_json(json, mod);
    json += ",\"pages\":" + std::to_string(pages) + ",\"text\":";
    append_json(json, text);
    json += '}';
    write("modlog.entry", json.c_str());
}

// ---- The start's check (add_start_findings) ----

namespace {

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
};

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
        std::fprintf(stderr, "[modlog] the start's check did not run: the importer's library did not load\n");
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
    if (!ok) std::fprintf(stderr, "[modlog] the start's check did not run: the importer's library lacks luma_validate\n");
    return ok ? &found : nullptr;
}

// FNV-1a, 64 bits: the stamps' hash (no secret; a change of any byte changes it).
std::uint64_t fnv(std::uint64_t h, std::string_view bytes) {
    for (const char c : bytes) {
        h ^= static_cast<unsigned char>(c);
        h *= 0x100000001b3ull;
    }
    return h;
}
constexpr std::uint64_t kFnvStart = 0xcbf29ce484222325ull;

std::string hex(std::uint64_t v) {
    char text[24];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(v));
    return text;
}

// A package's files: every name under its folder with its size and time, in name order. Empty when it cannot be read.
std::string files_stamp(const std::filesystem::path& folder) {
    std::vector<std::string> rows;
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator it(folder, error), end; !error && it != end; it.increment(error)) {
        std::error_code e;
        if (!it->is_regular_file(e)) continue;
        const auto size = it->file_size(e);
        const auto time = it->last_write_time(e).time_since_epoch().count();
        rows.push_back(std::filesystem::relative(it->path(), folder, e).generic_string() + "\t" + std::to_string(size) +
                       "\t" + std::to_string(static_cast<long long>(time)));
    }
    if (error) return {};
    std::sort(rows.begin(), rows.end());
    std::uint64_t h = kFnvStart;
    for (const std::string& row : rows) h = fnv(fnv(h, row), "\n");
    return hex(h);
}

// A package's bytes: every file's name and content under its folder, in name order. Read only when files_stamp changed
// (a copy, a touch, an unpack keeps the bytes and so the verdict). Empty when a file cannot be read.
std::string content_hash(const std::filesystem::path& folder) {
    std::vector<std::filesystem::path> files;
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator it(folder, error), end; !error && it != end; it.increment(error)) {
        std::error_code e;
        if (it->is_regular_file(e)) files.push_back(it->path());
    }
    if (error) return {};
    std::sort(files.begin(), files.end());
    std::uint64_t h = kFnvStart;
    std::vector<char> chunk(64 * 1024);
    for (const std::filesystem::path& file : files) {
        std::error_code e;
        h = fnv(fnv(h, std::filesystem::relative(file, folder, e).generic_string()), "\n");
        std::ifstream in(file, std::ios::binary);
        if (!in) return {};
        while (in) {
            in.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
            h = fnv(h, std::string_view(chunk.data(), static_cast<std::size_t>(in.gcount())));
        }
    }
    return hex(h);
}

// One finding as kept: code, level, line, column, file, played text, English text (tabs and line breaks escaped).
struct Kept {
    std::uint32_t code = 0;
    std::uint32_t level = 0;
    std::uint32_t line = 0;
    std::uint32_t column = 0;
    std::string file;
    std::string played;
    std::string english;
};

std::string escape(std::string_view text) {
    std::string out;
    for (const char c : text) {
        if (c == '\\') out += "\\\\";
        else if (c == '\t') out += "\\t";
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else out += c;
    }
    return out;
}

std::string unescape(std::string_view text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\\' || i + 1 == text.size()) {
            out += text[i];
            continue;
        }
        const char n = text[++i];
        out += n == 't' ? '\t' : n == 'n' ? '\n' : n == 'r' ? '\r' : n;
    }
    return out;
}

std::vector<std::string> split_tabs(const std::string& line) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    for (;;) {
        const std::size_t tab = line.find('\t', start);
        parts.push_back(line.substr(start, tab == std::string::npos ? std::string::npos : tab - start));
        if (tab == std::string::npos) break;
        start = tab + 1;
    }
    return parts;
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

bool write_file(const std::filesystem::path& path, std::string_view bytes) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    const std::filesystem::path temp = path.string() + ".new";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out) return false;
    }
    std::filesystem::rename(temp, path, error);
    return !error;
}

// <id>.check: "files <stamp>", "content <hash>", then up to kVerdicts verdicts, newest first: "findings <key>" and one
// finding a line. The stamp and the hash are each with the game's; a key covers the content, the game and the other
// packages' as this one saw them. More than one, because a start's rounds (check_packages) see the others twice when
// one is refused, and each start would otherwise validate again.
struct Verdict {
    std::string key;
    std::vector<Kept> findings;
};
constexpr std::size_t kVerdicts = 4;
struct Check {
    std::string files;
    std::string content;
    std::vector<Verdict> verdicts;
};

Check read_check(const std::filesystem::path& path) {
    Check check;
    std::istringstream in(read_file(path));
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("files ", 0) == 0) {
            check.files = line.substr(6);
        } else if (line.rfind("content ", 0) == 0) {
            check.content = line.substr(8);
        } else if (line.rfind("findings ", 0) == 0) {
            if (check.verdicts.size() == kVerdicts) break;
            check.verdicts.push_back({line.substr(9), {}});
        } else if (line.rfind("f\t", 0) == 0) {
            const std::vector<std::string> p = split_tabs(line);
            if (p.size() != 8 || check.verdicts.empty()) return {};  // another form: validated again
            Kept k;
            k.code = static_cast<std::uint32_t>(std::strtoul(p[1].c_str(), nullptr, 10));
            k.level = static_cast<std::uint32_t>(std::strtoul(p[2].c_str(), nullptr, 10));
            k.line = static_cast<std::uint32_t>(std::strtoul(p[3].c_str(), nullptr, 10));
            k.column = static_cast<std::uint32_t>(std::strtoul(p[4].c_str(), nullptr, 10));
            k.file = unescape(p[5]);
            k.played = unescape(p[6]);
            k.english = unescape(p[7]);
            check.verdicts.back().findings.push_back(std::move(k));
        }
    }
    return check;
}

std::string check_text(const Check& check) {
    std::string out = "files " + check.files + "\ncontent " + check.content + "\n";
    for (const Verdict& verdict : check.verdicts) {
        out += "findings " + verdict.key + "\n";
        for (const Kept& k : verdict.findings) {
            out += "f\t" + std::to_string(k.code) + "\t" + std::to_string(k.level) + "\t" + std::to_string(k.line) + "\t" +
                   std::to_string(k.column) + "\t" + escape(k.file) + "\t" + escape(k.played) + "\t" + escape(k.english) +
                   "\n";
        }
    }
    return out;
}

// The start's check's findings by package, in list order: written by check_packages (the start thread), read by the
// log and the Mods page (any thread), each under g_start_lock.
struct StartResult {
    std::string id;
    std::vector<Kept> findings;
};
std::mutex g_start_lock;
std::vector<StartResult> g_start_results;

// What a check's findings and kept reports depend on beyond the package: the platform with its processor
// ("windows-x64", luma-format 6), the disc ("RMGE01"; empty: none) and the played language folder.
struct CheckGame {
    std::string platform;
    std::string region;
    std::string played;
};
CheckGame g_check_game;  // the last start's check's, for check_import; under g_start_lock

// One luma_validate call on the package in `folder`: `installed` the other packages (with `kept_in` empty: every rule,
// alone), `position` its place among them (UINT32_MAX alone). Its kept report into `kept_out`, its findings (when
// asked) in English and the played language. False: the validator did not run (no library, or the package unreadable).
bool validate_package(const Validator* v, std::string_view id, const std::filesystem::path& folder, const CheckGame& game,
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

void add_kept(std::string_view mod, const Kept& k) {
    const std::string mod_id(mod);
    LumaModLogEntry entry{};
    entry.size = sizeof(entry);
    entry.source = LUMA_MODLOG_VALIDATOR;
    entry.level = k.level == 0 ? LUMA_MODLOG_ERROR : LUMA_MODLOG_NOTE;
    entry.code = k.code;
    entry.mod = mod_id.c_str();
    entry.file = k.file.c_str();
    entry.line = k.line;
    entry.column = k.column;
    entry.text = k.played.c_str();
    entry.english = k.english.c_str();
    add(entry);
}

}  // namespace

void check_packages(const std::vector<StartPackage>& packages, std::string_view program, std::string_view language) {
    // luma-format's platform names carry the processor ("windows-x64"), as a package's code does.
    const CheckGame side{std::string(galaxy::luma::plugin_target()), std::string(program),
                         language.empty() ? std::string("UsEnglish") : std::string(language)};
    {
        const std::lock_guard<std::mutex> lock(g_start_lock);
        g_start_results.clear();
        g_check_game = side;
    }
    if (packages.empty()) return;
    const std::filesystem::path folder = galaxy::mods::mods_folder() / ".cooked" / "validate";
    const std::string& platform = side.platform;
    const std::string& region = side.region;
    const std::string& played = side.played;
    // What a kept report and a finding's words depend on beyond the package's own files.
    const std::string game = std::string(NEBULA_VERSION) + "\t" + region + "\t" + platform + "\t" + played + "\t" +
                             std::to_string(LUMA_API);

    // Pass 1: each package's kept report (its own rules), validated only when its bytes changed: the names, sizes and
    // times first; their content hashed only when those changed.
    struct Placed {
        const StartPackage* package;
        std::string files;
        Check check;
        std::string kept;
        std::string content;  // the content key: the package's identity for the others' keys
    };
    std::vector<Placed> placed;
    placed.reserve(packages.size());
    const Validator* v = nullptr;
    bool looked = false;
    const auto run = [&](const StartPackage& p, const std::vector<LumaInstalledMod>& installed, std::uint32_t position,
                         const std::string& kept_in, std::string& kept_out, std::vector<Kept>* findings) -> bool {
        if (!looked) {
            looked = true;
            v = validator();
        }
        return validate_package(v, p.id, p.folder, side, installed, position, kept_in, kept_out, findings);
    };
    for (const StartPackage& p : packages) {
        Placed one{&p, files_stamp(p.folder), read_check(folder / (p.id + ".check")), read_file(folder / (p.id + ".kept")), {}};
        const std::string files_key = hex(fnv(fnv(kFnvStart, one.files), game));
        if (one.files.empty()) {
            std::fprintf(stderr, "[modlog] %s: its folder cannot be read; not checked\n", p.id.c_str());
            one.kept.clear();
        } else if (one.check.files == files_key && !one.check.content.empty() && !one.kept.empty()) {
            one.content = one.check.content;
        } else {
            const std::string bytes = content_hash(p.folder);
            const std::string content_key = bytes.empty() ? std::string() : hex(fnv(fnv(kFnvStart, bytes), game));
            if (!content_key.empty() && one.check.content == content_key && !one.kept.empty()) {
                // Touched, not changed: the verdict stands; only the stamp is new.
                one.check.files = files_key;
                (void)write_file(folder / (p.id + ".check"), check_text(one.check));
                one.content = content_key;
            } else {
                std::string kept;
                if (!content_key.empty() && run(p, {}, UINT32_MAX, {}, kept, nullptr)) {
                    one.kept = std::move(kept);
                    one.check = Check{files_key, content_key, {}};
                    one.content = content_key;
                    (void)write_file(folder / (p.id + ".kept"), one.kept);
                } else {
                    one.kept.clear();
                }
            }
        }
        one.files = files_key;
        placed.push_back(std::move(one));
    }

    // A package whose kept report names no id was refused (its luma.json gives none): it never starts, so the others
    // see it off, as the validator takes it ("it can only be off").
    std::vector<bool> named(placed.size(), true);
    if (const Validator* const found = validator(); found != nullptr && found->read_kept != nullptr && found->id != nullptr) {
        for (std::size_t j = 0; j < placed.size(); ++j) {
            if (placed[j].kept.empty()) continue;
            LumaReport* const read = found->read_kept(reinterpret_cast<const std::uint8_t*>(placed[j].kept.data()),
                                                      placed[j].kept.size());
            const char* const id = found->state(read) == 2 ? found->id(read) : "";
            named[j] = id != nullptr && id[0] != 0;
            found->release(read);
        }
    }

    // Pass 2: each against the others (L8xx), from its kept report; again only when its key changed. In rounds: a
    // package on that a round refuses is off for the others in the next (a mod that loses what it needs takes with it
    // the mods that need it) and stays refused with that round's findings, so the rounds end, one at most a package.
    // With nothing refused there is one round.
    std::vector<bool> off(placed.size());
    for (std::size_t j = 0; j < placed.size(); ++j) off[j] = !named[j];
    std::vector<bool> refused(placed.size(), false);
    std::vector<bool> checked(placed.size(), false);
    std::vector<std::vector<Kept>> verdict_of(placed.size());  // each package's findings from its last round
    for (std::size_t round = 0; round <= placed.size(); ++round) {
        bool more = false;
        for (std::size_t i = 0; i < placed.size(); ++i) {
            Placed& one = placed[i];
            if (one.kept.empty() || refused[i]) continue;
            std::vector<LumaInstalledMod> installed;
            std::vector<std::string> files;
            std::uint64_t h = fnv(kFnvStart, one.content);
            files.reserve(placed.size());
            for (std::size_t j = 0; j < placed.size(); ++j) {
                if (j == i || placed[j].kept.empty()) continue;
                files.push_back(placed[j].package->id);
            }
            std::size_t at = 0;
            std::uint32_t position = 0;
            for (std::size_t j = 0; j < placed.size(); ++j) {
                if (j == i || placed[j].kept.empty()) continue;
                if (j < i) position++;
                LumaInstalledMod m{};
                m.file = files[at++].c_str();
                const bool on = placed[j].package->on && !off[j];
                m.on = on ? 1u : 0u;
                m.kept = reinterpret_cast<const std::uint8_t*>(placed[j].kept.data());
                m.kept_size = placed[j].kept.size();
                installed.push_back(m);
                h = fnv(fnv(fnv(h, placed[j].content), on ? "1" : "0"), placed[j].package->id);
            }
            const std::string key = hex(fnv(h, std::to_string(position)));
            const auto known = std::find_if(one.check.verdicts.begin(), one.check.verdicts.end(),
                                            [&key](const Verdict& v) { return v.key == key; });
            if (known != one.check.verdicts.end()) {
                verdict_of[i] = known->findings;
            } else {
                std::string kept;
                std::vector<Kept> findings;
                if (!run(*one.package, installed, position, one.kept, kept, &findings)) continue;
                verdict_of[i] = findings;
                one.check.files = one.files;
                one.check.content = one.content;
                one.check.verdicts.insert(one.check.verdicts.begin(), Verdict{key, std::move(findings)});
                if (one.check.verdicts.size() > kVerdicts) one.check.verdicts.resize(kVerdicts);
                (void)write_file(folder / (one.package->id + ".check"), check_text(one.check));
            }
            checked[i] = true;
            if (one.package->on && std::any_of(verdict_of[i].begin(), verdict_of[i].end(),
                                               [](const Kept& k) { return k.level == 0; })) {
                refused[i] = true;
                more = true;
            }
        }
        if (!more) break;
        for (std::size_t j = 0; j < placed.size(); ++j) off[j] = !named[j] || refused[j];
    }
    std::vector<StartResult> results;
    for (std::size_t i = 0; i < placed.size(); ++i) {
        if (checked[i]) results.push_back({placed[i].package->id, std::move(verdict_of[i])});
    }
    const std::lock_guard<std::mutex> lock(g_start_lock);
    g_start_results = std::move(results);
}

void add_start_findings() {
    if (g_add.load(std::memory_order_acquire) == nullptr) return;
    std::vector<StartResult> results;
    {
        const std::lock_guard<std::mutex> lock(g_start_lock);
        results = g_start_results;
    }
    for (const StartResult& result : results) {
        for (const Kept& k : result.findings) add_kept(result.id, k);
    }
}

namespace {

// One field of a facts line as written (cook/refuse.rs `field`): `\t`, `\n`, `\r`, `\\` back to their bytes.
std::string facts_field(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\\' || i + 1 == text.size()) {
            out.push_back(text[i]);
            continue;
        }
        const char next = text[++i];
        out.push_back(next == 't' ? '\t' : next == 'n' ? '\n' : next == 'r' ? '\r' : next);
    }
    return out;
}

// A cook fact's mod when the line names none (a mod's own file): the <id> of "Luma/<id>/..." (a "+" before it).
std::string cook_fact_mod(const std::vector<std::string>& f) {
    if (!f[2].empty()) return f[2];
    for (const std::size_t at : {std::size_t{1}, std::size_t{3}}) {
        std::string_view path = f[at];
        if (!path.empty() && path.front() == '+') path.remove_prefix(1);
        if (path.substr(0, 5) != "Luma/") continue;
        path.remove_prefix(5);
        return std::string(path.substr(0, path.find('/')));
    }
    return {};
}

// A parameter's value as JSON: a whole number as a number, a list of them (comma between) as a list, else text.
void append_param_json(std::string& out, std::string_view name, std::string_view value) {
    const auto whole = [](std::string_view v) {
        return !v.empty() && v.size() <= 9 && std::all_of(v.begin(), v.end(), [](char c) { return c >= '0' && c <= '9'; });
    };
    if (name == "particles") {
        out += '[';
        bool first = true;
        std::size_t at = 0;
        while (at < value.size()) {
            const std::size_t end = (std::min)(value.find(',', at), value.size());
            const std::string_view one = value.substr(at, end - at);
            if (whole(one)) {
                out += first ? "" : ",";
                out += one;
                first = false;
            }
            at = end + 1;
        }
        out += ']';
    } else if (whole(value)) {
        out += value;
    } else {
        append_json(out, value);
    }
}

}  // namespace

void add_cook_facts(const std::filesystem::path& facts_file) {
    if (facts_file.empty()) return;
    std::istringstream in(read_file(facts_file));
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        // kind table mod file other row column selector rows code why, then the parameters.
        std::vector<std::string> f;
        std::size_t at = 0;
        for (;;) {
            const std::size_t tab = line.find('\t', at);
            f.push_back(facts_field(std::string_view(line).substr(at, tab == std::string::npos ? std::string::npos : tab - at)));
            if (tab == std::string::npos) break;
            at = tab + 1;
        }
        if (f.size() < 11) continue;
        const std::string& kind = f[0];
        const std::string mod = cook_fact_mod(f);
        if (kind == "failed" || kind == "noted") {
            std::uint32_t code = 0;
            for (const char c : f[9]) {
                if (c < '0' || c > '9' || code > 99999u) {
                    code = 0;
                    break;
                }
                code = code * 10u + static_cast<std::uint32_t>(c - '0');
            }
            if (code == 0u) continue;
            std::vector<std::pair<std::string, std::string>> values;
            for (std::size_t i = 11; i < f.size(); ++i) {
                const std::size_t eq = f[i].find('=');
                if (eq == std::string::npos || eq == 0) continue;
                values.emplace_back(f[i].substr(0, eq), f[i].substr(eq + 1));
            }
            add_finding(LUMA_MODLOG_COOK, kind == "failed" ? LUMA_MODLOG_ERROR : LUMA_MODLOG_NOTE, mod, code,
                        f[3].empty() ? f[1] : f[3], 0, 0, values);
        } else if (kind == "cook.effect" || kind == "cook.model") {
            std::string json = "{\"table\":";
            append_json(json, f[1]);
            json += ",\"file\":";
            append_json(json, f[3]);
            json += ",\"mod\":";
            append_json(json, mod);
            json += ",\"other\":";
            append_json(json, f[4]);
            for (std::size_t i = 11; i < f.size(); ++i) {
                const std::size_t eq = f[i].find('=');
                if (eq == std::string::npos || eq == 0) continue;
                const std::string_view name = std::string_view(f[i]).substr(0, eq);
                if (name == "table" || name == "file" || name == "mod" || name == "other") continue;
                json += ',';
                append_json(json, name);
                json += ':';
                append_param_json(json, name, std::string_view(f[i]).substr(eq + 1));
            }
            json += '}';
            fact(kind, json);
        }
    }
}

bool package_refused(std::string_view id) {
    const std::lock_guard<std::mutex> lock(g_start_lock);
    for (const StartResult& result : g_start_results) {
        if (result.id != id) continue;
        for (const Kept& k : result.findings) {
            if (k.level == 0) return true;
        }
    }
    return false;
}

ImportCheck check_import(const std::filesystem::path& folder, const std::vector<StartPackage>& others) {
    ImportCheck out;
    const Validator* const v = validator();
    if (v == nullptr) return out;
    CheckGame side;
    {
        const std::lock_guard<std::mutex> lock(g_start_lock);
        side = g_check_game;
    }
    if (side.platform.empty()) side = CheckGame{std::string(galaxy::luma::plugin_target()), {}, "UsEnglish"};
    // The packages placed now, each by its kept report: the last start's, else (placed since, or before any start's
    // check) one made now, alone. On as the list has it, but off when it would not start: refused at the last start, a
    // report made now with an Error, or a report naming no id (the validator takes those only off).
    const std::filesystem::path validate = galaxy::mods::mods_folder() / ".cooked" / "validate";
    std::vector<std::string> ids;
    std::vector<std::string> reports;
    std::vector<bool> on;
    for (const StartPackage& p : others) {
        std::string report = read_file(validate / (p.id + ".kept"));
        bool starts = p.on && !package_refused(p.id);
        if (report.empty()) {
            std::vector<Kept> alone;
            if (!validate_package(v, p.id, p.folder, side, {}, UINT32_MAX, {}, report, &alone) || report.empty()) continue;
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
    if (!validate_package(v, "import", folder, side, installed, UINT32_MAX, {}, kept, &findings)) return out;
    out.ran = true;
    for (const Kept& k : findings) {
        out.findings.push_back({k.code, k.level == 0, k.file, k.played, k.line, k.column});
        out.refused = out.refused || k.level == 0;
    }
    return out;
}

const char* refused_sentence() {
    return "THIS MOD HAS ERRORS. ITS LOG SAYS WHY";
}

}  // namespace galaxy::modlog
