// SPDX-License-Identifier: GPL-3.0-only
// luma.json as docs/format.md has it: every field, every refusal, and what a person might get wrong.
#include "galaxy/luma.h"

#include <cstdio>
#include <string>

namespace {

int failed = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++failed;
    }
}

constexpr const char* kGood = R"({
  "format": 1,
  "id": "kaizo",
  "name": { "default": "Kaizo", "ja": "カイゾウ", "fr": "Kaïzo" },
  "version": "2.1.0",
  "game": "RMG",
  "authors": ["A", "B"],
  "description": "Harder.\nMuch \"harder\" \u00e9 \ud83d\ude00",
  "future_field": { "anything": [1, 2, { "x": null }] },
  "needs": [{ "id": "base-mod", "version": "1.2" }],
  "conflicts": [{ "id": "old-mod", "below": "2.0" }, { "id": "other" }],
  "load_after": ["base-mod"],
  "options": [
    { "id": "lives", "name": "Lives", "default": "one", "choices": [ { "id": "off", "name": "Off" }, { "id": "one", "name": { "default": "One", "ja": "一" } } ] },
    { "id": "bad", "name": "Falls back", "default": "nope", "choices": [ { "id": "first", "name": "F" } ] }
  ],
  "layers": [
    { "path": "layers/base", "switches": { "dodoryu_shell_only": 1, "miss_star_pieces": 4294966297 } },
    { "path": "layers/one", "when": { "option": { "lives": ["one"] } } },
    { "path": "layers/jp", "when": { "version": ["J"], "language": ["JpJapanese"], "platform": ["android", "windows"] } },
    { "when": { "someday": true } }
  ],
  "save": { "mode": "copy", "path": "save", "tag": "family", "player_can_change": true }
})";

}  // namespace

