// SPDX-License-Identifier: GPL-3.0-only
// luma.json as docs/format.md has it: every field, every refusal, and what a person might get wrong.
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
    { "path": "layers/base", "tunables": { "GameRules.cDodoryuShellOnly": true, "SequenceUtil.cMissStarPieceNum": -999 } },
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
    check(m.layers.size() == 4u && std::any_of(m.layers[0].tunables.begin(), m.layers[0].tunables.end(), [](const Tunable& t) {
              return t.key == "SequenceUtil.cMissStarPieceNum" && t.value == -999.0;
          }),
          "a layer's tunable");
    check(m.save_path == "save" && m.save_mode == SaveMode::Copy && m.save_tag == "family" && m.save_player_can_change, "save");
    const std::optional<Manifest> plain = parse_manifest(R"({"format":1,"id":"a","name":"b","game":"RMG","layers":[]})");
    check(plain && plain->save_mode == SaveMode::Shared && plain->save_tag == "a" && !plain->save_player_can_change, "normal saves unless it says");
    {
        // A player number past a float's range is held as infinite (the start names it and sets nothing), not cast.
        const std::optional<Manifest> huge = parse_manifest(
            R"({"format":1,"id":"a","name":"b","game":"RMG","layers":[{"player_constants":{"mario":{"mJumpHeight":1e300},"luigi":{"mJumpHeight":-1e300}}}]})");
        check(huge && huge->layers.size() == 1u && huge->layers[0].player_constants.size() == 2u &&
                  std::isinf(huge->layers[0].player_constants[0].value) && huge->layers[0].player_constants[0].value > 0.0f &&
                  std::isinf(huge->layers[0].player_constants[1].value) && huge->layers[0].player_constants[1].value < 0.0f,
              "a player number past a float's range is infinite");
    }
    {
        // The page's metadata: the icon and pictures stay inside the mod, at most kMaxPictures of them.
        std::string text = R"({"format":1,"id":"a","name":"b","game":"RMG","layers":[],"license":"MIT","homepage":"https://x.example",)"
                           R"("icon":"../icon.png","pictures":["p/1.png","../2.png","/abs.png","C:/x.png","",3)";
        for (int i = 0; i < 20; ++i) text += ",\"many/" + std::to_string(i) + ".png\"";
        text += "]}";
        const std::optional<Manifest> pictured = parse_manifest(text);
        check(pictured && pictured->icon.empty(), "an icon outside the mod is dropped");
        check(pictured && pictured->license == "MIT" && pictured->homepage == "https://x.example", "license and homepage");
        check(pictured && pictured->pictures.size() == kMaxPictures && pictured->pictures[0] == "p/1.png" &&
                  pictured->pictures[1] == "many/0.png",
              "pictures: inside the mod only, the first kMaxPictures");
    }
    {
        // Achievements: each id once, a name needed; an event with its name and value, or none (a plugin's).
        const std::optional<Manifest> earned = parse_manifest(
            R"({"format":1,"id":"a","name":"b","game":"RMG","layers":[],"achievements":[)"
            R"({"id":"first","name":{"default":"First","fr":"Premier"},"description":"Win a star","when":{"event":"star_get","name":"EggStarGalaxy","value":1}},)"
            R"({"id":"first","name":"Again"},{"id":"Bad Id","name":"x"},{"id":"nameless"},)"
            R"({"id":"secret","name":"Secret","hidden":true}]})");
        check(earned && earned->achievements.size() == 2u, "achievements: two of five kept");
        if (earned && earned->achievements.size() == 2u) {
            const Achievement& first = earned->achievements[0];
            check(first.id == "first" && first.name.in("fr") == "Premier" && first.description.fallback == "Win a star" &&
                      first.event == "star_get" && first.event_name == "EggStarGalaxy" && first.has_value && first.event_value == 1 && !first.hidden,
                  "an achievement's name, description and event");
            check(earned->achievements[1].hidden && earned->achievements[1].event.empty(), "a hidden one a plugin unlocks");
        }
    }

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
    later.plugins[0].api = kPluginApi - 1u;
    check(refusal(later, "RMGE01", "2.1.0", "windows") == "THIS MOD'S CODE IS FOR AN OLDER NEBULA: REBUILD IT WITH THIS NEBULA'S NEBULA_MOD_API.H",
          "a plugin for plugin API 1 names the replacement");

    const std::optional<Manifest> newer = parse_manifest(R"({"format":2,"everything":"different"})");
    check(newer && refusal(*newer, "RMGE01", "2.1.0", "windows") == "THIS MOD NEEDS A NEWER NEBULA", "a newer format is named, not damaged");

    for (const char* bad : {"", "{", "[]", "not json", R"({"format":1})", R"({"format":0,"id":"a","name":"b","game":"RMG","layers":[]})",
                            R"({"format":1,"id":"a","name":"b","game":"RMG"})", R"({"format":1,"id":"a","name":"b","game":"RMG","layers":[{"path":"../up"}]})",
                            R"({"format":1,"id":"a","name":"b","game":"RMG","layers":[{"path":"C:/up"}]})", R"({"format":1,"id":"a","name":"b","game":"RMG","layers":[{"path":"/up"}]})",
                            R"({"format":1,"id":"a","name":"b","game":"RMG","layers":[{"path":"a/.. /up"}]})",
                            R"({"format":1,"id":"a","name":"b","game":"RMG","layers":[{"path":"..\u0000/up"}]})",
                            R"({"format":1,"id":"a","name":"b","game":"RMG","layers":[{"path":"a/.../up"}]})",
                            R"({"format":1,"id":"a","name":"b","game":"RMG","layers":[],"save":{"mode":"own","tag":"../../up"}})",
                            R"({"format":1,"id":"a","name":"b","game":"RMG","layers":[]} trailing)", R"({"a":"\ud800"})", R"({"a":"unterminated)"}) {
        check(!parse_manifest(bad).has_value(), bad);
    }
    std::string deep(100000, '[');
    check(!parse_manifest(deep).has_value(), "nesting past what a manifest needs");
    check(parse_manifest(std::string("\xEF\xBB\xBF") + kGood).has_value(), "a file saved with a byte order mark");

    check(compare_versions("2.10", "2.9") > 0, "2.10 is above 2.9");
    check(compare_versions("2.1", "2.1.0") == 0, "2.1 equals 2.1.0");
    check(compare_versions("1", "1.0.1") < 0, "1 is below 1.0.1");

    check(switch_tunable("dodoryu_shell_only").key == "GameRules.cDodoryuShellOnly" && !switch_tunable("dodoryu_shell_only").opposite &&
              switch_tunable("no_low_health_warning").opposite && switch_tunable("nope").key.empty(),
          "a switch's tunable by its name");
    check(language_code("EuFrench") == "fr" && language_code("JpJapanese") == "ja" && language_code("?") == "en", "language codes");

    // Rows: a dome is any number from 1 (the game checks it against the domes its data has), a number is cut to its type.
    const auto with_rows = [](const std::string& rows) {
        return std::string(R"({"format":1,"id":"a","name":"b","version":"1.0.0","game":"RMG",)") + rows + R"(,"layers":[]})";
    };
    {
        const std::optional<Manifest> comets = parse_manifest(with_rows(
            R"("comet_schedule":[{"dome":1,"comets":[{"galaxy":"EggStarGalaxy","coins":0}]},{"dome":17,"comets":[{"galaxy":"Other","coins":1}]}])"));
        check(comets && comets->comet_schedule.size() == 2u && comets->comet_schedule[0].dome == 1 && comets->comet_schedule[1].dome == 17 &&
                  comets->comet_schedule[1].comets.size() == 1u && comets->comet_schedule[1].comets[0].second == 1,
              "comet domes past 16 are read");
        const std::optional<Manifest> huge = parse_manifest(with_rows(R"("comet_schedule":[{"dome":1e300,"comets":[{"galaxy":"G","coins":0}]}])"));
        check(huge && huge->comet_schedule.size() == 1u && huge->comet_schedule[0].dome == std::numeric_limits<int>::max(),
              "a dome past the int range stops at its end");
        check(!parse_manifest(with_rows(R"("comet_schedule":[{"dome":0,"comets":[{"galaxy":"G","coins":0}]}])")).has_value(), "dome 0 is refused");
        check(!parse_manifest(with_rows(R"("comet_schedule":[{"dome":-5,"comets":[{"galaxy":"G","coins":0}]}])")).has_value(), "a negative dome is refused");
        check(!parse_manifest(with_rows(R"("comet_schedule":[{"dome":2,"comets":[]}])")).has_value(), "a dome with no comets is refused");
    }
    {
        check(parse_manifest(with_rows(R"("galaxies":[{"name":"G","stars_to_open":255,"dome":255}])")).has_value(), "stars and dome up to a byte");
        check(!parse_manifest(with_rows(R"("galaxies":[{"name":"G","stars_to_open":256}])")).has_value(), "stars past a byte are refused");
        check(!parse_manifest(with_rows(R"("galaxies":[{"name":"G","dome":256}])")).has_value(), "a galaxy's dome past a byte is refused");
        check(!parse_manifest(with_rows(R"("galaxies":[{"name":"G","dome":-1}])")).has_value(), "a negative dome is refused");
        check(!parse_manifest(with_rows(R"("galaxies":[{"name":"G","stars_to_open":1e300}])")).has_value(), "a number past the int range is refused, not cast");
    }
    {
        // A file's tuning numbers: a number, a bool, or an option's choice; a key without a file, or another kind of value, refuses the file.
        const auto with_layer = [](const std::string& layer) {
            return std::string(R"({"format":1,"id":"a","name":"b","version":"1.0.0","game":"RMG","layers":[{)") + layer + "}]}";
        };
        const std::optional<Manifest> tuned = parse_manifest(with_layer(R"("tunables":{"Kuribo.cWalkSpeed":2.5,"Kuribo.cIsSmall":true,"Koopa.cHp":{"option":"hp"}})"));
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
        check(!parse_manifest(with_layer(R"("tunables":{"cWalkSpeed":2.0})")).has_value(), "a key without its file is refused");
        check(!parse_manifest(with_layer(R"("tunables":{"Kuribo.cWalkSpeed":"fast"})")).has_value(), "text is refused");
        check(!parse_manifest(with_layer(R"("tunables":[1,2])")).has_value(), "tunables as a list is refused");
        const std::optional<Manifest> listed = parse_manifest(with_layer(R"("tunables":{"Kuribo.cSteps":[4,null,9]})"));
        check(listed && listed->layers.size() == 1u && listed->layers[0].tunables.size() == 2u &&
              listed->layers[0].tunables[0].key == "Kuribo.cSteps[0]" && listed->layers[0].tunables[0].value == 4.0 &&
              listed->layers[0].tunables[1].key == "Kuribo.cSteps[2]" && listed->layers[0].tunables[1].value == 9.0,
              "a list sets its places, a null keeps one");
        check(!parse_manifest(with_layer(R"("tunables":{"Kuribo.cSteps":[4,"x"]})")).has_value(), "text in a list is refused");
        const std::optional<Manifest> needs = parse_manifest(R"({"format":1,"id":"a","name":"b","version":"1.0.0","game":"RMG","requires":["tunables"],"layers":[]})");
        check(needs && refusal(*needs, "RMGE01", "2.1.0", "windows").empty(), "a mod needing tunables is usable");
        // Player animations: name and file, a rate over 0 up to 100, whole blend frames up to 600.
        const std::optional<Manifest> moves = parse_manifest(with_layer(
            R"("player_animations":[{"name":"GoldSpin","file":"GoldSpin","rate":0.5,"blend_frames":4},{"name":"Jump","file":"MyJump"}])"));
        check(moves && moves->layers.size() == 1u && moves->layers[0].player_animations.size() == 2u &&
              moves->layers[0].player_animations[0].name == "GoldSpin" && moves->layers[0].player_animations[0].rate == 0.5 &&
              moves->layers[0].player_animations[0].blend_frames == 4 && moves->layers[0].player_animations[1].file == "MyJump" &&
              moves->layers[0].player_animations[1].rate == 1.0 && moves->layers[0].player_animations[1].blend_frames == 0,
              "player animations read");
        for (const char* bad : {R"("player_animations":[{"name":"A"}])", R"("player_animations":[{"name":"A","file":"B","rate":0}])",
                                R"("player_animations":[{"name":"A","file":"B","rate":"fast"}])", R"("player_animations":[{"name":"A","file":"B","blend_frames":2.5}])",
                                R"("player_animations":[{"name":"A","file":"B","blend_frames":601}])", R"("player_animations":{"name":"A"})"}) {
            check(!parse_manifest(with_layer(bad)).has_value(), bad);
        }
        const std::optional<Manifest> animated = parse_manifest(R"({"format":1,"id":"a","name":"b","version":"1.0.0","game":"RMG","requires":["player-animations"],"layers":[]})");
        check(animated && refusal(*animated, "RMGE01", "2.1.0", "windows").empty(), "a mod needing player animations is usable");
    }
    {
        // A range of choices: counted, so numbers too large for the step cannot loop for ever.
        const std::optional<Manifest> ranged = parse_manifest(std::string(R"({"format":1,"id":"a","name":"b","version":"1.0.0","game":"RMG","options":[)") +
            R"({"id":"r1","name":"R","range":{"from":1,"to":5,"step":1}},)" +
            R"({"id":"r2","name":"R","range":{"from":1e300,"to":1e300,"step":1}},)" +
            R"({"id":"r3","name":"R","range":{"from":5,"to":1,"step":1}},)" +
            R"({"id":"r4","name":"R","range":{"from":0,"to":1,"step":0.25}}],"layers":[]})");
        check(ranged && ranged->options.size() == 4u, "options with ranges read");
        if (ranged && ranged->options.size() == 4u) {
            check(ranged->options[0].choices.size() == 5u && ranged->options[0].choices[4].id == "5", "1 to 5 gives five choices");
            check(ranged->options[1].choices.size() == 1u, "a range of one huge number gives one choice and ends");
            check(ranged->options[2].choices.size() == 5u && ranged->options[2].choices[0].id == "5" &&
                      ranged->options[2].choices[4].id == "1",
                  "a range from a larger number to a smaller one counts down");
            check(ranged->options[3].choices.size() == 5u && ranged->options[3].choices[1].id == "0.25", "fractional steps");
        }
    }
    {
        // A layer's "switches" are refused, naming the tunable that took the first one's place.
        const std::optional<Manifest> switches = parse_manifest(std::string(R"({"format":1,"id":"a","name":"b","version":"1.0.0","game":"RMG","layers":[)") +
            R"({"switches":{"no_low_health_warning":1}}]})");
        check(switches && refusal(*switches, "RMGE01", "2.1.0", "windows") ==
                              "\"SWITCHES\" ARE NOW \"TUNABLES\": no_low_health_warning IS MarioEffect.cLowHealthWarning (THE OPPOSITE SENSE)",
              "switches refused with their tunable");
        const std::optional<Manifest> unknown = parse_manifest(std::string(R"({"format":1,"id":"a","name":"b","version":"1.0.0","game":"RMG","layers":[)") +
            R"({"switches":{"nope":1}}]})");
        check(unknown && refusal(*unknown, "RMGE01", "2.1.0", "windows") == "\"SWITCHES\" ARE NOW \"TUNABLES\"", "a switch this game never had");
    }
    {
        // The manifest file is read only when it is a sensible size.
        const std::filesystem::path folder = galaxy::test_temp_path("nebula_luma_test_manifest");
        std::error_code error;
        std::filesystem::create_directories(folder, error);
        {
            std::ofstream out(folder / "luma.json", std::ios::binary);
            out << R"({"format":1,"id":"a","name":"b","version":"1.0.0","game":"RMG","layers":[]})";
        }
        check(read_manifest(folder).has_value(), "a small luma.json is read");
        {
            std::ofstream out(folder / "luma.json", std::ios::binary | std::ios::trunc);
            out << R"({"format":1,"id":"a","name":"b","version":"1.0.0","game":"RMG","description":")";
            const std::string filler(17u << 20u, 'x');
            out << filler << R"(","layers":[]})";
        }
        check(!read_manifest(folder).has_value(), "a luma.json past 16 MB is not read");
        std::filesystem::remove_all(folder, error);
    }

    {
        // A message's words: "like" names the game message whose talk a new line takes; it is not a language.
        const std::optional<Manifest> talk = parse_manifest(std::string(R"({"format":1,"id":"a","name":"b","version":"1.0.0","game":"RMG","layers":[],)") +
            R"("text":{"MyNpc_001":{"default":"Hi [player]!","fr":"Salut [player] !","like":"AstroGalaxy_Tico034"},"GalaxyName_X":"X"}})");
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
