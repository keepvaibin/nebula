// SPDX-License-Identifier: GPL-3.0-only
// Mods (mods.h, virtual_disc.h, mod_xml.h): the XML reader, the disc's patching and reads, Riivolution mods, files
// mods, load order, saved choices, savegames, main.dol's memory patches, the mods' layouts and languages, and importing.

#include "galaxy/mod_import.h"
#include "galaxy/mod_xml.h"
#include "galaxy/mods.h"
#include "galaxy/virtual_disc.h"

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
    // A files mod below it: its A.arc is covered.
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
        ok &= expect(read_file(active->disc, active->fst, p.bytes.data(), "/StageData/A.arc") == "RED",
                     "the mod on top wins");
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
    // Order moved: the files mod on top, on again; its A.arc wins now.
    list[1].enabled = true;
    list[0].order = 2;
    list[1].order = 1;
    ok &= expect(galaxy::mods::save_mod_settings(list), "order saved");
    const std::vector<galaxy::mods::ModInfo> reordered = galaxy::mods::scan_mods();
    ok &= expect(reordered.size() == 3u && reordered[0].id == "2 Files", "the saved load order");
    const auto third = galaxy::mods::apply_mods(p.fst, std::span(p.bytes.data() + p.dol, p.dol_size), p.dol,
                                                p.bytes.data(), p.bytes.size(), "RMGE01", 0, 0);
    if (third) {
        ok &= expect(read_file(third->disc, third->fst, p.bytes.data(), "/StageData/A.arc") == "FILES",
                     "the mod on top in the new order wins");
    }
    // Turned off: after the enabled mods, by name; turned on: the top.
    {
        std::vector<galaxy::mods::ModInfo> two = galaxy::mods::scan_mods();
        const auto ids = [&] {
            std::string out;
            for (const auto& m : two) out += m.id + (m.enabled ? "+" : "-") + std::to_string(m.order) + ",";
            return out;
        };
        galaxy::mods::set_mod_enabled(two, 0, false);
        galaxy::mods::set_mod_enabled(two, 0, false);
        ok &= expect(ids() == "3 Other+1,1 Riiv-2,2 Files-3,", "disabled mods by name, after the enabled ones");
        galaxy::mods::set_mod_enabled(two, 2, true);
        ok &= expect(ids() == "2 Files+1,3 Other+2,1 Riiv-3,", "an enabled mod goes on top");
        ok &= expect(galaxy::mods::save_mod_settings(two), "on/off saved");
        two = galaxy::mods::scan_mods();
        ok &= expect(ids() == "2 Files+1,3 Other+2,1 Riiv-3,", "the lists as saved");
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

// A partition with these files (path, contents), directories as the paths name them; an empty main.dol after them.
struct Disc {
    std::vector<std::uint8_t> bytes;
    std::vector<std::uint8_t> fst;
    std::size_t dol = 0;
    std::size_t dol_size = 0x100;

    explicit Disc(const std::vector<std::pair<std::string, std::string>>& files) {
        struct Node {
            std::string name;
            bool dir = false;
            std::string data;
            std::vector<Node> children;
        };
        Node root{"", true, {}, {}};
        for (const auto& [path, data] : files) {
            Node* at = &root;
            std::size_t start = 1;
            while (true) {
                const std::size_t slash = path.find('/', start);
                const std::string part = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
                auto it = std::find_if(at->children.begin(), at->children.end(),
                                       [&](const Node& n) { return n.name == part; });
                if (it == at->children.end()) {
                    at->children.push_back({part, slash != std::string::npos, {}, {}});
                    it = at->children.end() - 1;
                }
                if (slash == std::string::npos) {
                    it->data = data;
                    break;
                }
                at = &*it;
                start = slash + 1;
            }
        }
        std::vector<std::uint8_t> entries(12, 0);
        std::string strings;
        bytes.assign(0x8000, 0);
        const auto emit = [&](auto&& self, const Node& dir, std::uint32_t parent) -> void {
            for (const Node& n : dir.children) {
                const std::size_t at = entries.size();
                entries.resize(at + 12, 0);
                put32(entries, at, (n.dir ? 0x01000000u : 0u) | static_cast<std::uint32_t>(strings.size()));
                strings += n.name;
                strings.push_back('\0');
                if (n.dir) {
                    put32(entries, at + 4, parent);
                    self(self, n, static_cast<std::uint32_t>(at / 12));
                    put32(entries, at + 8, static_cast<std::uint32_t>(entries.size() / 12));
                } else {
                    const std::size_t offset = (bytes.size() + 31u) & ~std::size_t{31};
                    bytes.resize(offset + n.data.size(), 0);
                    std::memcpy(bytes.data() + offset, n.data.data(), n.data.size());
                    put32(entries, at + 4, static_cast<std::uint32_t>(offset >> 2u));
                    put32(entries, at + 8, static_cast<std::uint32_t>(n.data.size()));
                }
            }
        };
        emit(emit, root, 0);
        put32(entries, 0, 0x01000000u);
        put32(entries, 8, static_cast<std::uint32_t>(entries.size() / 12));
        fst = entries;
        fst.insert(fst.end(), strings.begin(), strings.end());
        dol = (bytes.size() + 0xFFFu) & ~std::size_t{0xFFF};
        bytes.resize(dol + dol_size, 0);
    }
    std::unique_ptr<galaxy::mods::ActiveMods> apply(const std::string& game_id, const std::string& language) {
        return galaxy::mods::apply_mods(fst, std::span(bytes.data() + dol, dol_size), dol, bytes.data(), bytes.size(),
                                        game_id, 0, 0, language);
    }
};

// Loose files, every name match, disc layouts a folder down, packs without textures/, hidden folders, region
// options, a sole option as the mod's switch, and text in the language played.
bool layouts() {
    bool ok = true;
    const fs::path root = fs::temp_directory_path() / "nebula_mods_layouts";
    std::error_code ec;
    fs::remove_all(root, ec);
    const fs::path mods = root / "mods";
    write(mods / "A Loose" / "Mario.arc", "NEWMARIO");
    write(mods / "A Loose" / "readme.txt", "put these in ObjectData");
    write(mods / "A Loose" / "Nowhere.arc", "unused");
    write(mods / "B Nested" / "MyMod" / "StageData" / "A.arc", "NESTED");
    write(mods / "C Pack" / "RMGE01" / "tex1_8x8_0123456789abcdef_1.png", "png");
    write(mods / ".importing-x" / "StageData" / "A.arc", "HALF");
    write(mods / "D Text" / "riivolution" / "t.xml",
          "<wiidisc version=\"1\"><id game=\"RMG\"/><options><section name=\"S\"><option name=\"Mod\">"
          "<choice name=\"Enabled\"><patch id=\"p\"/></choice></option></section></options>"
          "<patch id=\"p\"><folder disc=\"/UsEnglish\" external=\"/T/UsEnglish\" create=\"true\"/></patch></wiidisc>");
    write(mods / "D Text" / "T" / "UsEnglish" / "MessageData" / "Message.arc", "MOD ENGLISH");
    write(mods / "E Region" / "riivolution" / "r.xml",
          "<wiidisc version=\"1\"><id game=\"RMG\"/><options><section name=\"K\"><option name=\"Region:\">"
          "<choice name=\"PAL\"><patch id=\"pal\"/></choice><choice name=\"NTSC\"><patch id=\"ntsc\"/></choice>"
          "</option><option name=\"Extra\"><choice name=\"On\"><patch id=\"x\"/></choice></option></section></options>"
          "<patch id=\"pal\"><file disc=\"/StageData/B.arc\" external=\"/R/pal.arc\"/></patch>"
          "<patch id=\"ntsc\"><file disc=\"/StageData/B.arc\" external=\"/R/ntsc.arc\"/></patch>"
          "<patch id=\"x\"><file disc=\"/StageData/A.arc\" external=\"/R/x.arc\"/></patch></wiidisc>");
    write(mods / "E Region" / "R" / "pal.arc", "PAL");
    write(mods / "E Region" / "R" / "ntsc.arc", "NTSC");
    write(mods / "E Region" / "R" / "x.arc", "EXTRA");
    write(mods / "F Below" / "StageData" / "A.arc", "BELOW");
    galaxy::mods::set_mods_folder(mods, root / "mods.ini", "RMGE01");
    std::vector<galaxy::mods::ModInfo> list = galaxy::mods::scan_mods();
    ok &= expect(list.size() == 6u, "six mods; a hidden import folder is none");
    const auto find = [&](std::string_view id) -> const galaxy::mods::ModInfo* {
        for (const auto& m : list) {
            if (m.id == id) return &m;
        }
        return nullptr;
    };
    ok &= expect(find("A Loose") && find("A Loose")->loose && !find("A Loose")->files, "loose files");
    ok &= expect(find("B Nested") && find("B Nested")->files, "the disc's folders a folder down");
    ok &= expect(find("C Pack") && find("C Pack")->textures, "a pack without a textures folder");
    ok &= expect(find("D Text") && find("D Text")->options.size() == 1u && find("D Text")->options[0].selected == 1u &&
                     find("D Text")->languages == std::vector<std::string>{"UsEnglish"},
                 "a sole one-choice option is on; the text's language");
    const galaxy::mods::ModInfo* nested = find("B Nested");
    ok &= expect(nested && nested->changed_files == 1u && nested->overlaps.size() == 1u &&
                     nested->overlaps[0].other == "F Below" && !nested->overlaps[0].other_above &&
                     nested->overlaps[0].files == 1u,
                 "a mod above another covers the file both change (an off choice changes nothing)");
    const galaxy::mods::ModInfo* below = find("F Below");
    ok &= expect(below && below->overlaps.size() == 1u && below->overlaps[0].other_above,
                 "the mod below is covered");
    const galaxy::mods::ModInfo* loose = find("A Loose");
    ok &= expect(loose && loose->changed_files == 2u, "loose files counted by name");
    const galaxy::mods::ModInfo* region = find("E Region");
    ok &= expect(region && region->options.size() == 2u && region->options[0].selected == 2u &&
                     region->options[0].other_region == std::vector<bool>{true, false} &&
                     region->options[1].selected == 0u,
                 "a region option takes this disc's region; an extra stays off");

    Disc disc({{"/ObjectData/Mario.arc", "MARIO"}, {"/Extra/Mario.arc", "MARIO2"}, {"/StageData/A.arc", "AAAA"},
               {"/StageData/B.arc", "BBBB"}, {"/UsEnglish/MessageData/Message.arc", "ENGLISH"},
               {"/UsFrench/MessageData/Message.arc", "FRENCH"}});
    auto active = disc.apply("RMGE01", "UsFrench");
    ok &= expect(active != nullptr && !active->fst.empty(), "applied");
    if (active) {
        const auto read = [&](std::string_view path) {
            return read_file(active->disc, active->fst, disc.bytes.data(), path);
        };
        ok &= expect(read("/ObjectData/Mario.arc") == "NEWMARIO" && read("/Extra/Mario.arc") == "NEWMARIO",
                     "a loose file replaces every file of its name");
        ok &= expect(read("/StageData/A.arc") == "NESTED", "a nested disc layout");
        ok &= expect(read("/StageData/B.arc") == "NTSC", "this disc's region chosen");
        ok &= expect(read("/UsEnglish/MessageData/Message.arc") == "MOD ENGLISH", "the mod's English");
        ok &= expect(read("/UsFrench/MessageData/Message.arc") == "MOD ENGLISH",
                     "French played: the mod's English stands in (it has no French)");
        ok &= expect(read("/UsFrench/MessageData/NebulaText.txt") == "language = UsEnglish\n",
                     "Nebula's own text follows the mod's English");
        ok &= expect(active->texture_folders.size() == 1u && active->texture_folders[0] == mods / "C Pack",
                     "the pack's folder for the textures");
        ok &= expect(active->report.find("Nowhere.arc: no file of that name") != std::string::npos,
                     "a loose file no disc file is named for is reported");
    }
    // The game's French kept by choice.
    for (auto& m : list) {
        if (m.id == "D Text") m.own_text = false;
    }
    ok &= expect(galaxy::mods::save_mod_settings(list), "text choice saved");
    auto kept = disc.apply("RMGE01", "UsFrench");
    ok &= expect(kept && read_file(kept->disc, kept->fst, disc.bytes.data(), "/UsFrench/MessageData/Message.arc") ==
                             "FRENCH",
                 "TEXT: the game's French");
    ok &= expect(galaxy::mods::language_folder("RMGE01", 3u) == "UsFrench" &&
                     galaxy::mods::language_folder("RMGP01", 2u) == "EuGerman" &&
                     galaxy::mods::language_folder("RMGJ01", 0u) == "JpJapanese",
                 "language folders by region");
    {
        // The root's language folders: UsEnglish (holding EuGerman, not at the root), UsFrench; a file named
        // UsSpanish, Usb and StageData are none.
        const char strings[] = "UsEnglish\0EuGerman\0m.arc\0UsFrench\0UsSpanish\0Usb\0StageData\0";
        const std::size_t offsets[] = {0, 10, 19, 25, 34, 44, 48};
        std::vector<std::uint8_t> fst(8 * 12 + sizeof(strings), 0);
        put32(fst, 0, 0x01000000u);
        put32(fst, 8, 8u);
        const auto node = [&](std::size_t index, bool dir, std::size_t name, std::uint32_t next) {
            put32(fst, index * 12, (dir ? 0x01000000u : 0u) | static_cast<std::uint32_t>(offsets[name]));
            put32(fst, index * 12 + 8, next);
        };
        node(1, true, 0, 4);   // UsEnglish/ holds 2 and 3
        node(2, true, 1, 3);   // UsEnglish/EuGerman/
        node(3, false, 2, 4);  // UsEnglish/m.arc
        node(4, true, 3, 5);   // UsFrench/
        node(5, false, 4, 4);  // UsSpanish (a file)
        node(6, true, 5, 7);   // Usb/
        node(7, true, 6, 8);   // StageData/
        std::memcpy(fst.data() + 8 * 12, strings, sizeof(strings));
        const std::vector<std::string> found = galaxy::mods::language_folders(fst);
        ok &= expect(found == std::vector<std::string>{"UsEnglish", "UsFrench"}, "the root's language folders");
        ok &= expect(galaxy::mods::language_folders(std::vector<std::uint8_t>(5, 0)).empty(), "a short table has none");
    }
    fs::remove_all(root, ec);
    return ok;
}

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

int main() {
    const bool ok = xml() & disc() & riivolution() & layouts() & importing();
    std::cout << (ok ? "mods tests passed" : "mods tests FAILED") << '\n';
    return ok ? 0 : 1;
}