int main() {
    using namespace galaxy::luma;
    for (const char* bad : {"../up", "a/b", "a\\b", "UPPER", "with space", "", "con", "nul", "com1", "lpt9", "-a", "a-"}) {
        const std::string text = std::string(R"({"format":1,"id":")") + (std::string(bad) == "a\\b" ? "a\\\\b" : bad) + R"(","name":"n","version":"1.0.0","game":"RMG","layers":[]})";
        check(!parse_manifest(text).has_value(), bad);
    }
    check(parse_manifest(R"({"format":1,"id":"a-1","name":"n","version":"1.0.0","game":"RMG","layers":[]})").has_value(), "a good id");
    const std::optional<Manifest> read = parse_manifest(kGood);
    check(read.has_value(), "a good file reads");
    if (!read) return 1;
    const Manifest& m = *read;
    check(m.format == 1u && m.id == "kaizo" && m.version == "2.1.0" && m.game == "RMG", "the plain fields");
    check(m.name.in("ja") == "カイゾウ", "a translation");
    check(m.name.in("fr-CA") == "Kaïzo", "the language without its region");
    check(m.name.in("de") == "Kaizo", "the default");
    check(m.description.fallback == "Harder.\nMuch \"harder\" \xC3\xA9 \xF0\x9F\x98\x80", "escapes and pairs");
    check(m.authors.size() == 2u, "authors");
    check(m.needs.size() == 1u && m.needs[0].id == "base-mod" && m.needs[0].version == "1.2", "needs");
    check(m.conflicts.size() == 2u && m.conflicts[0].version == "2.0" && m.conflicts[1].version.empty(), "conflicts");
    check(m.load_after.size() == 1u, "load_after");
    check(m.options.size() == 2u && m.options[0].fallback == "one", "options");
    check(m.options[1].fallback == "first", "a default that isn't a choice falls back to the first");
    check(m.layers.size() == 4u && m.layers[0].switches.at("miss_star_pieces") == 4294966297u, "a switch's value");
    check(m.save_path == "save" && m.save_mode == SaveMode::Copy && m.save_tag == "family" && m.save_player_can_change, "save");
    const std::optional<Manifest> plain = parse_manifest(R"({"format":1,"id":"a","name":"b","game":"RMG","layers":[]})");
    check(plain && plain->save_mode == SaveMode::Shared && plain->save_tag == "a" && !plain->save_player_can_change, "normal saves unless it says");

    Situation s;
    s.version = "E";
    s.language = "UsEnglish";
    s.platform = "windows";
    s.options["lives"] = "off";
    check(layer_used(m.layers[0], s), "no conditions: always");
    check(!layer_used(m.layers[1], s), "an option not chosen");
    s.options["lives"] = "one";
    check(layer_used(m.layers[1], s), "an option chosen");
    check(!layer_used(m.layers[2], s), "another version");
    s.version = "J";
    s.language = "JpJapanese";
    check(layer_used(m.layers[2], s), "every condition holds");
    s.platform = "linux";
    check(!layer_used(m.layers[2], s), "another device");
    check(!layer_used(m.layers[3], s), "a condition this game doesn't know: not used");

    check(refusal(m, "RMGE01", "2.1.0", "windows").empty(), "usable");
    Manifest later = m;
    later.nebula = "9.0";
    check(refusal(later, "RMGE01", "2.1.0", "windows") == "THIS MOD NEEDS A NEWER NEBULA", "a newer Nebula");
    later = m;
    later.requires_features = {"thing"};
    check(refusal(later, "RMGE01", "2.1.0", "windows") == "THIS MOD NEEDS A NEWER NEBULA", "a feature this game lacks");
    later = m;
    later.game = "SB4";
    check(refusal(later, "RMGE01", "2.1.0", "windows") == "THIS MOD IS FOR SUPER MARIO GALAXY 2", "Galaxy 2");
    later.game = "XYZ";
    check(refusal(later, "RMGE01", "2.1.0", "windows") == "THIS MOD IS FOR ANOTHER GAME", "another game");
    later = m;
    later.versions = {"P"};
    check(refusal(later, "RMGE01", "2.1.0", "windows") == "THIS MOD DOESN'T WORK ON THIS VERSION OF THE GAME", "a version left out");
    check(refusal(later, "RMGP01", "2.1.0", "windows").empty(), "the version it names");
    later = m;
    later.plugins.push_back({{{"somewhere-else", "plugins/a.so"}}, kPluginApi, {}});
    check(refusal(later, "RMGE01", "2.1.0", "windows") == "THIS MOD HAS NO VERSION FOR THIS DEVICE", "a plugin with no build for this device");
    later.plugins[0].binaries[std::string(plugin_target())] = "plugins/a.dll";
    check(refusal(later, "RMGE01", "2.1.0", "windows").empty(), "a plugin with a build for this device");
    later.plugins[0].api = kPluginApi + 1u;
    check(refusal(later, "RMGE01", "2.1.0", "windows") == "THIS MOD NEEDS A NEWER NEBULA", "a plugin for a later game");

    const std::optional<Manifest> newer = parse_manifest(R"({"format":2,"everything":"different"})");
    check(newer && refusal(*newer, "RMGE01", "2.1.0", "windows") == "THIS MOD NEEDS A NEWER NEBULA", "a newer format is named, not damaged");

    for (const char* bad : {"", "{", "[]", "not json", R"({"format":1})", R"({"format":0,"id":"a","name":"b","game":"RMG","layers":[]})",
                            R"({"format":1,"id":"a","name":"b","game":"RMG"})", R"({"format":1,"id":"a","name":"b","game":"RMG","layers":[{"path":"../up"}]})",
                            R"({"format":1,"id":"a","name":"b","game":"RMG","layers":[{"path":"C:/up"}]})", R"({"format":1,"id":"a","name":"b","game":"RMG","layers":[{"path":"/up"}]})",
                            R"({"format":1,"id":"a","name":"b","game":"RMG","layers":[]} trailing)", R"({"a":"\ud800"})", R"({"a":"unterminated)"}) {
        check(!parse_manifest(bad).has_value(), bad);
    }
    std::string deep(100000, '[');
    check(!parse_manifest(deep).has_value(), "nesting past what a manifest needs");
    check(parse_manifest(std::string("\xEF\xBB\xBF") + kGood).has_value(), "a file saved with a byte order mark");

    check(compare_versions("2.10", "2.9") > 0, "2.10 is above 2.9");
    check(compare_versions("2.1", "2.1.0") == 0, "2.1 equals 2.1.0");
    check(compare_versions("1", "1.0.1") < 0, "1 is below 1.0.1");

    check(switch_number("dodoryu_shell_only") != 0u && switch_number("nope") == 0u, "switches by name");
    check(language_code("EuFrench") == "fr" && language_code("JpJapanese") == "ja" && language_code("?") == "en", "language codes");

    if (failed == 0) std::puts("luma: all passed");
    return failed == 0 ? 0 : 1;
}
