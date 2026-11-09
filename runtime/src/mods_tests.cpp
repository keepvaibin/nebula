// SPDX-License-Identifier: GPL-3.0-only
// Mods (mods.h, virtual_disc.h, mod_xml.h): the XML reader, the disc's patching and reads, Riivolution mods, files
// mods, load order, saved choices, savegames, main.dol's memory patches, the mods' layouts and languages, and importing.

#include "galaxy/mod_import.h"
#include "galaxy/test_temp.h"
#include "galaxy/mod_xml.h"
#include "galaxy/mods.h"

#include <archive.h>
#include <archive_entry.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
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
    ok &= expect(galaxy::mods::remove_mod(root / "mods", "Pack", &message) && !fs::exists(root / "mods" / "Pack"),
                 "a mod removed");
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
    using galaxy::textures::hash_of_pack_name;
    expect(hash_of_pack_name("tex1_64x64_m_0123456789abcdef_14") == 0x0123456789abcdefull, "a name's hash");
    expect(hash_of_pack_name("tex1_8x8_00000000000000ff_$_9") == 0xffull, "a palette texture's hash");
    expect(!hash_of_pack_name("notes").has_value(), "not a texture name");
}

int main() {
    test_texture_categories();
    const bool ok = xml() & importing();
    std::cout << (ok ? "mods tests passed" : "mods tests FAILED") << '\n';
    return ok ? 0 : 1;
}
