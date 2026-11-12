// SPDX-License-Identifier: GPL-3.0-only
// The mod log's host half (contract mod-log 1, 3, 9; runtime/include/galaxy/mod_log.h).

#include "galaxy/mod_log.h"

#include "galaxy/host_extension.h"
#include "galaxy/mod_plugins.h"

#include <luma_harness.h>
#include <luma_modlog.h>

#include <atomic>
#include <cstddef>
#include <cstdio>

namespace galaxy::modlog {
namespace {

static_assert(kSourceCook == LUMA_MODLOG_COOK, "mod_log.h's kSourceCook is luma_modlog.h's");

std::atomic<LumaModLogAdd> g_add{nullptr};
// The page's reads (luma_modlog.h), found with g_add.
std::atomic<LumaModLogNext> g_next{nullptr};
std::atomic<LumaModLogNextMod> g_next_mod{nullptr};
std::atomic<LumaModLogWords> g_words{nullptr};
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

void add_not_started() {
    if (g_add.load(std::memory_order_acquire) == nullptr) return;
    for (const galaxy::mods::NotStarted& record : galaxy::mods::plugins_not_started()) {
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

}  // namespace galaxy::modlog
