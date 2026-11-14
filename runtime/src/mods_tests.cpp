// SPDX-License-Identifier: GPL-3.0-only
// Mods (mods.h, virtual_disc.h, mod_xml.h): the XML reader, the disc's patching and reads, Riivolution mods, files
// mods, load order, saved choices, savegames, main.dol's memory patches, the mods' layouts and languages, and importing.

#if defined(_WIN32)
#include "galaxy/windows_api.h"
#endif

#include "galaxy/asset_store.h"
#include "galaxy/dolphin_pack.h"
#include "galaxy/texture_categories.h"
#include "galaxy/mod_import.h"
#include "galaxy/luma.h"
#include "galaxy/test_temp.h"
#include "galaxy/mod_xml.h"
#include "galaxy/mods.h"

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <iterator>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;

bool expect(bool condition, const char* message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

void write(const fs::path& path, std::string_view text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

bool xml() {
    bool ok = true;
    std::string error;
    const auto doc = galaxy::mods::parse_xml(
        "\xEF\xBB\xBF<?xml version=\"1.0\"?><!-- c --><!DOCTYPE x [<!ENTITY y \"z\">]>"
        "<a k=\"1 &amp; &lt;2&gt; &#65;&#x42;\" n='0x1F'><b/><![CDATA[<c/>]]>text<c t=\"Yes\"></c></a>",
        &error);
    ok &= expect(doc.has_value(), "a document");
    if (doc) {
        ok &= expect(doc->attribute("k") == "1 & <2> AB", "entities");
        ok &= expect(doc->attribute_uint("n", 0) == 0x1F, "hex numbers");
        ok &= expect(doc->children.size() == 2u && doc->child("c")->attribute_bool("t", false), "children; CDATA skipped");
    }
    ok &= expect(!galaxy::mods::parse_xml("<a><b></a>", &error) && error.find("line 1") != std::string::npos,
                 "a mismatched tag is refused, its line said");
    // A numeric reference that is no character (zero, past U+10FFFF, half a surrogate pair) gives U+FFFD, valid UTF-8;
    // one that is a character is itself.
    // UTF-16 (both orders, with the mark) and Latin-1 read as LumaLink reads them; a number at the type's end reads.
    const auto wide = galaxy::mods::parse_xml(std::string("\xFF\xFE<\0a\0 \0k\0=\0\"\0\xE9\0\"\0/\0>\0", 22), &error);
    const auto wide_big = galaxy::mods::parse_xml(std::string("\xFE\xFF\0<\0a\0 \0k\0=\0\"\0\xE9\0\"\0/\0>", 22), &error);
    const auto latin = galaxy::mods::parse_xml("<a k=\"\xE9\" n=\"-9223372036854775808\"/>", &error);
    ok &= expect(wide && wide->attribute("k") == "\xC3\xA9" && wide_big && wide_big->attribute("k") == "\xC3\xA9" && latin &&
                     latin->attribute("k") == "\xC3\xA9" && latin->attribute_int("n", 0) == std::numeric_limits<long long>::min(),
                 "UTF-16, Latin-1 and the lowest number");
    const auto refs = galaxy::mods::parse_xml("<a k=\"&#0;|&#x110000;|&#xFFFFFFFF;|&#xD800;|&#x10FFFF;|&#x20AC;\"/>", &error);
    ok &= expect(refs.has_value(), "a document with character references");
    if (refs) {
        ok &= expect(refs->attribute("k") ==
                         "\xEF\xBF\xBD|\xEF\xBF\xBD|\xEF\xBF\xBD|\xEF\xBF\xBD|\xF4\x8F\xBF\xBF|\xE2\x82\xAC",
                     "character references: invalid ones give U+FFFD");
    }
    return ok;
}

// Importing: zip and 7z made with libarchive's writer, a top folder lifted, a path that would leave refused, an
// update in place, a file that is no archive.
bool importing() {
    bool ok = true;
    const fs::path root = galaxy::test_temp_path("nebula_mods_import");
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "mods");
    const auto make = [&](const fs::path& file, int format,
                          const std::vector<std::pair<std::string, std::string>>& entries) {
        archive* w = archive_write_new();
        archive_write_set_format(w, format);
        archive_write_open_filename(w, file.string().c_str());
        for (const auto& [name, data] : entries) {
            archive_entry* e = archive_entry_new();
            archive_entry_set_pathname(e, name.c_str());
            archive_entry_set_size(e, static_cast<la_int64_t>(data.size()));
            archive_entry_set_filetype(e, AE_IFREG);
            archive_entry_set_perm(e, 0644);
            archive_write_header(w, e);
            archive_write_data(w, data.data(), data.size());
            archive_entry_free(e);
        }
        archive_write_close(w);
        archive_write_free(w);
    };
    make(root / "Cool Mod.zip", ARCHIVE_FORMAT_ZIP,
         {{"Cool Mod v1/riivolution/cool.xml", "<wiidisc version=\"1\"><id game=\"RMG\"/></wiidisc>"},
          {"Cool Mod v1/Cool/a.arc", "A"},
          {"readme.txt", "hi"},
          {"../evil.txt", "evil"},
          {"Cool Mod v1/C:../C:../evil.dll", "evil"},
          {"Cool Mod v1/D:/evil.dll", "evil"},
          {"D:/evil.dll", "evil"},
          {"/evil.dll", "evil"},
          {"Cool Mod v1/a.arc:evil", "evil"},
          {"Cool Mod v1/.. /evil.dll", "evil"},
          {"Cool Mod v1/NUL.dll", "evil"},
          {"Cool Mod v1/__MACOSX/x", "x"}});
    make(root / "Pack.7z", ARCHIVE_FORMAT_7ZIP, {{"RMG/tex1_8x8_0123456789abcdef_1.png", "png"}});
    write(root / "fake.zip", "not an archive");
    galaxy::mods::set_mods_folder(root / "mods", root / "mods.ini", "RMGE01");
    ok &= expect(galaxy::mods::is_mod_archive(root / "Cool Mod.zip") && galaxy::mods::is_mod_archive(root / "Pack.7z") &&
                     !galaxy::mods::is_mod_archive(root / "fake.zip"),
                 "archives by their contents");
    const galaxy::mods::ImportResult zip = galaxy::mods::import_mod(root / "Cool Mod.zip", root / "mods");
    ok &= expect(zip.ok && zip.id == "Cool Mod" && zip.kinds == "RIIVOLUTION", "a zip imported");
    ok &= expect(fs::exists(root / "mods" / "Cool Mod" / "riivolution" / "cool.xml") &&
                     fs::exists(root / "mods" / "Cool Mod" / "readme.txt") &&
                     !fs::exists(root / "mods" / "Cool Mod" / "Cool Mod v1"),
                 "its one top folder lifted, its notes kept");
    ok &= expect(!fs::exists(root / "evil.txt") && !fs::exists(root / "mods" / "evil.txt"),
                 "a path that leaves is refused");
    bool evil = false;
    for (const auto& entry : fs::recursive_directory_iterator(root / "mods", ec)) {
        const std::string name = entry.path().filename().string();
        evil = evil || name.find("evil") != std::string::npos || name.starts_with("NUL");
    }
    ok &= expect(!evil && !fs::exists(root / "evil.dll"), "drive, stream, dot and device paths are refused");
    for (const char* unsafe : {"", ".", "..", "...", ".. ", "C:..", "a:b", "a/b", "a\\b", "con", "CON.txt", "Aux.arc", "nul .x",
                               "com1", "LPT9.arc", "lpt\xC2\xB9", "conin$"}) {
        ok &= expect(!galaxy::mods::safe_path_part(unsafe), unsafe);
    }
    for (const char* safe : {"a.arc", ".hidden", "com", "com0", "com10", "console.txt", "auxiliary", "Mario.arc"}) {
        ok &= expect(galaxy::mods::safe_path_part(safe), safe);
    }
    ok &= expect(!fs::exists(root / "mods" / "Cool Mod" / "__MACOSX"), "system clutter skipped");
    const galaxy::mods::ImportResult again = galaxy::mods::import_mod(root / "Cool Mod.zip", root / "mods");
    ok &= expect(again.ok && again.already && !again.replaced && again.id == "Cool Mod",
                 "the same mod again: nothing added");
    fs::copy_file(root / "Cool Mod.zip", root / "Cool Mod (1).zip", ec);
    const galaxy::mods::ImportResult renamed = galaxy::mods::import_mod(root / "Cool Mod (1).zip", root / "mods");
    ok &= expect(renamed.ok && renamed.already && renamed.id == "Cool Mod" && !fs::exists(root / "mods" / "Cool Mod (1)"),
                 "the same mod under another file name: nothing added");
    fs::create_directories(root / "v2");
    make(root / "v2" / "Cool Mod.zip", ARCHIVE_FORMAT_ZIP,
         {{"riivolution/cool.xml", "<wiidisc version=\"1\"><id game=\"RMG\"/></wiidisc>"}, {"Cool/a.arc", "B"}, {"readme.txt", "hi"}});
    const galaxy::mods::ImportResult update = galaxy::mods::import_mod(root / "v2" / "Cool Mod.zip", root / "mods");
    ok &= expect(update.ok && update.replaced && !update.already, "a changed file of the same size: updated in place");
    const galaxy::mods::ImportResult pack = galaxy::mods::import_mod(root / "Pack.7z", root / "mods");
    ok &= expect(pack.ok && pack.kinds == "TEXTURES", "a 7z texture pack imported");
    ok &= expect(pack.dolphin_files == 1u && pack.dolphin_textures == 1u && pack.dolphin_unmatched == 0u &&
                     fs::exists(root / "mods" / "Pack" / "Pack" / "Grass.png") &&
                     fs::exists(root / "mods" / "Pack" / ".dolphin" / "tex1_8x8_0123456789abcdef_1.png"),
                 "its Dolphin names converted on import (its one top folder lifted first)");
    fs::copy(root / "mods" / "Pack", root / "Pack copy", fs::copy_options::recursive, ec);
    write(root / "Pack copy" / "Thumbs.db", "clutter");
    const galaxy::mods::ImportResult copy = galaxy::mods::import_mod(root / "Pack copy", root / "mods");
    ok &= expect(copy.ok && copy.already && copy.id == "Pack" && !fs::exists(root / "mods" / "Pack copy"),
                 "the same pack as a folder (clutter aside): nothing added");
    write(root / "Pack copy" / "RMG" / "extra.png", "png");
    const galaxy::mods::ImportResult more = galaxy::mods::import_mod(root / "Pack copy", root / "mods");
    ok &= expect(more.ok && !more.already && more.id == "Pack copy", "a pack with one more file: its own mod");
    ok &= expect(galaxy::mods::remove_mod(root / "mods", "Pack copy", nullptr), "the second pack removed");
    const galaxy::mods::ImportResult fake = galaxy::mods::import_mod(root / "fake.zip", root / "mods");
    ok &= expect(!fake.ok && !fake.message.empty(), "a file that is no archive is refused with a reason");
    std::vector<std::string> left;
    for (const auto& entry : fs::directory_iterator(root / "mods", ec)) left.push_back(entry.path().filename().string());
    std::sort(left.begin(), left.end());
    ok &= expect(left == std::vector<std::string>{"Cool Mod", "Pack"}, "no staging folders left behind");
    std::string message;
    for (const char* outside : {"../mods", "Pack/../../x", "..", ".cooked", "", "C:x"}) {
        const std::string what = std::string("no remove outside a mod's folder: ") + outside;
        ok &= expect(!galaxy::mods::remove_mod(root / "mods", outside, &message) && fs::exists(root / "mods" / "Pack"),
                     what.c_str());
    }
    ok &= expect(galaxy::mods::remove_mod(root / "mods", "Pack", &message) && !fs::exists(root / "mods" / "Pack"),
                 "a mod removed");
    fs::remove_all(root, ec);
    return ok;
}

// Memory patches over the game's code: never applied; the mod's problem counts them and each has its own note, its
// address and size, naming LumaLink's tunables and plugins.
bool code_patch_notes() {
    bool ok = true;
    const fs::path root = galaxy::test_temp_path("nebula_mods_code_patches");
    std::error_code ec;
    fs::remove_all(root, ec);
    // A console mod's patches of the game's memory, as its own file (runtime/tests): its addresses are the console's.
    std::ifstream fixture(fs::path(MODS_FIXTURES_DIR) / "riivolution_memory_patches.xml", std::ios::binary);
    const std::string patches{std::istreambuf_iterator<char>(fixture), std::istreambuf_iterator<char>()};
    ok &= expect(!patches.empty(), "the memory patch fixture reads");
    write(root / "mods" / "Rules" / "riivolution" / "rules.xml", patches);
    galaxy::mods::set_mods_folder(root / "mods", root / "mods.ini", "RMGE01");
    const std::vector<galaxy::mods::ModInfo> mods = galaxy::mods::scan_mods();
    const auto it = std::find_if(mods.begin(), mods.end(), [](const galaxy::mods::ModInfo& m) { return m.id == "Rules"; });
    ok &= expect(it != mods.end() && it->code_patches == 2u && it->code_patch_notes.size() == 2u, "two code patches, two notes");
    if (it != mods.end() && it->code_patch_notes.size() == 2u) {
        const std::string& a = it->code_patch_notes[0];
        const std::string& b = it->code_patch_notes[1];
        ok &= expect(a.find("80451234") != std::string::npos && a.find("4 bytes") != std::string::npos &&
                         a.find("tunables") != std::string::npos && a.find("plugin") != std::string::npos,
                     "a note names its address, size and what replaces it");
        ok &= expect(b.find("80001000") != std::string::npos && b.find("8 bytes, searched") != std::string::npos,
                     "a searched patch says so");
        ok &= expect(it->problem.find("2 CODE CHANGES") != std::string::npos, "the problem counts them");
    }
    fs::remove_all(root, ec);
    // The cooked mods are named with the store's version: another version cooks them again.
    const std::string signature = galaxy::mods::plan_signature(galaxy::mods::ModPlan{}, "RMGE01");
    ok &= expect(signature.find(" store " + std::to_string(galaxy::assets::kStoreVersion) + "\n") != std::string::npos,
                 "the plan's signature holds the store version");
    return ok;
}

// Two .luma mods setting the same things: a tunable, a table row one removes and the other changes a cell of, a line of
// one language against a line of every language. Each counts them against the other, and their own new rows count none.
bool setting_overlaps() {
    bool ok = true;
    const fs::path root = galaxy::test_temp_path("nebula_mods_setting_overlaps");
    std::error_code ec;
    fs::remove_all(root, ec);
    const auto manifest = [](const char* id, const char* tunables) {
        return std::string(R"({"format":"1.0","id":")") + id + R"(","name":"n","version":"1.0.0","game":{"id":"RMG"},"layers":[{"name":"layer"}],)" +
               R"("pre_standard":{"layers":{"layer":{"tunables":)" + tunables + "}}}}";
    };
    write(root / "mods" / "A" / "luma.json", manifest("mod-a", R"({"Kuribo.cWalkSpeed": 2, "Kuribo.cRunSpeed": 3})"));
    write(root / "mods" / "A" / "layers" / "layer" / "merge" / "ObjectData" / "Things.bcsv.json",
          R"({"change":[{"where":{"name":"Kuribo","id":1},"set":{"scale":2.5}}],"rows":[{"name":"New"}]})");
    write(root / "mods" / "A" / "layers" / "layer" / "merge" / "UsEnglish" / "MessageData" / "Message.arc" / "message.bmg.json",
          R"({"lines":{"Talk_A":"Hi"}})");
    write(root / "mods" / "B" / "luma.json", manifest("mod-b", R"({"Kuribo.cWalkSpeed": 4})"));
    write(root / "mods" / "B" / "layers" / "layer" / "merge" / "objectdata" / "things.bcsv.json",
          R"({"remove":[{"id":1,"name":"Kuribo"}],"rows":[{"name":"New"}]})");
    write(root / "mods" / "B" / "layers" / "layer" / "merge" / "MessageData" / "messages.json", R"({"lines":{"Talk_A":"Yo","Talk_B":"Hey"}})");
    galaxy::mods::set_mods_folder(root / "mods", root / "mods.ini", "RMGE01");
    const std::vector<galaxy::mods::ModInfo> mods = galaxy::mods::scan_mods();
    const auto find = [&](const char* id) {
        return std::find_if(mods.begin(), mods.end(), [&](const galaxy::mods::ModInfo& m) { return m.id == id; });
    };
    const auto a = find("A");
    const auto b = find("B");
    ok &= expect(a != mods.end() && b != mods.end() && a->problem.empty() && b->problem.empty(), "two .luma mods read");
    if (a != mods.end() && b != mods.end()) {
        ok &= expect(a->overlaps.size() == 1u && a->overlaps[0].other == "B" && a->overlaps[0].settings == 3u &&
                         a->overlaps[0].files == 0u,
                     "A shares a tunable, a cell of a removed row and an English line with B");
        ok &= expect(b->overlaps.size() == 1u && b->overlaps[0].other == "A" && b->overlaps[0].settings == 3u,
                     "B shares the same three with A");
    }
    const std::vector<std::string> cells = galaxy::luma::merge_keys("ObjectData/Things.bcsv.json",
                                                                    R"({"change":[{"where":{"id":1.5},"set":{"a":1,"b":"x"}}]})");
    ok &= expect(cells == std::vector<std::string>{"cell\tobjectdata/things.bcsv\tid=1.5\ta", "cell\tobjectdata/things.bcsv\tid=1.5\tb"},
                 "a change's cells by table, row and column");
    ok &= expect(galaxy::luma::merge_keys("ObjectData/Things.bcsv.json", "not json").empty() &&
                     galaxy::luma::merge_keys("Effect/Particles.jpc.json", R"({"change":[]})").empty(),
                 "no keys from what isn't JSON or adds particles");
    fs::remove_all(root, ec);
    return ok;
}

// Patches by key (luma.md 5.1) go to the cooker with their mod, file and game path, bottom of the list first, then
// layer and file order, with the mod order; a file that is not <game path>.json is named, not used. A mod of patches
// only changes the game.
bool patches_by_key_in_the_plan() {
    bool ok = true;
    const fs::path root = galaxy::test_temp_path("nebula_mods_patches_by_key");
    std::error_code ec;
    fs::remove_all(root, ec);
    const auto manifest = [](const char* id, const char* layers) {
        return std::string(R"({"format":1,"id":")") + id + R"(","name":"n","version":"1.0.0","game":"RMG","layers":[)" + layers + "]}";
    };
    write(root / "mods" / "top" / "luma.json", manifest("top", R"({"path":"layers/base"})"));
    write(root / "mods" / "top" / "layers" / "base" / "patches" / "StageData" / "AstroGalaxy" / "jmp" / "Placement" / "Common" /
              "ObjInfo.json",
          R"({"patch":1,"key":["l_id"],"remove":[{"l_id":3}]})");
    write(root / "mods" / "low" / "luma.json", manifest("low", R"({"path":"layers/one"},{"path":"layers/two"})"));
    write(root / "mods" / "low" / "layers" / "one" / "patches" / "ObjectData" / "Things.bcsv.json", R"({"patch":1,"key":["name"]})");
    write(root / "mods" / "low" / "layers" / "one" / "patches" / "ObjectData" / "Notes.txt", "not a patch");
    write(root / "mods" / "low" / "layers" / "two" / "patches" / "ObjectData" / "Things.bcsv.json", R"({"patch":1,"key":["name"]})");
    write(root / "mods" / "low" / "layers" / "two" / "patches" / "Effect" / "Particles.json", R"({"patch":1,"key":["name"]})");
#if !defined(_WIN32)
    // A name with a tab would be other fields of the cooker's line (Windows has no such names).
    write(root / "mods" / "low" / "layers" / "two" / "patches" / "ObjectData" / "Tab\tName.bcsv.json", R"({"patch":1,"key":["name"]})");
#endif
    write(root / "mods.ini", "[top]\nenabled=1\norder=1\n[low]\nenabled=1\norder=2\n");
    galaxy::mods::set_mods_folder(root / "mods", root / "mods.ini", "RMGE01");
    const std::vector<galaxy::mods::ModInfo> mods = galaxy::mods::scan_mods();
    const auto changes = [&](const char* id) {
        const auto it = std::find_if(mods.begin(), mods.end(), [&](const galaxy::mods::ModInfo& m) { return m.id == id; });
        return it != mods.end() && it->enabled && it->problem.empty() && galaxy::mods::changes_game_files(*it);
    };
    ok &= expect(changes("top") && changes("low"), "a .luma of patches only changes the game");
    const galaxy::assets::AssetStore store;
    const galaxy::mods::ModPlan plan = galaxy::mods::plan_mod_changes(store, "RMGE01", "UsEnglish", mods);
    std::vector<std::string> got;
    for (const galaxy::mods::ModPlan::Patch& p : plan.patches) got.push_back(p.mod_id + " " + p.entry + " " + p.disc);
    ok &= expect(got == std::vector<std::string>{
                            "low layers/one/patches/ObjectData/Things.bcsv.json ObjectData/Things.bcsv",
                            "low layers/two/patches/Effect/Particles.json Effect/Particles",
                            "low layers/two/patches/ObjectData/Things.bcsv.json ObjectData/Things.bcsv",
                            "top layers/base/patches/StageData/AstroGalaxy/jmp/Placement/Common/ObjInfo.json "
                            "StageData/AstroGalaxy/jmp/Placement/Common/ObjInfo"},
                 "the patches, bottom of the list first, then layer and file order");
    ok &= expect(plan.used == std::vector<std::string>{"low", "top"}, "the mod order, bottom of the list first");
    ok &= expect(std::any_of(plan.notes.begin(), plan.notes.end(),
                             [](const std::string& n) { return n.find("layers/one/patches/ObjectData/Notes.txt") != std::string::npos; }),
                 "a file that is not a patch is named");
#if !defined(_WIN32)
    ok &= expect(std::any_of(plan.notes.begin(), plan.notes.end(),
                             [](const std::string& n) { return n.find("Tab\tName.bcsv.json is not used: its name holds a tab") != std::string::npos; }),
                 "a patch whose name holds a tab is left out and named");
#endif
    const std::string text = galaxy::mods::plan_text(plan, "RMGE01");
    ok &= expect(text.find("\n%mods\tlow\ttop\n") != std::string::npos &&
                     text.find("\n%patch\ttop\tlayers/base/patches/StageData/AstroGalaxy/jmp/Placement/Common/ObjInfo.json\t"
                               "StageData/AstroGalaxy/jmp/Placement/Common/ObjInfo\t") != std::string::npos,
                 "the cooker's plan has the mod order and each patch");
    const std::string signature = galaxy::mods::plan_signature(plan, "RMGE01");
    ok &= expect(signature.find("\n%mods\tlow\ttop\n") != std::string::npos && signature.find("%patch\tlow\t") != std::string::npos,
                 "the signature has the order and the patches");
    fs::remove_all(root, ec);
    return ok;
}

// A .luma that only sets the player's numbers, or only adds an object, is in the plan and asks for a restart; one with an
// empty layer folder sets nothing.
bool layer_only_mods() {
    bool ok = true;
    const fs::path root = galaxy::test_temp_path("nebula_mods_layer_only");
    std::error_code ec;
    fs::remove_all(root, ec);
    // A layer's old keys are read from pre_standard.layers.<name> (OLD-ROWS.md).
    const auto manifest = [](const char* id, const char* old) {
        return std::string(R"({"format":"1.0","id":")") + id + R"(","name":"n","version":"1.0.0","game":{"id":"RMG"},"layers":[{"name":"layer"}])" +
               (old[0] != 0 ? std::string(R"(,"pre_standard":{"layers":{"layer":)") + old + "}}" : std::string()) + "}";
    };
    write(root / "mods" / "consts" / "luma.json", manifest("consts", R"({"player_constants":{"both":{"mSpinIntervalTime":0}}})"));
    write(root / "mods" / "classes" / "luma.json", manifest("classes", R"({"object_classes":[{"name":"BigKuribo","like":"Kuribo"}]})"));
    write(root / "mods" / "hollow" / "luma.json", manifest("hollow", ""));
    for (const char* folder : {"consts", "classes", "hollow"}) fs::create_directories(root / "mods" / folder / "layers" / "layer", ec);
    write(root / "mods.ini", "[consts]\nenabled=1\norder=1\n[classes]\nenabled=1\norder=2\n[hollow]\nenabled=1\norder=3\n");
    galaxy::mods::set_mods_folder(root / "mods", root / "mods.ini", "RMGE01");
    const std::vector<galaxy::mods::ModInfo> mods = galaxy::mods::scan_mods();
    const auto sets = [&](const char* id) {
        const auto it = std::find_if(mods.begin(), mods.end(), [&](const galaxy::mods::ModInfo& m) { return m.id == id; });
        return it != mods.end() && it->enabled && it->problem.empty() && galaxy::mods::changes_game_files(*it);
    };
    ok &= expect(sets("consts") && sets("classes") && !sets("hollow"), "the player's numbers and a new object set the game; an empty layer doesn't");
    const galaxy::assets::AssetStore store;
    const galaxy::mods::ModPlan plan = galaxy::mods::plan_mod_changes(store, "RMGE01", "UsEnglish", mods);
    ok &= expect(plan.rows.player_constants.size() == 2u && plan.rows.object_classes.size() == 1u,
                 "the plan has the number for Mario and Luigi, and the object");
    fs::remove_all(root, ec);
    return ok;
}

// A layer's assets/ is the mod's own store: each file a plan line at /Luma/<id>/<path>, the later layer's same path
// winning; each layer's assets/effects.json a change for the cook, in layer order.
bool layer_assets() {
    bool ok = true;
    const fs::path root = galaxy::test_temp_path("nebula_mods_layer_assets");
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path mod = root / "mods" / "fx-mod";  // an installed mod's folder is named by its id
    write(mod / "luma.json",
          R"({"format":"1.0","id":"fx-mod","name":"n","version":"1.0.0","game":{"id":"RMG"},"layers":[{"name":"base"},{"name":"night"}]})");
    write(mod / "layers" / "base" / "assets" / "spark.png", "base");
    write(mod / "layers" / "base" / "assets" / "effects.json", R"({"effects":[]})");
    write(mod / "layers" / "night" / "assets" / "spark.png", "night");
    write(mod / "layers" / "night" / "assets" / "sub" / "glow.bti", "glow");
    write(mod / "layers" / "night" / "assets" / "effects.json", R"({"effects":[]})");
    write(root / "mods.ini", "[fx-mod]\nenabled=1\norder=1\n");
    galaxy::mods::set_mods_folder(root / "mods", root / "mods.ini", "RMGE01");
    const std::vector<galaxy::mods::ModInfo> mods = galaxy::mods::scan_mods();
    const auto it = std::find_if(mods.begin(), mods.end(), [](const galaxy::mods::ModInfo& m) { return m.id == "fx-mod"; });
    ok &= expect(it != mods.end() && it->problem.empty() && galaxy::mods::changes_game_files(*it), "assets set the game");
    const galaxy::assets::AssetStore store;
    const galaxy::mods::ModPlan plan = galaxy::mods::plan_mod_changes(store, "RMGE01", "UsEnglish", mods);
    const auto host_of = [&](const char* disc) {
        for (const galaxy::mods::ModPlan::File& f : plan.files) {
            if (f.disc == disc) return f.host;
        }
        return fs::path();
    };
    ok &= expect(host_of("/Luma/fx-mod/spark.png") == mod / "layers" / "night" / "assets" / "spark.png",
                 "the later layer's file wins its path");
    ok &= expect(host_of("/Luma/fx-mod/sub/glow.bti") == mod / "layers" / "night" / "assets" / "sub" / "glow.bti",
                 "a file in a sub-folder keeps its path");
    ok &= expect(host_of("/Luma/fx-mod/effects.json").empty() && plan.files.size() == 2u, "effects.json is not a file line");
    ok &= expect(plan.merges.size() == 2u && plan.merges[0].disc == "Luma/fx-mod/effects.json" &&
                     plan.merges[0].host == mod / "layers" / "base" / "assets" / "effects.json" &&
                     plan.merges[1].host == mod / "layers" / "night" / "assets" / "effects.json",
                 "each layer's effects.json is a change, in layer order");
    fs::remove_all(root, ec);
    return ok;
}

// Two folder mods that each add a galaxy: the saves are the higher one's, as for every other choice of saves.
bool galaxy_saves() {
    bool ok = true;
    const fs::path root = galaxy::test_temp_path("nebula_mods_galaxy_saves");
    std::error_code ec;
    fs::remove_all(root, ec);
    write(root / "mods" / "Top" / "StageData" / "TopGalaxy" / "TopGalaxyScenario.arc", "a");
    write(root / "mods" / "Low" / "StageData" / "LowGalaxy" / "LowGalaxyScenario.arc", "b");
    write(root / "mods.ini", "[Top]\nenabled=1\norder=1\n[Low]\nenabled=1\norder=2\n");
    galaxy::mods::set_mods_folder(root / "mods", root / "mods.ini", "RMGE01");
    const galaxy::assets::AssetStore store;
    galaxy::mods::ModPlan plan = galaxy::mods::plan_mod_changes(store, "RMGE01", "UsEnglish", galaxy::mods::scan_mods());
    ok &= expect(plan.save_folder.filename() == "Top" && plan.files.size() == 2u, "two new galaxies play on the higher mod's saves");
    write(root / "mods.ini", "[Top]\nenabled=1\norder=2\n[Low]\nenabled=1\norder=1\n");
    plan = galaxy::mods::plan_mod_changes(store, "RMGE01", "UsEnglish", galaxy::mods::scan_mods());
    ok &= expect(plan.save_folder.filename() == "Low", "and the other's when the order turns");
    fs::remove_all(root, ec);
    return ok;
}

// Importing a .luma: a new one by its id; the same id in a folder of another name is the one updated, with both versions
// named; an older version says it goes back; a folder of its name holding another mod is left alone.
bool luma_import() {
    bool ok = true;
    const fs::path root = galaxy::test_temp_path("nebula_mods_luma_import");
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "mods");
    const auto make = [&](const fs::path& file, const char* version) {
        archive* w = archive_write_new();
        archive_write_set_format(w, ARCHIVE_FORMAT_ZIP);
        archive_write_open_filename(w, file.string().c_str());
        const std::string manifest = std::string(R"({"format":"1.0","id":"kaizo","name":"Kaizo","version":")") + version +
                                     R"(","game":{"id":"RMG"},"layers":[{"name":"layer"}]})";
        for (const auto& [name, data] : std::vector<std::pair<std::string, std::string>>{{"luma.json", manifest}, {"layers/layer/files/a.txt", version}}) {
            archive_entry* e = archive_entry_new();
            archive_entry_set_pathname(e, name.c_str());
            archive_entry_set_size(e, static_cast<la_int64_t>(data.size()));
            archive_entry_set_filetype(e, AE_IFREG);
            archive_entry_set_perm(e, 0644);
            archive_write_header(w, e);
            archive_write_data(w, data.data(), data.size());
            archive_entry_free(e);
        }
        archive_write_close(w);
        archive_write_free(w);
    };
    make(root / "Kaizo v1.luma", "1.0.0");
    make(root / "Kaizo v2.luma", "1.1.0");
    galaxy::mods::set_mods_folder(root / "mods", root / "mods.ini", "RMGE01");
    const galaxy::mods::ImportResult first = galaxy::mods::import_mod(root / "Kaizo v1.luma", root / "mods");
    ok &= expect(first.ok && first.id == "kaizo" && !first.replaced && fs::exists(root / "mods" / "kaizo" / "luma.json"),
                 "a .luma goes to the folder of its id");
    // A file made read-only (as an unpacker or a copy from a disc leaves them) does not stop the update.
    fs::permissions(root / "mods" / "kaizo" / "layers" / "layer" / "files" / "a.txt",
                    fs::perms::owner_write | fs::perms::group_write | fs::perms::others_write, fs::perm_options::remove, ec);
    fs::rename(root / "mods" / "kaizo", root / "mods" / "My Kaizo", ec);
    write(root / "mods" / "kaizo" / "riivolution" / "other.xml", "<wiidisc version=\"1\"><id game=\"RMG\"/></wiidisc>");
    {
        // The install screen's read (services/mods.md 9): the archive's id and version, and the installed one of its id
        // found in a folder of another name; nothing installed for an id no folder holds.
        const galaxy::mods::ImportPeek peek = galaxy::mods::peek_import(root / "Kaizo v2.luma", root / "mods");
        ok &= expect(peek.id == "kaizo" && peek.version == "1.1.0" && peek.installed == "1.0.0",
                     "a file's mod and the installed version of its id are read before importing");
        const galaxy::mods::ImportPeek none = galaxy::mods::peek_import(root / "Kaizo v2.luma", root / "empty");
        ok &= expect(none.id == "kaizo" && none.installed.empty(), "no installed version where no mod of its id is");
        const galaxy::mods::ImportPeek bad = galaxy::mods::peek_import(root / "missing.luma", root / "mods");
        ok &= expect(bad.id.empty() && bad.version.empty(), "a file that cannot be read gives nothing");
    }
    const galaxy::mods::ImportResult update = galaxy::mods::import_mod(root / "Kaizo v2.luma", root / "mods");
    ok &= expect(update.ok && update.replaced && update.id == "My Kaizo" && update.old_version == "1.0.0" &&
                     update.new_version == "1.1.0" && update.message == "UPDATED: Kaizo (1.0.0 TO 1.1.0)",
                 "the folder holding its id is updated, and the versions named");
    ok &= expect(fs::exists(root / "mods" / "kaizo" / "riivolution" / "other.xml"), "a folder of its name holding another mod stays");
    {
        // Another run cut off during an update (its mod moved aside, the new one still staged) and during an import: the
        // mod goes back, the staging goes; a moved-aside copy whose mod is in place goes too.
        const fs::path tidy = root / "tidy";
        fs::create_directories(tidy / ".importing-cut-previous-4000000000-0", ec);
        write(tidy / ".importing-cut-previous-4000000000-0" / "luma.json", "{}");
        write(tidy / ".importing-cut-4000000000-1" / "half.txt", "x");
        write(tidy / ".importing-kept-previous-4000000000-2" / "old.txt", "x");
        write(tidy / "kept" / "luma.json", "{}");
        write(tidy / ".importing-odd" / "a.txt", "x");
        galaxy::mods::apply_pending_updates(tidy);
        ok &= expect(fs::exists(tidy / "cut" / "luma.json") && !fs::exists(tidy / ".importing-cut-previous-4000000000-0") &&
                         !fs::exists(tidy / ".importing-cut-4000000000-1") && !fs::exists(tidy / ".importing-kept-previous-4000000000-2") &&
                         fs::exists(tidy / "kept" / "luma.json") && fs::exists(tidy / ".importing-odd" / "a.txt"),
                     "a cut-off update's mod goes back and its staging goes");
    }
    const galaxy::mods::ImportResult back = galaxy::mods::import_mod(root / "Kaizo v1.luma", root / "mods");
    ok &= expect(back.ok && back.id == "My Kaizo" && back.message == "OLDER VERSION: Kaizo (1.1.0 TO 1.0.0)", "an older version says so");
    fs::remove_all(root / "mods" / "My Kaizo", ec);
    const galaxy::mods::ImportResult beside = galaxy::mods::import_mod(root / "Kaizo v2.luma", root / "mods");
    ok &= expect(beside.ok && beside.id == "kaizo-2" && !beside.replaced, "with its name taken by another mod, a name of its own");
    ok &= expect(galaxy::mods::import_mod(root / "Kaizo v2.luma", root / "mods").already, "the same .luma again: nothing added");
#if defined(_WIN32)
    // A mod whose folder Windows won't move (a file in it open in another program): the update waits as
    // .update-<name> and takes its place at the next start; removing it leaves it whole.
    const fs::path held = root / "mods" / "kaizo-2" / "layers" / "layer" / "files" / "a.txt";
    const HANDLE open = CreateFileW(held.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    ok &= expect(open != INVALID_HANDLE_VALUE, "a file of the mod held open");
    const galaxy::mods::ImportResult waits = galaxy::mods::import_mod(root / "Kaizo v1.luma", root / "mods");
    ok &= expect(waits.ok && waits.pending && waits.replaced && waits.id == "kaizo-2" &&
                     fs::exists(root / "mods" / ".update-kaizo-2" / "luma.json"),
                 "an update of a mod in use waits beside it");
    std::string message;
    ok &= expect(!galaxy::mods::remove_mod(root / "mods", "kaizo-2", &message) && fs::exists(root / "mods" / "kaizo-2" / "luma.json") &&
                     fs::exists(held) && message.starts_with("IN USE: kaizo-2"),
                 "a mod in use is not removed, nor half removed");
    galaxy::mods::apply_pending_updates(root / "mods");
    ok &= expect(fs::exists(root / "mods" / ".update-kaizo-2"), "still in use at the start: the update waits on");
    CloseHandle(open);
    galaxy::mods::apply_pending_updates(root / "mods");
    std::ifstream now(held);
    const std::string text((std::istreambuf_iterator<char>(now)), std::istreambuf_iterator<char>());
    now.close();
    ok &= expect(text == "1.0.0" && !fs::exists(root / "mods" / ".update-kaizo-2"), "at the next start the update takes its place");
#endif
    fs::remove_all(root, ec);
    return ok;
}

// What the list decides of .luma mods: another copy, a chain of needs that loses its end and two mods that can't run
// together are left to the validator (L800 to L802, each start); damaged manifests, where new mods go, a folder named
// in any script, and a .luma's movies used while it runs.
bool luma_rules() {
    bool ok = true;
    const fs::path root = galaxy::test_temp_path("nebula_mods_luma_rules");
    std::error_code ec;
    fs::remove_all(root, ec);
    const auto manifest = [](const char* id, const char* more) {
        return std::string(R"({"format":"1.0","id":")") + id + R"(","name":"n","version":"1.0.0","game":{"id":"RMG"},)" + more +
               R"("layers":[{"name":"layer"}]})";
    };
    const auto mod = [&](const char* folder, const char* id, const char* more) {
        write(root / "mods" / folder / "luma.json", manifest(id, more));
        fs::create_directories(root / "mods" / folder / "layers" / "layer", ec);
    };
    mod("A", "mod-a", "");
    mod("A copy", "mod-a", "");
    mod("C1", "chain-1", R"("needs":[{"id":"chain-2"}],)");
    mod("C2", "chain-2", R"("needs":[{"id":"chain-3"}],)");
    mod("X", "mod-x", "");
    mod("Y", "mod-y", R"("conflicts":[{"id":"mod-x","why":"Both change the lives."}],)");
    mod("M", "mod-m", "");
    write(root / "mods" / "M" / "layers" / "layer" / "files" / "MovieData" / "opening.mp4", "movie");
    // What the reader refuses in its own words: a cut-off luma.json, a layer folder missing. A later format reads its
    // format and id only, and the second game reads: both are the validator's to refuse (L205, L207).
    write(root / "mods" / "Cut" / "luma.json", R"({"format":"1.0","id":"cut","na)");
    write(root / "mods" / "Later" / "luma.json", R"({"format":"2.0","id":"later","anything":[1,2]})");
    write(root / "mods" / "Hollow" / "luma.json", manifest("hollow", ""));
    // Where new mods ask to go: one loading after P goes above it (the top wins), one loading before P below it.
    mod("P", "mod-p", "");
    mod("Q after P", "mod-q", R"("load":{"after":["mod-p"]},)");
    mod("0 before P", "mod-r", R"("load":{"before":["mod-p"]},)");
    // A folder named in any script, with spaces.
    const std::string unicode = reinterpret_cast<const char*>(u8"Ünïcode ☆ mod");
    write(root / "mods" / fs::path(u8"Ünïcode ☆ mod") / "luma.json", manifest("unicode", ""));
    fs::create_directories(root / "mods" / fs::path(u8"Ünïcode ☆ mod") / "layers" / "layer", ec);
    galaxy::mods::set_mods_folder(root / "mods", root / "mods.ini", "RMGE01");
    const std::vector<galaxy::mods::ModInfo> mods = galaxy::mods::scan_mods();
    const auto problem = [&](const char* id) {
        const auto it = std::find_if(mods.begin(), mods.end(), [&](const galaxy::mods::ModInfo& m) { return m.id == id; });
        return it == mods.end() ? std::string("(missing)") : it->problem;
    };
    // Another copy, needs and conflicts are the validator's at each start (L800 to L802, modlog::check_packages): the
    // list itself leaves them alone.
    ok &= expect(problem("A").empty() && problem("A copy").empty(), "another copy is the validator's (L802)");
    ok &= expect(problem("C2").empty() && problem("C1").empty(), "what a mod needs is the validator's (L800)");
    ok &= expect(problem("X").empty() && problem("Y").empty(), "what a mod can't run with is the validator's (L801)");
    // The Mods page's draft says them as the player changes the list (mark_needs): the chain loses its end
    // and its start with it; the mod naming the conflict is the one off; another copy stays the validator's.
    {
        std::vector<galaxy::mods::ModInfo> draft = mods;
        galaxy::mods::mark_needs(draft);
        const auto drafted = [&](const char* id) {
            const auto it = std::find_if(draft.begin(), draft.end(), [&](const galaxy::mods::ModInfo& m) { return m.id == id; });
            return it == draft.end() ? std::string("(missing)") : it->problem;
        };
        ok &= expect(drafted("C2") == "THIS MOD NEEDS: chain-3", "the draft: a need not in the list");
        ok &= expect(drafted("C1").starts_with("THIS MOD NEEDS: "), "the draft: a need that is not working");
        ok &= expect(drafted("X").empty() && drafted("Y").starts_with("THIS MOD CAN'T RUN WITH: "),
                     "the draft: the mod naming a conflict is off");
        ok &= expect(drafted("A").empty() && drafted("A copy").empty() && drafted("P").empty(), "the draft: the rest run");
        for (galaxy::mods::ModInfo& m : draft) {
            if (m.id == "X") m.enabled = false;
            m.problem = m.own_problem;
        }
        galaxy::mods::mark_needs(draft);
        ok &= expect(drafted("Y").empty(), "the draft: a conflict turned off lets the mod run");
    }
    ok &= expect(problem("Cut") == "THIS FILE IS DAMAGED", "a cut-off luma.json is damaged");
    ok &= expect(problem("Later") != "THIS FILE IS DAMAGED", "a later format is not called damaged (L205 refuses it)");
    ok &= expect(problem("Hollow") == "THIS FILE IS DAMAGED", "a layer folder missing is damaged");
    ok &= expect(problem(unicode.c_str()).empty(), "a folder named in any script runs");
    const auto place = [&](const char* id) {
        return std::find_if(mods.begin(), mods.end(), [&](const galaxy::mods::ModInfo& m) { return m.id == id; }) - mods.begin();
    };
    ok &= expect(place("Q after P") < place("P") && place("0 before P") > place("P"), "load_after and load_before place a new mod");
    const std::vector<fs::path> movies = galaxy::mods::movie_folders(mods);
    ok &= expect(movies.size() == 1u && movies[0].filename() == "MovieData" && movies[0].parent_path().filename() == "files",
                 "a .luma's movies at their game path are used");
    fs::remove_all(root, ec);
    return ok;
}

}  // namespace

// Each kind of game file goes to the category a player would look for it in.
void test_texture_categories() {
    using galaxy::textures::Category;
    using galaxy::textures::category_of_path;
    const auto is = [](std::string_view path, Category category) {
        expect(category_of_path(path) == category, std::string(path).c_str());
    };
    is("ObjectData/Mario/Mario.bdl", Category::MarioLuigi);
    is("ObjectData/BeeMario/BeeMario.bdl", Category::MarioLuigi);
    is("ObjectData/Kuribo/Kuribo.bdl", Category::Enemies);
    is("ObjectData/BasaBasaHang/x.bdl", Category::Enemies);
    is("ObjectData/Rosetta/Rosetta.bdl", Category::Characters);
    is("ObjectData/TicoFat/TicoFat.bdl", Category::Characters);
    is("ObjectData/Dodoryu/Dodoryu.bdl", Category::Bosses);
    is("ObjectData/BossKameck/BossKameck.bdl", Category::Bosses);
    is("ObjectData/KinokoOneUp/x.bdl", Category::Items);
    is("ObjectData/PowerStar/PowerStar.bdl", Category::Items);
    is("ObjectData/Coin/Coin.bdl", Category::Items);
    is("ObjectData/HeavensDoorMiddlePlanet/x.bdl", Category::Planets);
    is("ObjectData/HeavensDoorMiddlePlanetLow/x.bdl", Category::Planets);
    is("ObjectData/GalaxySky/GalaxySky.bdl", Category::Skies);
    is("ObjectData/HomeAir/HomeAir.bdl", Category::Skies);
    is("ObjectData/AstroBaseA/x.bdl", Category::Objects);
    is("ParticleData/Effect/x.jpc", Category::Particles);
    is("LayoutData/Font/x", Category::Fonts);
    is("LayoutData/Icon/x", Category::Icons);
    is("LayoutData/BackButton/x", Category::Icons);
    is("LayoutData/PictureBook/x", Category::Story);
    is("LayoutData/PeachLetter/x", Category::Story);
    is("LayoutData/MapGalaxyBg/x", Category::GalaxyMap);
    is("LayoutData/StarCounter/x", Category::Hud);
    is("LayoutData/TalkBalloonEvent/x", Category::Hud);
    is("LayoutData/PauseMenu/x", Category::Menus);
    is("LayoutData/FileSelect/x", Category::Menus);
    is("UsEnglish/LayoutData/Font/x", Category::Fonts);
    is("StageData/HeavensDoorGalaxy/x", Category::Planets);
    is("opening.bnr", Category::Other);
    using galaxy::textures::category_of_pack_folder;
    expect(category_of_pack_folder("Bighead/Characters/Mario") == Category::MarioLuigi, "a pack's Mario folder");
    expect(category_of_pack_folder("HUD and UI") == Category::Hud, "a pack's HUD folder");
    expect(category_of_pack_folder("Particles Effects") == Category::Particles, "a pack's particles folder");
    expect(!category_of_pack_folder("Bighead/misc").has_value(), "words that say nothing");
    expect(!category_of_pack_folder("Mainstream").has_value(), "no word inside another");
}

std::string read_all(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// The game's texture names (the store's names table), a pack's file names, and Dolphin packs converted to them. Sets the
// names index the rest of the tests see.
bool texture_names() {
    using namespace galaxy::textures;
    bool ok = true;
    // The names table as the importer writes it: a header, then name, image, palette, width, height, format, file.
    const std::vector<TextureName> parsed = parse_names(
        "name\timage\tpalette\twidth\theight\tformat\tfile\r\n"
        "Kuribo/KuriboBody\t1111\t0\t32\t32\t14\tObjectData/Kuribo.arc\n"
        "broken row\t12\n"
        "IconA\t2222\taaaa\t16\t16\t9\tLayoutData/IconA.arc\n");
    ok &= expect(parsed.size() == 2u, "two rows read, the broken one left out");
    ok &= expect(std::is_sorted(parsed.begin(), parsed.end(), [](const TextureName& a, const TextureName& b) { return a.hash < b.hash; }),
                 "rows sorted by name hash");
    for (const TextureName& row : parsed) {
        if (row.name == "Kuribo/KuriboBody") {
            ok &= expect(row.image == 0x1111u && row.width == 32u && row.height == 32u && row.format == 14u &&
                             row.category == category_of_path("ObjectData/Kuribo.arc"),
                         "a row's fields and its file's category");
        }
    }
    ok &= expect(name_hash("Kuribo/KuriboBody") == name_hash("kuribo/kuribobody") && name_hash("a") != name_hash("b"),
                 "names hash without case");

    auto row = [](const char* name, std::uint64_t image, std::uint64_t palette, std::uint32_t size, std::uint8_t format,
                  Category category) {
        TextureName out;
        out.name = name;
        out.hash = name_hash(name);
        out.image = image;
        out.palette = palette;
        out.width = size;
        out.height = size;
        out.format = format;
        out.category = category;
        return out;
    };
    set_index_for_tests({row("Kuribo/KuriboBody", 0x1111u, 0u, 32u, 14u, Category::Enemies),
                         row("KuriboMini/KuriboBody", 0x1111u, 0u, 32u, 14u, Category::Enemies),
                         row("IconA", 0x2222u, 0xAAAAu, 16u, 9u, Category::Icons),
                         row("IconB", 0x2222u, 0xBBBBu, 16u, 9u, Category::Icons),
                         row("Pack/Grass", 0x0123456789abcdefull, 0u, 8u, 1u, Category::Planets)});
    ok &= expect(wait_for_index() && names().size() == 5u && find_name(name_hash("ICONA")) != nullptr &&
                     find_name(name_hash("IconC")) == nullptr,
                 "the index finds names");

    // A pack's file: "<folder>/<texture>" for a model's, "<texture>" for a texture file's, under any folders of its own.
    const auto body = parse_pack_file("Enemies/Kuribo", "KuriboBody");
    ok &= expect(body && body->keys[0] == name_hash("kuribo/kuribobody") && body->level == 0u && !body->arbitrary &&
                     pack_file_key(*body) == name_hash("Kuribo/KuriboBody"),
                 "a model's texture by its folder");
    const auto icon = parse_pack_file("Icons", "IconA_arb_mip2");
    ok &= expect(icon && icon->level == 2u && icon->arbitrary && pack_file_key(*icon) == name_hash("IconA"),
                 "a texture file's, its own folder left out, with its level suffixes");
    const auto loose = parse_pack_file("", "IconB");
    ok &= expect(loose && loose->keys[0] == loose->keys[1] && pack_file_key(*loose) == name_hash("IconB"), "at the pack's root");
    ok &= expect(!parse_pack_file("RMGE01", "tex1_32x32_0000000000001111_14").has_value() &&
                     is_dolphin_pack_name("TEX1_8x8_0000000000000001_1") && !is_dolphin_pack_name("tex1"),
                 "Dolphin names are not pack names");

    // Dolphin names.
    for (const char* stem : {"tex1_256x256_m_123c307fab917b99_1", "tex1_200x40_63f3408997969cd6_9a81ceb434acb873_8",
                             "tex1_48x51_53403146169829a2_$_9", "tex1_36x36_4653193aaa14d048_14_mip2",
                             "tex1_64x64_0123456789abcdef_14_arb", "tex1_128x128_m_02b5073505a48fb4_0_arb_mip1",
                             "tex1_1024x8_ffffffffffffffff_0123456789abcdef_10", "tex1_70000x3_0123456789abcdef_14_mip31"}) {
        const auto name = parse_dolphin_name(stem);
        ok &= expect(name && dolphin_name(*name) == stem, stem);
    }
    const auto slip = parse_dolphin_name("tex1_64x128_d4a37bf6901674f2_14v");
    ok &= expect(slip && slip->format == 14u && dolphin_name(*slip) == "tex1_64x128_d4a37bf6901674f2_14",
                 "a letter after the format number is a slip");
    const auto m = parse_dolphin_name("tex1_256x256_m_123c307fab917b99_1");
    ok &= expect(m && m->width == 256u && m->height == 256u && m->mipmapped && m->format == 1u &&
                     m->hash == 0x123c307fab917b99ull && m->palette == 0u && !m->any_palette,
                 "fields of a mipmapped I8 name");
    const auto c4 = parse_dolphin_name("tex1_200x40_63f3408997969cd6_9a81ceb434acb873_8");
    ok &= expect(c4 && c4->format == 8u && c4->palette == 0x9a81ceb434acb873ull, "palette hash of a C4 name");
    const auto arb = parse_dolphin_name("tex1_128x128_m_02b5073505a48fb4_0_arb_mip7");
    ok &= expect(arb && arb->arbitrary && arb->level == 7u && arb->mipmapped, "arbitrary level suffix");
    for (const char* stem : {"tex1_64x64_0123456789abcde_14", "tex1_64x64_0123456789abcdef_7",
                             "tex1_64x64_0123456789abcdef_1234567890abcdef_14", "target1_n000007_160x120_6",
                             "tex1_0x64_0123456789abcdef_14", "tex1_64x64_0123456789abcdef_14_mip0",
                             "tex1_64x64_0123456789abcdef_14_mip2_arb", "tex1_64x64_0123456789abcdef_14_extra",
                             "tex1_4294967296x1_0123456789abcdef_14", "tex1_64x64_m", "tex1_128x128_cf175626cca47369_142"}) {
        ok &= expect(!parse_dolphin_name(stem).has_value(), stem);
    }
    const auto any_icon = parse_dolphin_name("tex1_16x16_0000000000002222_$_9");
    const auto own_icon = parse_dolphin_name("tex1_16x16_0000000000002222_000000000000aaaa_9");
    const TextureName& icon_b = *find_name(name_hash("IconB"));
    ok &= expect(any_icon && own_icon && dolphin_matches(*any_icon, icon_b) && !dolphin_matches(*own_icon, icon_b),
                 "a palette name matches its palette, $ any");

    // A Dolphin pack converted: each file moves to .dolphin/, linked under every game texture it stands for.
    const fs::path pack = galaxy::test_temp_path("nebula_tex1_pack");
    std::error_code ec;
    fs::remove_all(pack, ec);
    write(pack / "RMGE01" / "a" / "tex1_32x32_m_0000000000001111_14.png", "body");
    write(pack / "RMGE01" / "a" / "tex1_32x32_m_0000000000001111_14_mip1.png", "body1");
    write(pack / "RMGE01" / "tex1_16x16_0000000000002222_000000000000aaaa_9.png", "icon a");
    write(pack / "RMGE01" / "tex1_16x16_0000000000002222_$_9.png", "icon any");
    write(pack / "RMGE01" / "tex1_64x64_0000000000009999_6.png", "other game");
    write(pack / "RMGE01" / "notes.txt", "notes");
    ok &= expect(has_dolphin_files(pack), "a Dolphin pack found");
    const DolphinConversion done = convert_dolphin_pack(pack, names());
    ok &= expect(done.files == 5u && done.textures == 6u && done.taken == 1u && done.failed.empty() &&
                     done.unmatched == std::vector<std::string>{"RMGE01/tex1_64x64_0000000000009999_6.png"},
                 "five files: six names, one taken, one for another game");
    const fs::path game = pack / "RMGE01";
    ok &= expect(read_all(game / "Kuribo" / "KuriboBody.png") == "body" &&
                     read_all(game / "KuriboMini" / "KuriboBody.png") == "body" &&
                     read_all(game / "Kuribo" / "KuriboBody_mip1.png") == "body1" &&
                     read_all(game / "IconA.png") == "icon a" && read_all(game / "IconB.png") == "icon any",
                 "linked under the game's names, in the game folder, a named palette before $");
    ok &= expect(fs::exists(pack / ".dolphin" / "RMGE01" / "a" / "tex1_32x32_m_0000000000001111_14.png") &&
                     !fs::exists(game / "a") && fs::exists(game / "notes.txt") &&
                     read_all(pack / ".dolphin" / "unmatched.txt").find("tex1_64x64_0000000000009999_6.png") != std::string::npos,
                 "originals kept in .dolphin/, emptied folders gone, the unmatched listed");
    ok &= expect(!has_dolphin_files(pack) && convert_dolphin_pack(pack, names()).files == 0u, "converted once");

    // An installed mod's Dolphin pack is converted when the mods are first read.
    const fs::path mods = galaxy::test_temp_path("nebula_tex1_mods");
    fs::remove_all(mods, ec);
    write(mods / "mods" / "Old Pack" / "textures" / "tex1_32x32_0000000000001111_14.png", "old");
    galaxy::mods::set_mods_folder(mods / "mods", mods / "mods.ini", "RMGE01");
    const std::vector<galaxy::mods::ModInfo> found = galaxy::mods::scan_mods();
    const auto old = std::find_if(found.begin(), found.end(), [](const galaxy::mods::ModInfo& m) { return m.id == "Old Pack"; });
    ok &= expect(old != found.end() && old->textures && old->game_textures == 2u && old->changed_textures == 2u &&
                     old->category_textures[static_cast<std::size_t>(Category::Enemies)] == 2u &&
                     read_all(mods / "mods" / "Old Pack" / "textures" / "Kuribo" / "KuriboBody.png") == "old",
                 "an installed Dolphin pack converted and sorted");
    fs::remove_all(pack, ec);
    fs::remove_all(mods, ec);
    return ok;
}

namespace {

// A text option's value as mods.ini keeps it (services/mods.md 8.4): UTF-8, no control character, at most max_length
// code points.
bool text_options() {
    using galaxy::mods::text_option_fits;
    bool ok = true;
    ok &= expect(text_option_fits("", 1u), "empty text fits");
    ok &= expect(text_option_fits("Mario 64", 8u), "8 ASCII characters fit 8");
    ok &= expect(!text_option_fits("Mario 64!", 8u), "9 characters do not fit 8");
    ok &= expect(text_option_fits("\xE3\x83\x9E\xE3\x83\xAA\xE3\x82\xAA", 3u), "3 Japanese characters fit 3");
    ok &= expect(text_option_fits("\xF0\x9F\x8D\x84", 1u), "one 4-byte character fits 1");
    ok &= expect(!text_option_fits("a\nb", 8u), "a newline is refused");
    ok &= expect(!text_option_fits("a\x7F", 8u), "DEL is refused");
    ok &= expect(!text_option_fits("\xC2\x85", 8u), "a C1 control is refused");
    ok &= expect(!text_option_fits("\xE3\x83", 8u), "a character cut short is refused");
    ok &= expect(!text_option_fits("\xC0\xAF", 8u), "an overlong form is refused");
    ok &= expect(!text_option_fits("\xED\xA0\x80", 8u), "a surrogate is refused");
    ok &= expect(!text_option_fits("\x80", 8u), "a lone continuation byte is refused");
    return ok;
}

}  // namespace

int main() {
    test_texture_categories();
    // Every test runs, whichever fail.
    const bool results[] = {texture_names(), xml(), importing(), code_patch_notes(), setting_overlaps(), patches_by_key_in_the_plan(), layer_only_mods(), layer_assets(), galaxy_saves(),
                            luma_rules(), luma_import(), text_options()};
    bool ok = true;
    for (const bool passed : results) ok = ok && passed;
    std::cout << (ok ? "mods tests passed" : "mods tests FAILED") << '\n';
    return ok ? 0 : 1;
}
