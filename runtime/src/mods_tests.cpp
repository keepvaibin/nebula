// SPDX-License-Identifier: GPL-3.0-only
// Mods (mods.h, virtual_disc.h, mod_xml.h): the XML reader, the disc's patching and reads, Riivolution mods, files
// mods, load order, saved choices, savegames, main.dol's memory patches, the mods' layouts and languages, and importing.

#include "galaxy/asset_store.h"
#include "galaxy/dolphin_pack.h"
#include "galaxy/texture_categories.h"
#include "galaxy/mod_import.h"
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

int main() {
    test_texture_categories();
    const bool ok = texture_names() & xml() & importing() & code_patch_notes();
    std::cout << (ok ? "mods tests passed" : "mods tests FAILED") << '\n';
    return ok ? 0 : 1;
}
