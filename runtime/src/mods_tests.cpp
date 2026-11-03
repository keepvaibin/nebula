#include <string_view>
// SPDX-License-Identifier: GPL-3.0-only
// Mods (mods.h, virtual_disc.h, mod_xml.h): the XML reader, the disc's patching and reads, Riivolution mods, files
// mods, load order, saved choices, savegames, main.dol's memory patches, the mods' layouts and languages, and importing.

#include "galaxy/mod_import.h"
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
#include <vector>

namespace {

namespace fs = std::filesystem;

bool expect(bool condition, const char* message) {
    if (!condition) std::cerr << "FAIL: " << message << '\n';
    return condition;
}

void put32(std::vector<std::uint8_t>& b, std::size_t at, std::uint32_t v) {
    b[at] = static_cast<std::uint8_t>(v >> 24u);
    b[at + 1] = static_cast<std::uint8_t>(v >> 16u);
    b[at + 2] = static_cast<std::uint8_t>(v >> 8u);
    b[at + 3] = static_cast<std::uint8_t>(v);
}

std::uint32_t get32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) << 24u | static_cast<std::uint32_t>(p[1]) << 16u |
           static_cast<std::uint32_t>(p[2]) << 8u | static_cast<std::uint32_t>(p[3]);
}

void write(const fs::path& path, std::string_view text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

// A partition: a file table (/opening.bnr, /StageData/A.arc, /StageData/B.arc), its files, main.dol.
// Reads a file through a built table: its position and size by path ("/StageData/A.arc").
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
    return ok;
}

// A partition with these files (path, contents), directories as the paths name them; an empty main.dol after them.
// Loose files, every name match, disc layouts a folder down, packs without textures/, hidden folders, region
// options, a sole option as the mod's switch, and text in the language played.
// Importing: zip and 7z made with libarchive's writer, a top folder lifted, a path that would leave refused, an
// update in place, a file that is no archive.
bool importing() {
    bool ok = true;
    const fs::path root = fs::temp_directory_path() / "nebula_mods_import";
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
    ok &= expect(again.ok && again.replaced, "the same mod again: updated in place");
    const galaxy::mods::ImportResult pack = galaxy::mods::import_mod(root / "Pack.7z", root / "mods");
    ok &= expect(pack.ok && pack.kinds == "TEXTURES", "a 7z texture pack imported");
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

// A patch by the disc file's offsets into a file the import cooked is refused and reported; the game's file stays.
// Each kind of game file goes to the category a player would look for it in.
void test_texture_categories() {
    using galaxy::textures::Category;
    using galaxy::textures::category_of_path;
    const auto is = [](std::string_view path, Category category) {
        expect(category_of_path(path) == category, std::string(path).c_str());
    };
    is("ObjectData/Mario.arc/Mario.bdl", Category::MarioLuigi);
    is("ObjectData/BeeMario.arc/BeeMario.bdl", Category::MarioLuigi);
    is("ObjectData/Kuribo.arc/Kuribo.bdl", Category::Enemies);
    is("ObjectData/BasaBasaHang.arc/x.bdl", Category::Enemies);
    is("ObjectData/Rosetta.arc/Rosetta.bdl", Category::Characters);
    is("ObjectData/TicoFat.arc/TicoFat.bdl", Category::Characters);
    is("ObjectData/Dodoryu.arc/Dodoryu.bdl", Category::Bosses);
    is("ObjectData/BossKameck.arc/BossKameck.bdl", Category::Bosses);
    is("ObjectData/KinokoOneUp.arc/x.bdl", Category::Items);
    is("ObjectData/PowerStar.arc/PowerStar.bdl", Category::Items);
    is("ObjectData/Coin.arc/Coin.bdl", Category::Items);
    is("ObjectData/HeavensDoorMiddlePlanet.arc/x.bdl", Category::Planets);
    is("ObjectData/HeavensDoorMiddlePlanetLow.arc/x.bdl", Category::Planets);
    is("ObjectData/GalaxySky.arc/GalaxySky.bdl", Category::Skies);
    is("ObjectData/HomeAir.arc/HomeAir.bdl", Category::Skies);
    is("ObjectData/AstroBaseA.arc/x.bdl", Category::Objects);
    is("ParticleData/Effect.arc/x.jpc", Category::Particles);
    is("LayoutData/Font.arc/x", Category::Fonts);
    is("LayoutData/Icon.arc/x", Category::Icons);
    is("LayoutData/BackButton.arc/x", Category::Icons);
    is("LayoutData/PictureBook.arc/x", Category::Story);
    is("LayoutData/PeachLetter.arc/x", Category::Story);
    is("LayoutData/MapGalaxyBg.arc/x", Category::GalaxyMap);
    is("LayoutData/StarCounter.arc/x", Category::Hud);
    is("LayoutData/TalkBalloonEvent.arc/x", Category::Hud);
    is("LayoutData/PauseMenu.arc/x", Category::Menus);
    is("LayoutData/FileSelect.arc/x", Category::Menus);
    is("UsEnglish/LayoutData/Font.arc/x", Category::Fonts);
    is("StageData/HeavensDoorGalaxy.arc/x", Category::Planets);
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
