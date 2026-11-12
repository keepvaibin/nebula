// SPDX-License-Identifier: GPL-3.0-only
// The mod log's host half (contract mod-log): the host tells the game module's log what it found about the mods (the
// validator's findings, the loader's and the order's refusals, the cook's failures) as entries, in the played language
// and English. The module keeps them, shows them and writes the harness's facts (luma_modlog.h).
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
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

// A placed package for the start's check: its folder name, its folder, whether the player has it on.
struct StartPackage {
    std::string id;
    std::filesystem::path folder;
    bool on = true;
};

// Each start, after add_not_started (validator 2 "each start", mod-log 7): every placed package through the
// validator (luma_validate, the importer's library), each finding a VALIDATOR entry worded in the played language
// (`language`, a language folder) and in English. The other packages are its `installed`, in list order; `program` is
// the player's disc ("RMGE01"). Kept per package in <mods>/.cooked/validate/: a package is validated again only when
// its files (names, sizes, times), the game (release, program, platform, language) or the other packages changed;
// otherwise its findings are read back. Nothing at all with no package. Findings do not refuse here: mods.cpp still
// decides (director, 21:00 integration of 11-11); the validator's errors refuse once every test mod has one form.
void add_start_findings(const std::vector<StartPackage>& packages, std::string_view program, std::string_view language);

// One host entry (sources 0 VALIDATOR, 1 LOADER, 2 COOK; levels 0 Error, 1 Note): the validator's code and its
// parameters, worded by the validator in the played language and in English. `file` the entry inside the mod, `line`
// and `column` 0 when none.
void add_finding(std::uint32_t source, std::uint32_t level, std::string_view mod, std::uint32_t code, std::string_view file,
                 std::uint32_t line, std::uint32_t column, const std::vector<std::pair<std::string, std::string>>& values);

// One host entry worded by a row of the game's text table (`row`, e.g. Start.PartMissing; luma_modlog.h `row`): its
// {names} from `values`, a value "R:<row>" being that row's words. For the host's own lines that no validator code words.
void add_row(std::uint32_t source, std::uint32_t level, std::string_view mod, std::uint32_t code, std::string_view file,
             std::string_view row, const std::vector<std::pair<std::string, std::string>>& values);

// What the log holds, for the Mods > Log page and a mod's own page (contract 4.2); any thread. Empty when the module
// has no log or nothing was kept.
// PageEntry::source of the cooker's and the start's own lines (luma_modlog.h LUMA_MODLOG_COOK), for code that does not
// include the C header.
inline constexpr std::uint32_t kSourceCook = 2u;

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
// Any row of the module's text table in the played language, {mod} and `values`' {names} filled (a value "R:<row>"
// is that row's words); "" when there is none. For the host's screens' rows with values (consent 2.2).
[[nodiscard]] std::string row_words(const char* row, std::string_view mod,
                                    const std::vector<std::pair<std::string, std::string>>& values);
// How many entries the log keeps (a walk, nothing copied).
[[nodiscard]] std::uint32_t kept();
// In a harness run, one row of the log page as the fact `modlog.page {"row", "label", "value"}` (contract 4.3).
void page_fact(std::uint32_t row, std::string_view label, std::string_view value);
// In a harness run, a fact `key` with `json` as its value (the host's screens: consent 2.6); nothing otherwise.
void fact(const std::string& key, const std::string& json);
// `text` as a JSON string, quoted and escaped, appended to `out`.
void append_json_text(std::string& out, std::string_view text);
// In a harness run, an entry opened whole (A or Space on its row) as `modlog.entry {"mod", "pages", "text"}`: the
// window's pages and the text they hold, all of it.
void entry_fact(std::string_view mod, std::uint32_t pages, std::string_view text);

// A mod's world clock as the Mods page shows it (contract services-time 10 and 11; the module's luma_time_clock_of,
// found by `begin`); any thread. Empty when the module lacks the export or the mod never asked for time.
struct ModClock {
    std::uint32_t rate = 1000u;  // thousandths of real time
    bool held = false;           // the mod holds the world
};
[[nodiscard]] std::optional<ModClock> mod_clock(std::string_view mod);
// The page's "Let go" (services-time 10): the module clears the mod's rate, hold and owed steps at the next frame's
// start (luma_time_let_go); then the Note Time.LetGo is in the log. False when the module did not take it (the mod
// unknown, or 8 already waiting); any thread.
bool let_go(std::string_view mod);

}  // namespace galaxy::modlog
