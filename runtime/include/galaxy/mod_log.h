// SPDX-License-Identifier: GPL-3.0-only
// The mod log's host half (contract mod-log): the host tells the game module's log what it found about the mods (the
// validator's findings, the loader's and the order's refusals, the cook's failures) as entries, in the played language
// and English. The module keeps them, shows them and writes the harness's facts (luma_modlog.h).
#pragma once

#include <functional>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace galaxy::mods {
struct ModInfo;
}

namespace galaxy::modlog {

// Once a session, before the mods' code starts: finds the module's exports (`find_symbol`, as start_plugins takes it)
// and tells the log how many mods are on (0: none; then the log costs nothing) and, in a harness run, the harness's
// fact writer. Nothing happens when the module has no log.
void begin(void* (*find_symbol)(const char*), std::uint32_t mods_on);

// Before the start's plan: each mod's stored permission answers, from the ModInfo list its granted lists come from
// (consent 1.4: the two never differ within one start). add_not_started and declined_at_start read them.
void keep_permission_answers(const std::vector<galaxy::mods::ModInfo>& mods);

// Runs `then` once begin has run (at once when it has; else at begin's end, on its thread): for what needs the module's
// words, as an import's facts do when Open with or --import imports before the game module's log is found. Any thread.
void after_begin(std::function<void()> then);

// After start_plugins: each mod whose code did not start (galaxy::mods::plugins_not_started, Z-10) as a LOADER Error:
// a refusal in the validator's words, a host sentence as the Mods page translates it. An L506 (a required service's
// permission not granted) whose kept answer is Declined is the player's choice, a Note `Consent.OffDeclined`; one whose
// answer is Granted or not asked is the store and the facts disagreeing, an Error `Consent.RefusedAnyway` (consent 2.3).
void add_not_started();

// A placed package for the start's check: its folder name, its folder, whether the player has it on.
struct StartPackage {
    std::string id;
    std::filesystem::path folder;
    bool on = true;
};

// Each start, before the mods' plan (validator 2 "each start", mod-log 7): every placed package through the validator
// (luma_validate, the importer's library), its findings worded in the played language (`language`, a language folder)
// and in English and kept for add_start_findings and package_refused. The other packages are its `installed`, in list
// order; `program` is the player's disc ("RMGE01"). Kept per package in <mods>/.cooked/validate/: a package is
// validated again only when its bytes (names, sizes and times first, then the content's hash), the game (release,
// program, platform, language) or the other packages changed; otherwise its findings are read back. Nothing at all
// with no package.
void check_packages(const std::vector<StartPackage>& packages, std::string_view program, std::string_view language);

// One finding of an import's check, worded in the played language.
struct ImportFinding {
    std::uint32_t code = 0;  // 509 is L509
    bool error = false;      // an Error refuses the import; else a Note
    std::string file;        // the entry inside the mod ("" for the whole mod)
    std::string text;
    std::uint32_t line = 0;  // the place in `file` (0: none)
    std::uint32_t column = 0;
};

// An import's check (validator 2 "at import", task 6.1): `ran` false when the validator did not run (the importer's
// library is missing; nothing is refused then, as at the start); `refused` when it found an Error.
struct ImportCheck {
    bool ran = false;
    bool refused = false;
    std::vector<ImportFinding> findings;
};

// The validator on a package staged for import (`folder`, laid out as a placed mod), once: every rule (no kept report
// of its own), last in the list after `others`, the packages placed now in list order, each by its kept report from
// the last start's check (else one made now, alone), off when it would not start (L800 to L802). The disc and language
// are the last start's check's (before any: no disc, English). Any thread; it reads the validator's library, the kept
// reports and the packages it has to check alone.
ImportCheck check_import(const std::filesystem::path& folder, const std::vector<StartPackage>& others);

// After add_not_started: the start's check's findings, each a VALIDATOR entry (Error or Note as the validator's).
void add_start_findings();

// The device cooker's facts file (`<overlay>.facts`, cook/overlay.rs; mod-log 9's request to G, read by the host's
// half): each `failed` line a COOK Error and each `noted` line a COOK Note, by its code, mod, file and parameters
// (`name=T:<text>` or `name=P:<phrase>`); each `cook.effect` and `cook.model` line (effects 7, models 9) a harness fact
// of that key: {"table", "file", "mod", "other", then each `name=value` parameter, a number as a number and
// `particles` as a list}. A conflict is left out: the start's check says each overlap (L305, L404). After
// add_start_findings, the overlay attached; no file, nothing.
void add_cook_facts(const std::filesystem::path& facts_file);

// The start's check found an Error in the package `id`: it is refused (the validator's errors refuse, as at import).
[[nodiscard]] bool package_refused(std::string_view id);

// The Mods page's sentence for a package refused by the start's check (a host sentence: S. row in 14 languages).
[[nodiscard]] const char* refused_sentence();

// The permission as the player reads it when the mod's code did not start for an L506 and its answer kept at the start
// is Declined (the Mods page then says Consent.OffDeclined, as the log does); "" otherwise. Any thread.
[[nodiscard]] std::string declined_at_start(std::string_view mod_id);

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
// Whether this run has the harness, so a screen builds its facts' JSON only then.
[[nodiscard]] bool facts_wanted();
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
