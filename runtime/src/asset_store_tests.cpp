// Tests for galaxy/asset_store.h on a small store: paths in either case, "." and "..", '/' returning to the root, a folder's
// entries in order, 64-bit positions, malformed stores refused.
#include "galaxy/asset_store.h"
#include "galaxy/test_temp.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

template <typename T>
void put(std::vector<std::uint8_t>& out, T value) {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(T));
}

std::uint64_t path_hash(const std::string& path) {
    std::uint64_t hash = 0xCBF29CE484222325ull;
    for (const char c : path) {
        const char lower = c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        hash = (hash ^ static_cast<std::uint8_t>(lower)) * 0x100000001B3ull;
    }
    return hash;
}

struct Item {
    std::string path;
    std::string name;
    std::uint64_t position;
    std::uint64_t size;
    std::uint32_t parent;
    bool folder;
};

std::vector<std::uint8_t> store_of(const std::vector<Item>& items) {
    std::vector<std::uint8_t> out(64);
    std::string names(1, '\0');
    const std::uint64_t nodes = out.size();
    for (const Item& item : items) {
        put<std::uint64_t>(out, item.folder ? 0u : item.position);
        put<std::uint64_t>(out, item.folder ? 0u : item.size);
        put<std::uint32_t>(out, item.name.empty() ? 0u : static_cast<std::uint32_t>(names.size()));
        put<std::uint32_t>(out, item.parent);
        put<std::uint32_t>(out, item.folder ? 1u : 0u);
        put<std::uint32_t>(out, item.folder ? static_cast<std::uint32_t>(item.position) : 0u);
        put<std::uint32_t>(out, item.folder ? static_cast<std::uint32_t>(item.size) : 0u);
        put<std::uint32_t>(out, 0u);
        if (!item.name.empty()) {
            names += item.name;
            names.push_back('\0');
        }
    }
    std::vector<std::pair<std::uint64_t, std::uint32_t>> hashes;
    for (std::uint32_t i = 0; i < items.size(); i++) {
        hashes.emplace_back(path_hash(items[i].path), i);
    }
    std::sort(hashes.begin(), hashes.end());
    const std::uint64_t hashes_at = out.size();
    for (const auto& [hash, node] : hashes) {
        put<std::uint64_t>(out, hash);
        put<std::uint32_t>(out, node);
        put<std::uint32_t>(out, 0u);
    }
    const std::uint64_t names_at = out.size();
    out.insert(out.end(), names.begin(), names.end());
    std::vector<std::uint8_t> header;
    header.insert(header.end(), galaxy::assets::kStoreMagic, galaxy::assets::kStoreMagic + 8);
    put<std::uint32_t>(header, galaxy::assets::kStoreVersion);
    put<std::uint32_t>(header, static_cast<std::uint32_t>(items.size()));
    put<std::uint64_t>(header, nodes);
    put<std::uint64_t>(header, hashes_at);
    put<std::uint64_t>(header, names_at);
    put<std::uint64_t>(header, names.size());
    const char id[8] = {'R', 'M', 'G', 'E', '0', '1', 0, 2};
    header.insert(header.end(), id, id + 8);
    std::memcpy(out.data(), header.data(), header.size());
    return out;
}

}  // namespace

