// SPDX-License-Identifier: GPL-3.0-only
// The mod log's host half (contract mod-log 1, 3, 9; runtime/include/galaxy/mod_log.h).

#include "galaxy/mod_log.h"

#include "galaxy/host_extension.h"
#include "galaxy/mod_plugins.h"

#include <luma_harness.h>
#include <luma_modlog.h>

#include <atomic>
#include <cstddef>

namespace galaxy::modlog {
namespace {

std::atomic<LumaModLogAdd> g_add{nullptr};

// The harness's fact writer, when this run has the harness (its service exists only then).
LumaModLogFact harness_fact() {
    const auto* harness = static_cast<const LumaHarness*>(galaxy::extensions::service(LUMA_HARNESS_NAME, LUMA_HARNESS_MAJOR, 0));
    if (harness == nullptr || harness->size < offsetof(LumaHarness, fact) + sizeof(harness->fact)) return nullptr;
    return harness->fact;
}

void add(const LumaModLogEntry& entry) {
    if (const LumaModLogAdd give = g_add.load(std::memory_order_acquire)) give(&entry);
}

}  // namespace

void begin(void* (*find_symbol)(const char*), std::uint32_t mods_on) {
    if (find_symbol == nullptr) return;
    const auto session = reinterpret_cast<LumaModLogSession>(find_symbol("luma_modlog_session"));
    g_add.store(reinterpret_cast<LumaModLogAdd>(find_symbol("luma_modlog_add")), std::memory_order_release);
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

}  // namespace galaxy::modlog
