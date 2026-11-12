// SPDX-License-Identifier: GPL-3.0-only
// The mod log's host half (contract mod-log 1, 3, 9; runtime/include/galaxy/mod_log.h).

#include "galaxy/mod_log.h"

#include "galaxy/host_extension.h"
#include "galaxy/mod_consent.h"
#include "galaxy/mod_plugins.h"
#include "galaxy/mods.h"

#include <luma_harness.h>
#include <luma_modlog.h>

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <vector>

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

}  // namespace

void begin(void* (*find_symbol)(const char*), std::uint32_t mods_on) {
    if (find_symbol == nullptr) return;
    const auto session = reinterpret_cast<LumaModLogSession>(find_symbol("luma_modlog_session"));
    g_next.store(reinterpret_cast<LumaModLogNext>(find_symbol("luma_modlog_next")), std::memory_order_release);
    g_next_mod.store(reinterpret_cast<LumaModLogNextMod>(find_symbol("luma_modlog_next_mod")), std::memory_order_release);
    g_words.store(reinterpret_cast<LumaModLogWords>(find_symbol("luma_modlog_words")), std::memory_order_release);
    g_row_words.store(reinterpret_cast<LumaModLogRowWords>(find_symbol("luma_modlog_row_words")), std::memory_order_release);
    g_add.store(reinterpret_cast<LumaModLogAdd>(find_symbol("luma_modlog_add")), std::memory_order_release);
    g_clock_of.store(reinterpret_cast<LumaTimeClockOf>(find_symbol("luma_time_clock_of")), std::memory_order_release);
    g_let_go.store(reinterpret_cast<LumaTimeLetGo>(find_symbol("luma_time_let_go")), std::memory_order_release);
    if (session != nullptr) session(mods_on, harness_fact());
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

}  // namespace galaxy::modlog