int main() {
    using galaxy::assets::AssetStore;
    bool passed = true;

    const std::vector<Item> items = {
        {"", "", 1, 3, 0, true},
        {"opening.bnr", "opening.bnr", 0x100, 64, 0, false},
        {"StageData", "StageData", 4, 2, 0, true},
        {"sub", "sub", 6, 1, 0, true},
        {"StageData/EggStarGalaxy", "EggStarGalaxy", 7, 1, 2, true},
        {"StageData/Readme.txt", "Readme.txt", 0x3000, 5, 2, false},
        {"sub/readme.txt", "readme.txt", 0x1'0000'4000ull, 7, 3, false},
        {"StageData/EggStarGalaxy/EggStarGalaxy.arc", "EggStarGalaxy.arc", 0x2000, 1000, 4, false},
    };
    const std::vector<std::uint8_t> bytes = store_of(items);
    std::vector<std::uint8_t> previous = bytes;
    const std::uint32_t previous_version = 3u;
    std::memcpy(previous.data() + 8, &previous_version, sizeof(previous_version));
    AssetStore old_table;
    passed &= expect(!old_table.load(previous.data(), previous.size()) && !old_table.find("/"),
                     "a store with the old model and clip layouts needs importing again");
    AssetStore table;
    passed &= expect(table.load(bytes.data(), bytes.size()), "the store loads");
    passed &= expect(table.game_id() == "RMGE01" && table.revision() == 2, "the game id and revision");

    using galaxy::assets::store_path;
    for (const auto& [disc, store] : {std::pair{"ObjectData/Kuribo.ARC/kuribo.bdl", "ObjectData/Kuribo/kuribo.bdl"},
                                      std::pair{"/StageData/EggStarGalaxy.arc", "/StageData/EggStarGalaxy"},
                                      std::pair{"/StageData/EggStarGalaxy/EggStarGalaxyScenario.arc/x.bcsv",
                                                "/StageData/EggStarGalaxyScenario/x.bcsv"},
                                      std::pair{"stagedata/A/jmp/b", "stagedata/A/jmp/b"}, std::pair{"/AudioRes/SMR.szs", "/AudioRes/SMR"},
                                      std::pair{".arc", ".arc"}, std::pair{"/", "/"}, std::pair{"", ""}}) {
        passed &= expect(store_path(disc) == store && store_path(store) == store, disc);
    }

    auto found = [&table](const char* path) { return table.find(path) ? table.find(path)->index : -1; };
    passed &= expect(found("/StageData/EggStarGalaxy/EggStarGalaxy.arc") == 7, "an absolute path");
    passed &= expect(found("stagedata/eggstargalaxy/EGGSTARGALAXY.ARC") == 7, "letters in either case, from the root");
    passed &= expect(found("/StageData") == 2 && found("/StageData/") == 2, "a folder, with or without its slash");
    passed &= expect(found("") == 0 && found("/") == 0 && found(".") == 0 && found("..") == 0, "the root");
    passed &= expect(found("/StageData/./Readme.txt") == 5 && found("/StageData/EggStarGalaxy/../Readme.txt") == 5,
                     "'.' and '..' steps");
    passed &= expect(found("/StageData//opening.bnr") == 1, "'/' at a name's start returns to the root");
    passed &= expect(found("/sub/readme.txt") == 6 && found("/StageData/readme.txt") == 5,
                     "a name is looked up in its own folder");
    passed &= expect(found("/StageDat") == -1 && found("/StageData/Readme.tx") == -1 && found("/nothing") == -1,
                     "a name matches whole");
    passed &= expect(found("/opening.bnr/x") == -1 && found("/StageData/Readme.txt/") == -1, "a file is no folder");

    const auto file = table.find("/sub/readme.txt");
    passed &= expect(file && !file->folder && file->length == 7 && file->position == 0x100004000ull &&
                         std::strcmp(file->name, "readme.txt") == 0,
                     "a file's length, its position past 4 GB and its name");

    std::string listing;
    for (std::uint32_t i = 0; auto entry = table.child(2, i); i++) {
        listing += entry->name;
        listing += entry->folder ? "/ " : " ";
    }
    passed &= expect(listing == "EggStarGalaxy/ Readme.txt ", "a folder's entries in order");
    passed &= expect(!table.child(1, 0) && !table.child(99, 0) && !table.entry(8) && !table.entry(-1),
                     "a file has no entries; indices past the store have none");

    // Malformed: not a store, a count past the data, an unterminated name table.
    std::vector<std::uint8_t> bad = bytes;
    bad[0] = 'X';
    passed &= expect(!table.load(bad.data(), bad.size()) && !table.find("/"), "another file is refused");
    bad = bytes;
    bad[14] = 0xFF;
    passed &= expect(!table.load(bad.data(), bad.size()), "a count past the data is refused");
    bad = bytes;
    bad.back() = 'x';
    passed &= expect(!table.load(bad.data(), bad.size()), "names without an end are refused");
    passed &= expect(table.load(bytes.data(), bytes.size()) && table.language_folders().empty(), "no language folders");
    std::uint8_t read[4]{};
    passed &= expect(table.read(0, 4, read) && std::memcmp(read, "NBST", 4) == 0 && !table.read(bytes.size(), 1, read),
                     "reads inside the store only");
    const std::uint8_t* mapped = table.map(0, 4);
    passed &= expect(mapped != nullptr && std::memcmp(mapped, "NBST", 4) == 0 && table.map(bytes.size(), 1) == nullptr &&
                         table.map(bytes.size(), 0) != nullptr,
                     "a store in memory maps inside itself only");
    // From a file: every byte where the store says it is, and the mapping gone with the store.
    const auto path = galaxy::test_temp_path("nebula_asset_store_test.pak");
    {
        std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        AssetStore from_file;
        passed &= expect(from_file.open(path), "the store opens from a file");
        mapped = from_file.map(0, bytes.size());
        passed &= expect(mapped != nullptr && std::memcmp(mapped, bytes.data(), bytes.size()) == 0, "the file's bytes are mapped");
        passed &= expect(from_file.map(bytes.size() - 1, 2) == nullptr, "a file maps inside itself only");
    }
    std::filesystem::remove(path);

    // A language pack: a mod's EuGerman on a US disc is a language the setting offers, and a file the pack leaves out
    // comes from the disc's own English.
    {
        const std::vector<Item> disc = {
            {"", "", 1, 1, 0, true},
            {"UsEnglish", "UsEnglish", 2, 2, 0, true},
            {"UsEnglish/LayoutData", "LayoutData", 4, 1, 1, true},
            {"UsEnglish/MessageData", "MessageData", 5, 1, 1, true},
            {"UsEnglish/LayoutData/Menu.arc", "Menu.arc", 0x2000, 10, 2, false},
            {"UsEnglish/MessageData/Message.arc", "Message.arc", 0x3000, 20, 3, false},
        };
        const std::vector<Item> pack = {
            {"", "", 1, 1, 0, true},
            {"EuGerman", "EuGerman", 2, 1, 0, true},
            {"EuGerman/MessageData", "MessageData", 3, 1, 1, true},
            {"EuGerman/MessageData/Message.arc", "Message.arc", 0x100, 30, 2, false},
        };
        const std::vector<std::uint8_t> disc_bytes = store_of(disc);
        const std::vector<std::uint8_t> pack_bytes = store_of(pack);
        const auto pack_path = galaxy::test_temp_path("nebula_asset_store_pack.pak");
        std::ofstream(pack_path, std::ios::binary)
            .write(reinterpret_cast<const char*>(pack_bytes.data()), static_cast<std::streamsize>(pack_bytes.size()));
        {
            // The store maps the pack's file until it goes.
            AssetStore game;
            passed &= expect(game.load(disc_bytes.data(), disc_bytes.size()) && game.attach_overlay(pack_path), "a disc and a language pack");
            passed &= expect(game.language_folders() == std::vector<std::string>{"UsEnglish", "EuGerman"},
                             "the pack's language is listed after the disc's");
            const auto message = game.find("EuGerman/MessageData/Message.arc");
            passed &= expect(message && message->length == 30, "the pack's own text");
            const auto menu = game.find("/EuGerman/LayoutData/Menu.arc");
            passed &= expect(menu && menu->length == 10, "a file the pack leaves out is the disc's English one");
            passed &= expect(!game.find("EuGerman/LayoutData/None.arc") && !game.find("UsEnglish/None.arc") &&
                                 !game.find("EuFrench/LayoutData/Menu.arc"),
                             "only files the disc has, and only for a language a mod adds");
        }
        std::filesystem::remove(pack_path);
    }

    if (!passed) {
        return 1;
    }
    std::cout << "Asset store tests passed\n";
    return 0;
}
