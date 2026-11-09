// D-07: an older install's data under the current layout, laid out in a temporary folder: a game.pak of an older store
// version, and the save of the versions before Nebula.sav (GameData.bin in title/00010000/<the code in hex>/data).
// The old store is refused as it is (the importer writes the current one; nothing converts it in place). The old save
// is read by name and left as it was while Nebula.sav is written in the game's own folder with its backups; a second
// run that only reads changes no file.
#include "galaxy/asset_store.h"
#include "galaxy/save_files.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

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

// The smallest store that loads: a root folder with no entries, of the given version.
std::vector<std::uint8_t> store_with(std::uint32_t version) {
    std::vector<std::uint8_t> out(64);
    const std::uint64_t nodes_at = out.size();
    put<std::uint64_t>(out, 0u);  // position
    put<std::uint64_t>(out, 0u);  // size
    put<std::uint32_t>(out, 0u);  // name
    put<std::uint32_t>(out, 0u);  // parent
    put<std::uint32_t>(out, 1u);  // flags: a folder
    put<std::uint32_t>(out, 1u);  // first child
    put<std::uint32_t>(out, 0u);  // children
    put<std::uint32_t>(out, 0u);  // reserved
    const std::uint64_t hashes_at = out.size();
    put<std::uint64_t>(out, 0xCBF29CE484222325ull);  // the hash of the root's empty path
    put<std::uint32_t>(out, 0u);
    put<std::uint32_t>(out, 0u);
    const std::uint64_t names_at = out.size();
    out.push_back(0u);  // the name table: one terminator
    std::vector<std::uint8_t> header;
    header.insert(header.end(), galaxy::assets::kStoreMagic, galaxy::assets::kStoreMagic + 8);
    put<std::uint32_t>(header, version);
    put<std::uint32_t>(header, 1u);  // one node
    put<std::uint64_t>(header, nodes_at);
    put<std::uint64_t>(header, hashes_at);
    put<std::uint64_t>(header, names_at);
    put<std::uint64_t>(header, 1u);
    const char id[8] = {'R', 'M', 'G', 'E', '0', '1', 0, 2};
    header.insert(header.end(), id, id + 8);
    std::memcpy(out.data(), header.data(), header.size());
    return out;
}

std::vector<std::uint8_t> pattern(std::size_t size, unsigned multiplier) {
    std::vector<std::uint8_t> bytes(size);
    for (std::size_t i = 0; i < size; i++) {
        bytes[i] = static_cast<std::uint8_t>(i * multiplier + multiplier);
    }
    return bytes;
}

void write_file(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

std::vector<std::uint8_t> read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(in ? static_cast<std::streamoff>(in.tellg()) : 0));
    in.seekg(0);
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return bytes;
}

// Every file under a folder, by its path from the folder, with its bytes.
using Tree = std::map<std::string, std::vector<std::uint8_t>>;

Tree snapshot(const fs::path& folder) {
    Tree tree;
    std::error_code error;
    if (!fs::is_directory(folder, error)) {
        return tree;
    }
    for (const auto& entry : fs::recursive_directory_iterator(folder)) {
        if (entry.is_regular_file()) {
            tree[entry.path().lexically_relative(folder).generic_string()] = read_file(entry.path());
        }
    }
    return tree;
}

bool is_file(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
    return fs::is_regular_file(path) && read_file(path) == bytes;
}

}  // namespace

