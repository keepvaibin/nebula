// SPDX-License-Identifier: GPL-3.0-only
// Mods (mods.h, virtual_disc.h, mod_xml.h): the XML reader, the disc's patching and reads, Riivolution mods, files
// mods, load order, saved choices, savegames and main.dol's memory patches.

#include "galaxy/mod_xml.h"
#include "galaxy/mods.h"
#include "galaxy/virtual_disc.h"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using galaxy::mods::VirtualDisc;

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
struct Partition {
    std::vector<std::uint8_t> bytes = std::vector<std::uint8_t>(0x30000, 0);
    std::vector<std::uint8_t> fst;
    std::size_t dol = 0x20000;
    std::size_t dol_size = 0;

    Partition() {
        const char* strings = "opening.bnr\0StageData\0A.arc\0B.arc\0";
        const std::size_t strings_size = 12 + 10 + 6 + 6;
        fst.assign(5 * 12 + strings_size, 0);
        put32(fst, 0, 0x01000000u);
        put32(fst, 8, 5u);
        put32(fst, 12, 0u);  // opening.bnr
        put32(fst, 16, 0x8000u >> 2u);
        put32(fst, 20, 16u);
        put32(fst, 24, 0x01000000u | 12u);  // StageData/
        put32(fst, 28, 0u);
        put32(fst, 32, 5u);
        put32(fst, 36, 22u);  // A.arc
        put32(fst, 40, 0x10000u >> 2u);
        put32(fst, 44, 8u);
        put32(fst, 48, 28u);  // B.arc
        put32(fst, 52, 0x18000u >> 2u);
        put32(fst, 56, 4u);
        std::memcpy(fst.data() + 60, strings, strings_size);
        std::memcpy(bytes.data() + 0x8000, "BANNER..........", 16);
        std::memcpy(bytes.data() + 0x10000, "AAAAAAAA", 8);
        std::memcpy(bytes.data() + 0x18000, "BBBB", 4);
        // main.dol: one data section at 8053C000 (0xC000 bytes: the tables the native game reads).
        dol_size = 0x100 + 0xC000;
        bytes.resize(dol + dol_size, 0);
        put32(bytes, dol + 0x1C, 0x100u);
        put32(bytes, dol + 0x48 + 0x1C, 0x8053C000u);
        put32(bytes, dol + 0x90 + 0x1C, 0xC000u);
        bytes[dol + 0x100 + 0x800] = 0x11;  // galaxy info, 8053C800
    }
};

// Reads a file through a built table: its position and size by path ("/StageData/A.arc").
std::string read_file(const VirtualDisc& disc, const std::vector<std::uint8_t>& fst, const std::uint8_t* partition,
                      std::string_view path) {
    const std::uint32_t count = get32(fst.data() + 8);
    const char* strings = reinterpret_cast<const char*>(fst.data()) + count * 12u;
    // A linear walk with directories by name, enough for these tables.
    std::string current;
    std::vector<std::pair<std::uint32_t, std::string>> dirs{{count, ""}};
    for (std::uint32_t i = 1; i < count; ++i) {
        while (i >= dirs.back().first) dirs.pop_back();
        const std::uint8_t* e = fst.data() + i * 12u;
        const std::string name = strings + (get32(e) & 0xFFFFFFu);
        const std::string full = dirs.back().second + "/" + name;
        if (e[0] != 0u) {
            dirs.emplace_back(get32(e + 8), full);
        } else if (full == path) {
            const std::uint64_t at = static_cast<std::uint64_t>(get32(e + 4)) << 2u;
            const std::uint32_t size = get32(e + 8);
            std::string out(size, '\0');
            if (size != 0u &&
                !disc.read(at, size, reinterpret_cast<std::uint8_t*>(out.data()), partition)) {
                return "<unreadable>";
            }
            return out;
        }
    }
    return "<absent>";
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
    return ok;
}

