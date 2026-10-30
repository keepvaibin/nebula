// Tests for galaxy/disc_files.h on a small table laid out as a Wii disc's: paths in either case, "." and "..", '/'
// returning to the root, a folder's entries in order, positions in 4-byte units, malformed tables refused.
#include "galaxy/disc_files.h"

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

struct Builder {
    struct Item {
        bool folder;
        std::string name;
        std::uint32_t parent_or_position;
        std::uint32_t next_or_length;
    };
    std::vector<Item> items;

    std::vector<std::uint8_t> bytes() const {
        std::vector<std::uint8_t> out;
        std::string names;
        auto put = [&out](std::uint32_t value) {
            for (int shift = 24; shift >= 0; shift -= 8) {
                out.push_back(static_cast<std::uint8_t>(value >> shift));
            }
        };
        for (const Item& item : items) {
            const auto name = static_cast<std::uint32_t>(names.size());
            names += item.name;
            names.push_back('\0');
            put((item.folder ? 0x01000000u : 0u) | name);
            put(item.parent_or_position);
            put(item.next_or_length);
        }
        out.insert(out.end(), names.begin(), names.end());
        return out;
    }
};

}  // namespace

int main() {
    using galaxy::disc::FileTable;
    bool passed = true;

    // 0 /            (8 entries)
    // 1 opening.bnr  at 0x100 (word 0x40), 64 bytes
    // 2 StageData/   (to 6)
    // 3   EggStarGalaxy/ (to 5)
    // 4     EggStarGalaxy.arc at 0x2000, 1000 bytes
    // 5   Readme.txt at 0x3000, 5 bytes
    // 6 sub/         (to 8)
    // 7   readme.txt at 0x4000, 7 bytes
    Builder b;
    b.items = {{true, "", 0, 8},
               {false, "opening.bnr", 0x40, 64},
               {true, "StageData", 0, 6},
               {true, "EggStarGalaxy", 2, 5},
               {false, "EggStarGalaxy.arc", 0x800, 1000},
               {false, "Readme.txt", 0xC00, 5},
               {true, "sub", 0, 8},
               {false, "readme.txt", 0x1000, 7}};
    const std::vector<std::uint8_t> fst = b.bytes();
    FileTable table;
    passed &= expect(table.load(fst.data(), fst.size()), "the table loads");

    auto found = [&table](const char* path) { return table.find(path) ? table.find(path)->index : -1; };
    passed &= expect(found("/StageData/EggStarGalaxy/EggStarGalaxy.arc") == 4, "an absolute path");
    passed &= expect(found("stagedata/eggstargalaxy/EGGSTARGALAXY.ARC") == 4, "letters in either case, from the root");
    passed &= expect(found("/StageData") == 2 && found("/StageData/") == 2, "a folder, with or without its slash");
    passed &= expect(found("") == 0 && found("/") == 0 && found(".") == 0 && found("..") == 0, "the root");
    passed &= expect(found("/StageData/./Readme.txt") == 5 && found("/StageData/EggStarGalaxy/../Readme.txt") == 5,
                     "'.' and '..' steps");
    passed &= expect(found("/StageData//opening.bnr") == 1, "'/' at a name's start returns to the root");
    passed &= expect(found("/sub/readme.txt") == 7 && found("/StageData/readme.txt") == 5,
                     "a name is looked up in its own folder");
    passed &= expect(found("/StageDat") == -1 && found("/StageData/Readme.tx") == -1 && found("/nothing") == -1,
                     "a name matches whole");
    passed &= expect(found("/opening.bnr/x") == -1 && found("/StageData/Readme.txt/") == -1,
                     "a file is no folder");

    const auto file = table.find("/StageData/EggStarGalaxy/EggStarGalaxy.arc");
    passed &= expect(file && !file->folder && file->length == 1000 && file->position == 0x2000 &&
                         std::strcmp(file->name, "EggStarGalaxy.arc") == 0,
                     "a file's length, its position in bytes and its name");

    std::string listing;
    for (std::uint32_t i = 0; auto entry = table.child(2, i); i++) {
        listing += entry->name;
        listing += entry->folder ? "/ " : " ";
    }
    passed &= expect(listing == "EggStarGalaxy/ Readme.txt ", "a folder's entries in order, its subfolders' skipped");
    passed &= expect(!table.child(1, 0) && !table.child(99, 0) && !table.entry(8) && !table.entry(-1),
                     "a file has no entries; indices past the table have none");

    // Malformed: an entry count past the data, a folder running past the table, a name outside the names.
    std::vector<std::uint8_t> bad = fst;
    bad[11] = 200;
    passed &= expect(!table.load(bad.data(), bad.size()) && !table.find("/"), "a count past the data is refused");
    bad = fst;
    bad[2 * 12 + 11] = 9;
    passed &= expect(!table.load(bad.data(), bad.size()), "a folder past the table is refused");
    bad = fst;
    bad[1 * 12 + 3] = 0xFF;
    passed &= expect(!table.load(bad.data(), bad.size()), "a name outside the names is refused");

    if (!passed) {
        return 1;
    }
    std::cout << "Disc file table tests passed\n";
    return 0;
}