int main() {
    namespace saves = galaxy::saves;
    bool passed = true;
    const fs::path root = fs::temp_directory_path() /
        ("galaxy_install_migration_tests_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const fs::path content = root / "content";
    const fs::path save_root = root / "saves";

    // Where the game's saves are: its code, and the folder the earlier versions used (the code in hex).
    const saves::SaveFolders folders = saves::save_folders("RMGE");
    passed &= expect(folders.current == "/RMGE" && folders.earlier == "/title/00010000/524d4745/data",
                     "a game's save folders are its code and the earlier title folder");
    const fs::path old_folder = save_root / folders.earlier.substr(1);
    const fs::path new_folder = save_root / folders.current.substr(1);

    // An older install: a store of an older version, and the old save with its banner.
    const std::vector<std::uint8_t> old_store = store_with(3u);
    const std::vector<std::uint8_t> old_save = pattern(300, 37);
    const std::vector<std::uint8_t> old_banner = pattern(100, 11);
    write_file(content / "game.pak", old_store);
    write_file(old_folder / "GameData.bin", old_save);
    write_file(old_folder / "banner.bin", old_banner);
    const Tree old_saves = snapshot(old_folder);
    passed &= expect(old_saves.size() == 2, "the old install holds the save and its banner");

    // The store: the old one is refused and left alone; the importer's current one opens.
    const Tree old_content = snapshot(content);
    {
        galaxy::assets::AssetStore store;
        passed &= expect(!store.open(content / "game.pak"), "a store of an older version is refused");
    }
    passed &= expect(snapshot(content) == old_content, "refusing the old store changes no file");
    write_file(content / "game.pak", store_with(galaxy::assets::kStoreVersion));
    {
        galaxy::assets::AssetStore store;
        passed &= expect(store.open(content / "game.pak") && store.game_id() == "RMGE01", "the current store opens");
    }

    // The old save is read by name in the earlier folder; the game's own folder has no save yet.
    std::array<std::uint8_t, 512> buffer{};
    std::uint32_t length = 0;
    passed &= expect(saves::read(new_folder, "Nebula.sav", buffer.data(), 512, &length) == saves::kNoFile,
                     "before the first save there is no Nebula.sav");
    passed &= expect(saves::read(old_folder, "GameData.bin", buffer.data(), 512, &length) == saves::kDone &&
                         length == old_save.size() && std::memcmp(buffer.data(), old_save.data(), old_save.size()) == 0,
                     "the old save reads back whole");

    // The first save under the current layout: Nebula.sav in the game's folder, the old save left as it was.
    const std::vector<std::uint8_t> first = pattern(200, 5);
    const std::vector<std::uint8_t> second = pattern(220, 7);
    const std::vector<std::uint8_t> third = pattern(240, 9);
    passed &= expect(saves::write(new_folder, "Nebula.sav", first.data(), static_cast<std::uint32_t>(first.size())) == saves::kDone &&
                         is_file(new_folder / "Nebula.sav", first),
                     "the first save is Nebula.sav in the game's folder");
    passed &= expect(snapshot(old_folder) == old_saves, "the old save and banner are left as they were");
    passed &= expect(!fs::exists(new_folder / "Nebula.sav.1"), "a first save has no backup");

    // Later saves keep the last two beside it.
    passed &= expect(saves::write(new_folder, "Nebula.sav", second.data(), static_cast<std::uint32_t>(second.size())) == saves::kDone &&
                         is_file(new_folder / "Nebula.sav", second) && is_file(new_folder / "Nebula.sav.1", first),
                     "the next save keeps the one it replaced");
    passed &= expect(saves::write(new_folder, "Nebula.sav", third.data(), static_cast<std::uint32_t>(third.size())) == saves::kDone &&
                         is_file(new_folder / "Nebula.sav", third) && is_file(new_folder / "Nebula.sav.1", second) &&
                         is_file(new_folder / "Nebula.sav.2", first),
                     "and the last two stay");
    passed &= expect(snapshot(old_folder) == old_saves, "the old save is still as it was");

    // A second run that only reads (the store opened, Nebula.sav read) changes no file.
    const Tree settled = snapshot(root);
    {
        galaxy::assets::AssetStore store;
        passed &= expect(store.open(content / "game.pak"), "the store opens again");
    }
    passed &= expect(saves::read(new_folder, "Nebula.sav", buffer.data(), 512, &length) == saves::kDone && length == third.size() &&
                         std::memcmp(buffer.data(), third.data(), third.size()) == 0,
                     "Nebula.sav reads back whole");
    passed &= expect(snapshot(root) == settled, "a second run changes nothing");

    std::error_code error;
    fs::remove_all(root, error);
    if (!passed) {
        return 1;
    }
    std::cout << "Install migration tests passed\n";
    return 0;
}