bool disc() {
    bool ok = true;
    const fs::path root = fs::temp_directory_path() / "nebula_mods_disc";
    std::error_code ec;
    fs::remove_all(root, ec);
    write(root / "new_a.arc", "replaced A, longer");
    write(root / "piece.bin", "xy");
    write(root / "c.bin", "CCC");
    Partition p;
    VirtualDisc disc;
    std::string error;
    ok &= expect(disc.load(p.fst, p.bytes.size(), &error), "the table loads");
    galaxy::mods::DiscNode* a = disc.find_path("stagedata/a.ARC", false);
    ok &= expect(a != nullptr && a->size == 8u, "paths are found whatever their case");
    VirtualDisc::patch(*a, root / "new_a.arc", 18, 0, 0, 0, true);
    galaxy::mods::DiscNode* b = disc.find_name("b.arc");
    ok &= expect(b != nullptr, "a file by its name alone");
    VirtualDisc::patch(*b, root / "piece.bin", 2, 1, 0, 0, false);
    galaxy::mods::DiscNode* c = disc.find_path("/New/Deep/C.bin", true);
    ok &= expect(c != nullptr && !c->directory, "a new file and its folders");
    VirtualDisc::patch(*c, root / "c.bin", 3, 0, 0, 0, true);
    ok &= expect(disc.find_path("/StageData/A.arc/x", true) == nullptr, "a file is no folder");
    const std::vector<std::uint8_t> fst = disc.build(p.bytes.size());
    ok &= expect(!fst.empty(), "the table builds");
    ok &= expect(read_file(disc, fst, p.bytes.data(), "/StageData/A.arc") == "replaced A, longer", "a replaced file");
    ok &= expect(read_file(disc, fst, p.bytes.data(), "/StageData/B.arc") == "BxyB", "a patch inside a file keeps the rest");
    ok &= expect(read_file(disc, fst, p.bytes.data(), "/New/Deep/C.bin") == "CCC", "an added file");
    ok &= expect(read_file(disc, fst, p.bytes.data(), "/opening.bnr") == "BANNER..........", "an untouched file");
    ok &= expect(get32(fst.data() + 16) == (0x8000u >> 2u), "an untouched file keeps its place");
    ok &= expect(disc.changed_files() == 3u, "three files placed past the partition");
    fs::remove_all(root, ec);
    return ok;
}

