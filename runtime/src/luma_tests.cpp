// SPDX-License-Identifier: GPL-3.0-only
// luma.json in the one form (board/contracts/manifest-one-form.md; the validator is the rule): what the reader takes,
// pre_standard's rows (OLD-ROWS.md), and what a person might get wrong. Refusals are the validator's, tested there.
#include "galaxy/luma.h"
#include "galaxy/test_temp.h"
#include "nebula_text_tags.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>

namespace {

int failed = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++failed;
    }
}

// A manifest of the form with `more` (members after the required ones, each with its leading comma).
std::string one_form(const std::string& id, const std::string& more) {
    return R"({"format":"1.0","id":")" + id + R"(","name":"N","version":"1.0.0","summary":"S","authors":["A"],"game":{"id":"RMG"})" + more + "}";
}

// One layer "l" whose old keys (pre_standard.layers.l) are `old`.
std::string old_layer(const std::string& old) {
    return one_form("abc", R"(,"layers":[{"name":"l"}],"pre_standard":{"layers":{"l":{)" + old + "}}}");
}

// pre_standard's top holding `old`.
std::string old_top(const std::string& old) {
    return one_form("abc", R"(,"layers":[],"pre_standard":{)" + old + "}");
}

constexpr const char* kGood = R"({
  "format": "1.0",
  "id": "kaizo",
  "name": "Kaizo",
  "summary": "Harder.\nMuch \"harder\" \u00e9 \ud83d\ude00",
  "version": "2.1.0",
  "game": { "id": "RMG", "regions": ["RMGE01", "RMGJ01"] },
  "authors": ["A", "B"],
  "license": "MIT",
  "links": { "home": "https://x.example", "source": "https://y.example" },
  "text": {
    "JpJapanese": { "name": "カイゾウ", "options": { "lives": { "name": "残機", "value_names": { "one": "一" } } } },
    "EuFrench": { "name": "Kaïzo", "summary": "Plus dur." }
  },
  "needs": [{ "id": "base-mod", "version": ">=1.2" }],
  "conflicts": [{ "id": "old-mod", "why": "Both change the lives." }, { "id": "other", "why": "Same stage." }],
  "load": { "after": ["base-mod"] },
  "options": [
    { "key": "lives", "kind": "choice", "name": "Lives", "default": "one", "values": ["off", "one"], "value_names": { "off": "Off", "one": "One" } },
    { "key": "bad", "kind": "choice", "name": "Falls back", "default": "nope", "values": ["first"], "value_names": { "first": "F" } },
    { "key": "hard", "kind": "toggle", "name": "Hard", "default": true },
    { "key": "label", "kind": "text", "name": "Label", "default": "x", "max_length": 8 }
  ],
  "layers": [
    { "name": "base" },
    { "name": "one", "when": { "option": { "lives": ["one"] } } },
    { "name": "jp", "when": { "region": ["RMGJ01"], "language": ["JpJapanese"], "platform": ["android-arm64", "windows-x64"] } },
    { "name": "odd", "when": { "someday": true } },
    { "name": "coded", "when": { "code": true } },
    { "name": "hard", "when": { "option": { "hard": true } } },
    { "name": "old", "when": { "language": ["UsEnglish"] } }
  ],
  "pre_standard": {
    "save": { "mode": "copy", "path": "save", "tag": "family", "player_can_change": true },
    "layers": {
      "base": { "tunables": { "GameRules.cDodoryuShellOnly": true, "SequenceUtil.cMissStarPieceNum": -999 } },
      "old": { "when": { "mods": ["base-mod"], "platform": ["windows"] } }
    }
  },
  "future_field": { "anything": [1, 2, { "x": null }] }
})";

}  // namespace

