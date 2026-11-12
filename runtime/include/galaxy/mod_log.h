// SPDX-License-Identifier: GPL-3.0-only
// The mod log's host half (contract mod-log): the host tells the game module's log what it found about the mods (the
// validator's findings, the loader's and the order's refusals, the cook's failures) as entries, in the played language
// and English. The module keeps them, shows them and writes the harness's facts (luma_modlog.h).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace galaxy::modlog {

// Once a session, before the mods' code starts: finds the module's exports (`find_symbol`, as start_plugins takes it)
// and tells the log how many mods are on (0: none; then the log costs nothing) and, in a harness run, the harness's
// fact writer. Nothing happens when the module has no log.
void begin(void* (*find_symbol)(const char*), std::uint32_t mods_on);

// After start_plugins: each mod whose code did not start (galaxy::mods::plugins_not_started, Z-10) as a LOADER Error:
// a refusal in the validator's words, a host sentence as the Mods page translates it.
void add_not_started();

// One host entry (sources 0 VALIDATOR, 1 LOADER, 2 COOK; levels 0 Error, 1 Note): the validator's code and its
// parameters, worded by the validator in the played language and in English. `file` the entry inside the mod, `line`
// and `column` 0 when none.
void add_finding(std::uint32_t source, std::uint32_t level, std::string_view mod, std::uint32_t code, std::string_view file,
                 std::uint32_t line, std::uint32_t column, const std::vector<std::pair<std::string, std::string>>& values);

// What the log holds, for the Mods > Log page and a mod's own page (contract 4.2); any thread. Empty when the module
// has no log or nothing was kept.
struct PageEntry {
    std::uint32_t source = 0, level = 0, code = 0, line = 0, column = 0, count = 0;
    std::string mod;
    std::string file;       // "" when none
    std::string text;       // the played language
    std::string code_name;  // "L203", "Hook.NoResult", "S0904"; "" for none
};
struct PageMod {
    std::string id;
    std::uint32_t past = 0;  // entries not kept (ModLog.TooMany)
};
[[nodiscard]] std::vector<PageEntry> entries();  // in added order
[[nodiscard]] std::vector<PageMod> mods();       // in the order each mod's first entry came
// The module's row `row` (ModLog.*) in the played language, {mod} and {count} filled; "" when there is none.
[[nodiscard]] std::string words(const char* row, std::string_view mod, std::uint32_t count);
// How many entries the log keeps (a walk, nothing copied).
[[nodiscard]] std::uint32_t kept();
// In a harness run, one row of the log page as the fact `modlog.page {"row", "label", "value"}` (contract 4.3).
void page_fact(std::uint32_t row, std::string_view label, std::string_view value);

}  // namespace galaxy::modlog