bool riivolution() {
    bool ok = true;
    const fs::path root = fs::temp_directory_path() / "nebula_mods_riiv";
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path mods = root / "mods";
    // A Riivolution mod: an option with two choices, its parameters, a folder patch, a savegame, memory patches.
    write(mods / "1 Riiv" / "riivolution" / "Mod.XML",
          "<wiidisc version=\"1\" root=\"/MyMod\">"
          "<id game=\"RMG\"><region type=\"E\"/><region type=\"P\"/></id>"
          "<options><section name=\"Main\">"
          "<option name=\"Stage\" id=\"stage\" default=\"1\"><choice name=\"Red\"><patch id=\"files\">"
          "<param name=\"colour\" value=\"red\"/></patch></choice>"
          "<choice name=\"Blue\"><patch id=\"files\"><param name=\"colour\" value=\"blue\"/></patch></choice></option>"
          "<option name=\"Extras\" default=\"1\"><choice name=\"On\"><patch id=\"extras\"/></choice></option>"
          "</section></options>"
          "<patch id=\"files\"><file disc=\"/StageData/A.arc\" external=\"{$colour}.arc\"/>"
          "<folder disc=\"/ObjectData\" external=\"objects\" create=\"true\"/>"
          "<savegame external=\"/save/{$__gameid}{$__region}\" clone=\"true\"/></patch>"
          "<patch id=\"extras\"><file disc=\"B.arc\" external=\"/MyMod/b.arc\"/>"
          "<memory offset=\"0x8053C800\" value=\"22\" original=\"11\"/>"
          "<memory offset=\"0x80004000\" value=\"60000000\"/>"
          "<memory offset=\"0x80001000\" value=\"48000000\" ocarina=\"true\"/></patch>"
          "</wiidisc>");
    write(mods / "1 Riiv" / "MyMod" / "red.arc", "RED");
    write(mods / "1 Riiv" / "MyMod" / "blue.arc", "BLUE");
    write(mods / "1 Riiv" / "MyMod" / "b.arc", "bb");
    write(mods / "1 Riiv" / "MyMod" / "objects" / "Coin.arc", "COIN");
    // A files mod after it: its A.arc wins.
    write(mods / "2 Files" / "StageData" / "A.arc", "FILES");
    // A mod for another game.
    write(mods / "3 Other" / "riivolution" / "x.xml", "<wiidisc version=\"1\"><id game=\"SB4\"/></wiidisc>");
    galaxy::mods::set_mods_folder(mods, root / "mods.ini", "RMGE01");

    std::vector<galaxy::mods::ModInfo> list = galaxy::mods::scan_mods();
    ok &= expect(list.size() == 3u && list[0].id == "1 Riiv" && list[0].riivolution && list[1].files,
                 "three mods, in folder order");
    ok &= expect(list.size() == 3u && list[0].options.size() == 2u && list[0].options[0].selected == 1u &&
                     list[0].options[0].choices.size() == 2u,
                 "the Riivolution options and their defaults");
    ok &= expect(list.size() == 3u && list[2].problem == "FOR ANOTHER GAME", "a mod for another game");

    Partition p;
    const auto active = galaxy::mods::apply_mods(p.fst, std::span(p.bytes.data() + p.dol, p.dol_size), p.dol,
                                                 p.bytes.data(), p.bytes.size(), "RMGE01", 0, 0);
    ok &= expect(active != nullptr && !active->fst.empty(), "mods applied");
    if (active) {
        ok &= expect(read_file(active->disc, active->fst, p.bytes.data(), "/StageData/A.arc") == "FILES",
                     "the later mod wins");
        ok &= expect(read_file(active->disc, active->fst, p.bytes.data(), "/StageData/B.arc") == "bb",
                     "a disc path by name; an absolute external path");
        ok &= expect(read_file(active->disc, active->fst, p.bytes.data(), "/ObjectData/Coin.arc") == "COIN",
                     "a folder patch creates files");
        ok &= expect(active->save_folder == mods / "1 Riiv" / "save" / "RMGE" && active->save_clone,
                     "the savegame folder, its placeholders filled");
        ok &= expect(!active->dol.empty() && active->dol[0x100 + 0x800] == 0x22, "a memory patch over a data table");
        ok &= expect(active->unsupported == 2u, "code and ocarina patches reported unsupported");
        ok &= expect(active->texture_folders.empty(), "no textures");
    }

    // Saved choices: Blue, Extras off, the files mod off.
    list[0].options[0].selected = 2;
    list[0].options[1].selected = 0;
    list[1].enabled = false;
    ok &= expect(galaxy::mods::save_mod_settings(list), "choices saved");
    const auto again = galaxy::mods::apply_mods(p.fst, std::span(p.bytes.data() + p.dol, p.dol_size), p.dol,
                                                p.bytes.data(), p.bytes.size(), "RMGE01", 0, 0);
    if (again) {
        ok &= expect(read_file(again->disc, again->fst, p.bytes.data(), "/StageData/A.arc") == "BLUE", "a saved choice");
        ok &= expect(read_file(again->disc, again->fst, p.bytes.data(), "/StageData/B.arc") == "BBBB",
                     "an option turned off");
        ok &= expect(again->dol.empty(), "no memory patch: main.dol untouched");
    } else {
        ok &= expect(false, "mods applied again");
    }
    // Load order moved: the files mod first, on again; the Riivolution mod's A.arc wins now.
    list[1].enabled = true;
    list[0].order = 2;
    list[1].order = 1;
    ok &= expect(galaxy::mods::save_mod_settings(list), "order saved");
    const std::vector<galaxy::mods::ModInfo> reordered = galaxy::mods::scan_mods();
    ok &= expect(reordered.size() == 3u && reordered[0].id == "2 Files", "the saved load order");
    const auto third = galaxy::mods::apply_mods(p.fst, std::span(p.bytes.data() + p.dol, p.dol_size), p.dol,
                                                p.bytes.data(), p.bytes.size(), "RMGE01", 0, 0);
    if (third) {
        ok &= expect(read_file(third->disc, third->fst, p.bytes.data(), "/StageData/A.arc") == "BLUE",
                     "the later mod in the new order wins");
    }
    // Every mod off: nothing applies.
    for (auto& m : list) m.enabled = false;
    ok &= expect(galaxy::mods::save_mod_settings(list), "all off saved");
    ok &= expect(galaxy::mods::apply_mods(p.fst, std::span(p.bytes.data() + p.dol, p.dol_size), p.dol, p.bytes.data(),
                                          p.bytes.size(), "RMGE01", 0, 0) == nullptr,
                 "no mod on: the game's disc");
    fs::remove_all(root, ec);
    return ok;
}

}  // namespace

int main() {
    const bool ok = xml() & disc() & riivolution();
    std::cout << (ok ? "mods tests passed" : "mods tests FAILED") << '\n';
    return ok ? 0 : 1;
}