int main() {
    using namespace galaxy::luma;
    // The validator's id: 3 to 64 of a-z 0-9 - ., a letter first, no "..", no "." at the end; no name Windows keeps.
    for (const char* bad : {"../up", "a/b", "a\\b", "UPPER", "with space", "", "ab", "1ab", "-ab", "a..b", "abc.", "con", "nul",
                            "com1", "lpt9", "con.mod"}) {
        const std::string text = one_form(std::string(bad) == "a\\b" ? "a\\\\b" : bad, R"(,"layers":[])");
        check(!parse_manifest(text).has_value(), bad);
    }
    check(parse_manifest(one_form("a-1", R"(,"layers":[])")).has_value(), "a good id");
    check(parse_manifest(one_form("my.mod-2", R"(,"layers":[])")).has_value(), "an id with a dot");
    const std::optional<Manifest> read = parse_manifest(kGood);
    check(read.has_value(), "a good file reads");
    if (!read) return 1;
    const Manifest& m = *read;
    check(m.format == 1u && m.format_minor == 0u && m.id == "kaizo" && m.version == "2.1.0" && m.game == "RMG", "the plain fields");
    check(m.regions == std::vector<std::string>{"RMGE01", "RMGJ01"}, "game.regions");
    check(m.name.in("ja") == "カイゾウ", "a translation from text.<language>.name");
    check(m.name.in("fr-CA") == "Kaïzo", "the language without its region");
    check(m.name.in("de") == "Kaizo", "the name as given");
    check(m.description.fallback == "Harder.\nMuch \"harder\" \xC3\xA9 \xF0\x9F\x98\x80" && m.description.in("fr") == "Plus dur.",
          "summary: escapes and pairs, and its translation");
    check(m.license == "MIT" && m.homepage == "https://x.example", "license and links.home");
    check(m.authors.size() == 2u, "authors");
    check(m.needs.size() == 1u && m.needs[0].id == "base-mod" && m.needs[0].version == ">=1.2", "needs");
    check(m.conflicts.size() == 2u && m.conflicts[0].id == "old-mod" && m.conflicts[0].version.empty(), "conflicts by id");
    check(m.load_after == std::vector<std::string>{"base-mod"} && m.load_before.empty(), "load.after");
    check(m.options.size() == 3u, "choice, choice and toggle read; a text option is not (plan 6.3)");
    if (m.options.size() == 3u) {
        const Option& lives = m.options[0];
        check(lives.id == "lives" && lives.fallback == "one" && lives.choices.size() == 2u && lives.choices[1].name.fallback == "One" &&
                  lives.choices[1].name.in("ja") == "一" && lives.name.in("ja") == "残機",
              "a choice option, its value names and their translations");
        check(m.options[1].fallback == "first", "a default that isn't a value falls back to the first");
        check(m.options[2].choices.size() == 2u && m.options[2].choices[0].id == "off" && m.options[2].choices[1].id == "on" &&
                  m.options[2].fallback == "on",
              "a toggle is off and on, true starts on");
    }
    check(m.layers.size() == 7u && m.layers_listed && m.layers[0].path == "layers/base", "layers by name, at layers/<name>");
    check(m.layers.size() == 7u && std::any_of(m.layers[0].tunables.begin(), m.layers[0].tunables.end(), [](const Tunable& t) {
              return t.key == "SequenceUtil.cMissStarPieceNum" && t.value == -999.0;
          }),
          "a layer's old tunables from pre_standard.layers.<name>");
    check(m.save_path == "save" && m.save_mode == SaveMode::Copy && m.save_tag == "family" && m.save_player_can_change,
          "pre_standard's save");
    const std::optional<Manifest> plain = parse_manifest(one_form("abc", R"(,"layers":[])"));
    check(plain && plain->save_mode == SaveMode::Shared && plain->save_tag == "abc" && !plain->save_player_can_change,
          "normal saves unless pre_standard says");
    check(plain && plain->regions.empty(), "no game.regions: every disc");
    {
        // pre_standard's keys are read there only: the same keys at the top or in a form layer are not read.
        const std::optional<Manifest> top = parse_manifest(one_form("abc", R"(,"layers":[{"name":"l","tunables":{"A.b":1}}],"save":{"mode":"own"})"));
        check(top && top->layers.size() == 1u && top->layers[0].tunables.empty() && top->save_mode == SaveMode::Shared,
              "old keys outside pre_standard are not read (L220 names them)");
        const std::optional<Manifest> lone = parse_manifest(one_form("abc", R"(,"layers":[],"pre_standard":{"layers":{"gone":{"tunables":{"A.b":1}}}})"));
        check(lone && lone->layers.empty(), "a pre_standard layer the form does not list is not read");
    }
    {
        // Power-ups from data: the game's forms by name, whole numbers.
        const auto forms = [](const char* row) { return parse_manifest(old_layer(std::string(R"("forms":[)") + row + "]")); };
        const std::optional<Manifest> gold =
            forms(R"({"name":"gold","acts_like":"fire","looks_like":"fire","color_frame":2,"frames":600,"music":"BGM_FIRE_A"})");
        check(gold && gold->layers[0].forms.size() == 1u && gold->layers[0].forms[0].acts_like == 2 && gold->layers[0].forms[0].color_frame == 2 &&
                  gold->layers[0].forms[0].frames == 600 && !gold->layers[0].old_scripts,
              "forms: a power-up from data");
        check(forms(R"({"name":"plain"})") && forms(R"({"name":"plain"})")->layers[0].forms[0].acts_like == 0, "forms: normal unless it says");
        check(!forms(R"({"acts_like":"fire"})"), "forms: a name");
        check(!forms(R"({"name":"x","acts_like":"lava"})"), "forms: only the game's forms");
        check(!forms(R"({"name":"x","frames":1.5})"), "forms: whole frames");
        const std::optional<Manifest> scripted = forms(R"({"name":"x","script":"gold"})");
        check(scripted && scripted->layers[0].old_scripts, "forms: a script is the removed interpreter's: marked, the mod refused (mods.cpp)");
    }
    {
        // A player number past a float's range is held as infinite (the start names it and sets nothing), not cast.
        const std::optional<Manifest> huge =
            parse_manifest(old_layer(R"("player_constants":{"mario":{"mJumpHeight":1e300},"luigi":{"mJumpHeight":-1e300}})"));
        check(huge && huge->layers.size() == 1u && huge->layers[0].player_constants.size() == 2u &&
                  std::isinf(huge->layers[0].player_constants[0].value) && huge->layers[0].player_constants[0].value > 0.0f &&
                  std::isinf(huge->layers[0].player_constants[1].value) && huge->layers[0].player_constants[1].value < 0.0f,
              "a player number past a float's range is infinite");
    }
    {
        // The page's metadata: the icon and pictures stay inside the mod, at most kMaxPictures of them.
        std::string more = R"(,"layers":[],"icon":"../icon.png","pictures":["p/1.png","../2.png","/abs.png","C:/x.png","",3)";
        for (int i = 0; i < 20; ++i) more += ",\"many/" + std::to_string(i) + ".png\"";
        more += "]";
        const std::optional<Manifest> pictured = parse_manifest(one_form("abc", more));
        check(pictured && pictured->icon.empty(), "an icon outside the mod is dropped");
        check(pictured && pictured->pictures.size() == kMaxPictures && pictured->pictures[0] == "p/1.png" &&
                  pictured->pictures[1] == "many/0.png",
              "pictures: inside the mod only, the first kMaxPictures");
    }
    {
        // Achievements (pre_standard): each id once, a name needed; an event with its name and value, or none.
        const std::optional<Manifest> earned = parse_manifest(old_top(
            R"("achievements":[)"
            R"({"id":"first","name":{"default":"First","fr":"Premier"},"description":"Win a star","when":{"event":"star_get","name":"EggStarGalaxy","value":1}},)"
            R"({"id":"first","name":"Again"},{"id":"Bad Id","name":"x"},{"id":"nameless"},)"
            R"({"id":"secret","name":"Secret","hidden":true}])"));
        check(earned && earned->achievements.size() == 2u, "achievements: two of five kept");
        if (earned && earned->achievements.size() == 2u) {
            const Achievement& first = earned->achievements[0];
            check(first.id == "first" && first.name.in("fr") == "Premier" && first.description.fallback == "Win a star" &&
                      first.event == "star_get" && first.event_name == "EggStarGalaxy" && first.has_value && first.event_value == 1 && !first.hidden,
                  "an achievement's name, description and event");
            check(earned->achievements[1].hidden && earned->achievements[1].event.empty(), "a hidden one code unlocks");
        }
    }

    if (m.layers.size() == 7u) {
        Situation s;
        s.region = "RMGE01";
        s.version = "E";
        s.language = "UsEnglish";
        s.platform = "windows-x64";
        s.options["lives"] = "off";
        s.options["hard"] = "off";
        check(layer_used(m.layers[0], s), "no conditions: always");
        check(!layer_used(m.layers[1], s), "an option not chosen");
        s.options["lives"] = "one";
        check(layer_used(m.layers[1], s), "an option chosen");
        check(!layer_used(m.layers[2], s), "another region");
        s.region = "RMGJ01";
        s.language = "JpJapanese";
        check(layer_used(m.layers[2], s), "every condition holds");
        s.platform = "linux-x64";
        check(!layer_used(m.layers[2], s), "another device");
        check(!layer_used(m.layers[3], s), "a condition this game doesn't know: not used");
        check(!layer_used(m.layers[4], s), "code: true while the code does not run");
        s.code = true;
        check(layer_used(m.layers[4], s), "code: true while it runs");
        check(!layer_used(m.layers[5], s), "a toggle off");
        s.options["hard"] = "on";
        check(layer_used(m.layers[5], s), "a toggle on");
        s.language = "UsEnglish";
        s.platform = "windows-x64";
        check(!layer_used(m.layers[6], s), "the old when: a mod not on");
        s.mods = {"base-mod"};
        check(layer_used(m.layers[6], s), "the old when and the form's both hold; an operating system's name holds for its machines");
        s.platform = "windows-arm64";
        check(layer_used(m.layers[6], s), "every machine of it");
        s.platform = "windowsx";
        check(!layer_used(m.layers[6], s), "a name it only starts with is not it");
    }

    // luma-format 6: code/<platform>/mod.<ending>, by `code.platforms` alone; the one route for code.
    {
        const std::string here(plugin_target());
        const auto coded_with = [](const std::string& code) { return parse_manifest(one_form("abc", R"(,"layers":[],)" + code)); };
        const std::optional<Manifest> coded =
            coded_with(R"("code":{"platforms":[")" + here + R"(","linux-arm64"],"libraries":{"linux-arm64":["libphysics.so"]}})");
        check(coded && coded->plugins.size() == 1u, "code: one entry library per platform");
        if (coded && coded->plugins.size() == 1u) {
            check(coded->plugins[0].binaries.at(here) == code_entry(here) &&
                      coded->plugins[0].binaries.at("linux-arm64") == "code/linux-arm64/mod.so",
                  "code: each platform's entry library by its path");
            check(coded->code_libraries.at("linux-arm64") == std::vector<std::string>{"libphysics.so"}, "code: extra libraries in order");
            check(manifest_sets_game(*coded), "code for this device sets the game");
        }
        check(code_entry("windows-x64") == "code/windows-x64/mod.dll" && code_entry("macos-arm64") == "code/macos-arm64/mod.dylib" &&
                  code_entry("ios-arm64") == "code/ios-arm64/mod.dylib" && code_entry("android-x64") == "code/android-x64/mod.so" &&
                  code_entry("windows").empty(),
              "code_entry: the format's endings; none for an unknown platform");
        check(!coded_with(R"("code":{"platforms":["somewhere-else"]})"), "code: a platform the format does not name: not read");
        const std::optional<Manifest> other =
            coded_with(R"("code":{"platforms":[")" + std::string(here == "linux-arm64" ? "linux-x64" : "linux-arm64") + R"("]})");
        check(other && other->plugins.size() == 1u && other->plugins[0].binaries.count(here) == 0u,
              "code for another device only: read, no entry here (L507 says so)");
        const std::optional<Manifest> old = coded_with(R"("plugins":[{"api":3,"binaries":{")" + here + R"(":"a.dll"}}])");
        check(old && old->plugins.empty(), "the old \"plugins\" route is gone: nothing read from it");
        for (const char* bad : {R"("code":[])", R"("code":{"platforms":["linux-x64","linux-x64"]})",
                                R"("code":{"platforms":["linux-x64"],"libraries":{"linux-arm64":["a.so"]}})",
                                R"("code":{"platforms":["linux-x64"],"libraries":{"linux-x64":["sub/a.so"]}})",
                                R"("code":{"platforms":["linux-x64"],"libraries":{"linux-x64":[".."]}})",
                                R"("code":{"platforms":["linux-x64"],"libraries":{"linux-x64":["mod.so"]}})"}) {
            check(!coded_with(bad), (std::string("code not read: ") + bad).c_str());
        }
    }

    // The removed interpreter's rows (pre_standard "objects", "scripts", a class's "script"): nothing read, the layer
    // marked, and the mod refused with its reason (mods.cpp); never read as if they would run (plan 1.10b).
    {
        const std::optional<Manifest> objects = parse_manifest(old_layer(
            R"("objects":[{"name":"GlideBlock","script":"blocks/glide","archive":"Kinopio","kind":"map"}])"));
        check(objects && objects->layers.size() == 1u && objects->layers[0].old_scripts, "objects with scripts: marked");
        const std::optional<Manifest> wide = parse_manifest(old_layer(R"("scripts":["main","rules/coins"])"));
        check(wide && wide->layers.size() == 1u && wide->layers[0].old_scripts, "mod-wide scripts: marked");
        const std::optional<Manifest> on_class =
            parse_manifest(old_layer(R"("object_classes":[{"name":"Kuribo","script":"walk"},{"name":"Big","like":"Kuribo"}])"));
        check(on_class && on_class->layers.size() == 1u && on_class->layers[0].old_scripts && on_class->layers[0].object_classes.size() == 1u &&
                  on_class->layers[0].object_classes[0].like == "Kuribo",
              "a class's script: marked; a class made like another still read");
        const std::optional<Manifest> plain = parse_manifest(old_layer(R"("object_classes":[{"name":"Big","like":"Kuribo"}])"));
        check(plain && !plain->layers.empty() && !plain->layers[0].old_scripts, "no script: not marked");
        check(!parse_manifest(old_layer(R"("object_classes":[{"name":"Big"}])")), "a class row without like: refused");
    }

    const std::optional<Manifest> newer = parse_manifest(R"({"format":"2.0","id":"later","everything":"different"})");
    check(newer && newer->format == 2u && newer->id == "later" && newer->name.fallback.empty(),
          "a newer format reads its format and id only (L205 refuses it), never called damaged");
    const std::optional<Manifest> minor = parse_manifest(R"({"format":"1.7","id":"later","name":"x"})");
    check(minor && minor->format_minor == 7u && minor->name.fallback.empty(), "a newer minor too");

    for (const char* bad : {"", "{", "[]", "not json", R"({"format":"1.0"})", R"({"format":1,"id":"abc"})", R"({"format":"1","id":"abc"})",
                            R"({"format":"01.0","id":"abc"})", R"({"format":"1.0x","id":"abc"})", R"({"format":"1.0","id":"abc","layers":[{"name":"../up"}]})",
                            R"({"format":"1.0","id":"abc","layers":[{"name":"a/b"}]})", R"({"format":"1.0","id":"abc","layers":[{"name":"C:"}]})",
                            R"({"format":"1.0","id":"abc","layers":[{"name":".. "}]})", R"({"format":"1.0","id":"abc","layers":[{"name":""}]})",
                            R"({"format":"1.0","id":"abc","layers":[],"pre_standard":{"save":{"mode":"own","tag":"../../up"}}})",
                            R"({"format":"1.0","id":"abc","layers":[]} trailing)", R"({"a":"\ud800"})", R"({"a":"unterminated)"}) {
        check(!parse_manifest(bad).has_value(), bad);
    }
    std::string deep(100000, '[');
    check(!parse_manifest(deep).has_value(), "nesting past what a manifest needs");
    check(parse_manifest(std::string("\xEF\xBB\xBF") + kGood).has_value(), "a file saved with a byte order mark");

    check(compare_versions("2.10", "2.9") > 0, "2.10 is above 2.9");
    check(compare_versions("2.1", "2.1.0") == 0, "2.1 equals 2.1.0");
    check(compare_versions("1", "1.0.1") < 0, "1 is below 1.0.1");
    check(language_code("EuFrench") == "fr" && language_code("JpJapanese") == "ja" && language_code("?") == "en", "language codes");
    {
        // A language an installed language mod adds keys its own text by its folder name, never as English.
        const std::optional<Manifest> extra = parse_manifest(one_form("abc", R"(,"layers":[],"text":{"EuCatalan":{"name":"Català"}})"));
        check(extra && extra->name.in("en") == "N" && extra->name.in("EuCatalan") == "Català", "an added language's own key");
    }

    // Rows (pre_standard): a dome is any number from 1 (the game checks it against the domes its data has), a number is
    // cut to its type.
    {
        const std::optional<Manifest> comets = parse_manifest(old_top(
            R"("comet_schedule":[{"dome":1,"comets":[{"galaxy":"EggStarGalaxy","coins":0}]},{"dome":17,"comets":[{"galaxy":"Other","coins":1}]}])"));
        check(comets && comets->comet_schedule.size() == 2u && comets->comet_schedule[0].dome == 1 && comets->comet_schedule[1].dome == 17 &&
                  comets->comet_schedule[1].comets.size() == 1u && comets->comet_schedule[1].comets[0].second == 1,
              "comet domes past 16 are read");
        const std::optional<Manifest> huge = parse_manifest(old_top(R"("comet_schedule":[{"dome":1e300,"comets":[{"galaxy":"G","coins":0}]}])"));
        check(huge && huge->comet_schedule.size() == 1u && huge->comet_schedule[0].dome == std::numeric_limits<int>::max(),
              "a dome past the int range stops at its end");
        check(!parse_manifest(old_top(R"("comet_schedule":[{"dome":0,"comets":[{"galaxy":"G","coins":0}]}])")).has_value(), "dome 0 is refused");
        check(!parse_manifest(old_top(R"("comet_schedule":[{"dome":-5,"comets":[{"galaxy":"G","coins":0}]}])")).has_value(), "a negative dome is refused");
        check(!parse_manifest(old_top(R"("comet_schedule":[{"dome":2,"comets":[]}])")).has_value(), "a dome with no comets is refused");
    }
    {
        check(parse_manifest(old_top(R"("galaxies":[{"name":"G","stars_to_open":255,"dome":255}])")).has_value(), "stars and dome up to a byte");
        check(!parse_manifest(old_top(R"("galaxies":[{"name":"G","stars_to_open":256}])")).has_value(), "stars past a byte are refused");
        check(!parse_manifest(old_top(R"("galaxies":[{"name":"G","dome":256}])")).has_value(), "a galaxy's dome past a byte is refused");
        check(!parse_manifest(old_top(R"("galaxies":[{"name":"G","dome":-1}])")).has_value(), "a negative dome is refused");
        check(!parse_manifest(old_top(R"("galaxies":[{"name":"G","stars_to_open":1e300}])")).has_value(), "a number past the int range is refused, not cast");
    }
    {
        // A file's tuning numbers (pre_standard): a number, a bool, or an option's choice; a key without a file, or
        // another kind of value, refuses the file.
        const std::optional<Manifest> tuned =
            parse_manifest(old_layer(R"("tunables":{"Kuribo.cWalkSpeed":2.5,"Kuribo.cIsSmall":true,"Koopa.cHp":{"option":"hp"}})"));
        check(tuned && tuned->layers.size() == 1u && tuned->layers[0].tunables.size() == 3u, "tunables read");
        if (tuned && tuned->layers.size() == 1u && tuned->layers[0].tunables.size() == 3u) {
            const std::vector<Tunable>& rows = tuned->layers[0].tunables;
            const auto find = [&](const char* key) -> const Tunable* {
                for (const Tunable& row : rows) {
                    if (row.key == key) return &row;
                }
                return nullptr;
            };
            const Tunable* walk = find("Kuribo.cWalkSpeed");
            const Tunable* small = find("Kuribo.cIsSmall");
            const Tunable* hp = find("Koopa.cHp");
            check(walk != nullptr && walk->value == 2.5 && walk->option.empty(), "a number");
            check(small != nullptr && small->value == 1.0, "a bool is 1 or 0");
            check(hp != nullptr && hp->option == "hp", "an option's choice");
        }
        check(!parse_manifest(old_layer(R"("tunables":{"cWalkSpeed":2.0})")).has_value(), "a key without its file is refused");
        check(!parse_manifest(old_layer(R"("tunables":{"Kuribo.cWalkSpeed":"fast"})")).has_value(), "text is refused");
        check(!parse_manifest(old_layer(R"("tunables":[1,2])")).has_value(), "tunables as a list is refused");
        const std::optional<Manifest> listed = parse_manifest(old_layer(R"("tunables":{"Kuribo.cSteps":[4,null,9]})"));
        check(listed && listed->layers.size() == 1u && listed->layers[0].tunables.size() == 2u &&
              listed->layers[0].tunables[0].key == "Kuribo.cSteps[0]" && listed->layers[0].tunables[0].value == 4.0 &&
              listed->layers[0].tunables[1].key == "Kuribo.cSteps[2]" && listed->layers[0].tunables[1].value == 9.0,
              "a list sets its places, a null keeps one");
        check(!parse_manifest(old_layer(R"("tunables":{"Kuribo.cSteps":[4,"x"]})")).has_value(), "text in a list is refused");
        // Player animations: name and file, a rate over 0 up to 100, whole blend frames up to 600.
        const std::optional<Manifest> moves = parse_manifest(old_layer(
            R"("player_animations":[{"name":"GoldSpin","file":"GoldSpin","rate":0.5,"blend_frames":4},{"name":"Jump","file":"MyJump"}])"));
        check(moves && moves->layers.size() == 1u && moves->layers[0].player_animations.size() == 2u &&
              moves->layers[0].player_animations[0].name == "GoldSpin" && moves->layers[0].player_animations[0].rate == 0.5 &&
              moves->layers[0].player_animations[0].blend_frames == 4 && moves->layers[0].player_animations[1].file == "MyJump" &&
              moves->layers[0].player_animations[1].rate == 1.0 && moves->layers[0].player_animations[1].blend_frames == 0,
              "player animations read");
        for (const char* bad : {R"("player_animations":[{"name":"A"}])", R"("player_animations":[{"name":"A","file":"B","rate":0}])",
                                R"("player_animations":[{"name":"A","file":"B","rate":"fast"}])", R"("player_animations":[{"name":"A","file":"B","blend_frames":2.5}])",
                                R"("player_animations":[{"name":"A","file":"B","blend_frames":601}])", R"("player_animations":{"name":"A"})"}) {
            check(!parse_manifest(old_layer(bad)).has_value(), bad);
        }
    }
    {
        // A number option: min to max by step, each a choice; counted, so numbers too large for the step cannot loop.
        const std::optional<Manifest> ranged = parse_manifest(one_form("abc", std::string(R"(,"layers":[],"options":[)") +
            R"({"key":"r1","kind":"number","name":"R","default":1,"min":1,"max":5,"step":1},)" +
            R"({"key":"r2","kind":"number","name":"R","default":1e300,"min":1e300,"max":1e300,"step":1},)" +
            R"({"key":"r3","kind":"number","name":"R","default":1,"min":5,"max":1,"step":1},)" +
            R"({"key":"r4","kind":"number","name":"R","default":0.5,"min":0,"max":1,"step":0.25}])"));
        check(ranged && ranged->options.size() == 3u, "number options read; one with min above max has no choices and is not");
        if (ranged && ranged->options.size() == 3u) {
            check(ranged->options[0].choices.size() == 5u && ranged->options[0].choices[4].id == "5" && ranged->options[0].fallback == "1",
                  "1 to 5 gives five choices");
            check(ranged->options[1].choices.size() == 1u, "a range of one huge number gives one choice and ends");
            check(ranged->options[2].choices.size() == 5u && ranged->options[2].choices[1].id == "0.25" && ranged->options[2].fallback == "0.5",
                  "fractional steps");
        }
    }
    {
        // The manifest file is read only when it is a sensible size; with no "layers", layers/base when it is there.
        const std::filesystem::path folder = galaxy::test_temp_path("nebula_luma_test_manifest");
        std::error_code error;
        std::filesystem::remove_all(folder, error);
        std::filesystem::create_directories(folder, error);
        {
            std::ofstream out(folder / "luma.json", std::ios::binary);
            out << one_form("abc", "");
        }
        const std::optional<Manifest> small = read_manifest(folder);
        check(small.has_value() && small->layers.empty(), "a small luma.json is read; no layers without layers/base");
        std::filesystem::create_directories(folder / "layers" / "base", error);
        const std::optional<Manifest> based = read_manifest(folder);
        check(based && based->layers.size() == 1u && based->layers[0].path == "layers/base", "no \"layers\": layers/base");
        {
            std::ofstream out(folder / "luma.json", std::ios::binary | std::ios::trunc);
            out << R"({"format":"1.0","id":"abc","name":"b","version":"1.0.0","game":{"id":"RMG"},"summary":")";
            const std::string filler(17u << 20u, 'x');
            out << filler << R"("})";
        }
        check(!read_manifest(folder).has_value(), "a luma.json past 16 MB is not read");
        std::filesystem::remove_all(folder, error);
    }

    {
        // A message's words (pre_standard text rows): "like" names the game message whose talk a new line takes; it is
        // not a language.
        const std::optional<Manifest> talk = parse_manifest(old_top(
            R"("text":{"MyNpc_001":{"default":"Hi [player]!","fr":"Salut [player] !","like":"AstroGalaxy_Tico034"},"GalaxyName_X":"X"})"));
        check(talk.has_value(), "text with like reads");
        if (talk) {
            check(talk->text.size() == 2u, "two lines");
            check(talk->text_like.size() == 1u && talk->text_like.at("MyNpc_001") == "AstroGalaxy_Tico034", "like read");
            check(talk->text.at("MyNpc_001").languages.size() == 1u && talk->text.at("MyNpc_001").in("fr") == "Salut [player] !", "like is not a language");
            check(talk->text.at("MyNpc_001").in("de") == "Hi [player]!", "the default");
        }
    }
    {
        // Mod text as the game's message text: the game's tags in brackets, as its own messages hold them.
        const auto encoded = [](const char* utf8) {
            std::u16string out(nebula_text::encode(utf8, nullptr, 0), u'\0');
            std::u16string buffer(out.size() + 1u, u'\0');
            nebula_text::encode(utf8, buffer.data(), static_cast<uint32_t>(buffer.size()));
            out.assign(buffer.data(), out.size());
            return out;
        };
        // A tag: 0x1A, length and group (length in the low byte), tag number, parameters.
        check(encoded("A[wait 30]B") == std::u16string({u'A', 0x1A, 8 | (1 << 8), 0, 30, u'B'}), "wait");
        check(encoded("[page]") == std::u16string({0x1A, 6 | (1 << 8), 1}), "page");
        check(encoded("[color 1]x[color 0]") == std::u16string({0x1A, 8 | (255 << 8), 0, 0x100, u'x', 0x1A, 8 | (255 << 8), 0, 0}),
              "colour in the parameter's high byte, as the game's 2359 colour tags");
        check(encoded("[player]") == std::u16string({0x1A, 8 | (5 << 8), 0, 0}), "the player's name");
        check(encoded("[icon 7][LARGE]") == std::u16string({0x1A, 6 | (3 << 8), 7, 0x1A, 6 | (4 << 8), 2}), "icon and size, any case");
        check(encoded("[sound SE_A]") == std::u16string({0x1A, 14 | (2 << 8), 0, u'S', u'E', u'_', u'A'}), "a sound by name");
        check(encoded("[tag 6 5 0 0x1 2]") == std::u16string({0x1A, 12 | (6 << 8), 5, 0, 1, 2}), "any tag");
        check(encoded("[[a] [nope 1] [wait] [wait x] [color 256]") == u"[a] [nope 1] [wait] [wait x] [color 256]", "anything else stays as written");
        check(encoded("[wait 1") == u"[wait 1", "an unclosed bracket stays");
        check(encoded("\xC3\xA9\xF0\x9F\x98\x80") == std::u16string({0xE9, 0xD83D, 0xDE00}), "UTF-8 to UTF-16");
        char16_t small[4];
        check(nebula_text::encode("abcdef", small, 4) == 6u && small[2] == u'c' && small[3] == 0, "cut to its room, with its 0");
    }

    if (failed == 0) std::puts("luma: all passed");
    return failed == 0 ? 0 : 1;
}
