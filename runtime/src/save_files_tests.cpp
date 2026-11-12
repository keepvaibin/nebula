// Tests for galaxy/save_files.h: a save written and read back whole, replaced by a shorter one, a buffer too small,
// missing files, deletes, and names that would leave the save folder.
#include "galaxy/save_files.h"
#include "galaxy/test_temp.h"

#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

bool expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
    }
    return condition;
}

}  // namespace

int main() {
    using namespace galaxy::saves;
    namespace fs = std::filesystem;
    bool passed = true;
    const fs::path root = galaxy::test_temp_path("galaxy_save_files_tests");
    fs::remove_all(root);  // left by an earlier run that had this process id
    // The save folder does not exist until the first save.
    const fs::path folder = root / "title" / "data";

    constexpr std::uint32_t kSaveSize = 4096;
    std::array<unsigned char, kSaveSize> save{};
    for (std::uint32_t i = 0; i < kSaveSize; i++) {
        save[i] = static_cast<unsigned char>(i * 37u);  // zero bytes and every value, as a binary save holds
    }
    constexpr std::uint32_t kBufferSize = 8192;
    std::array<unsigned char, kBufferSize> buffer{};
    std::uint32_t length = 0;

    passed &= expect(read(folder, "GameData.bin", buffer.data(), kBufferSize, &length) == kNoFile,
                     "reading before the first save finds no file");
    passed &= expect(write(folder, "GameData.bin", save.data(), kSaveSize) == kDone,
                     "the first save creates the folder and the file");
    passed &= expect(read(folder, "GameData.bin", buffer.data(), kBufferSize, &length) == kDone &&
                         length == kSaveSize && std::memcmp(buffer.data(), save.data(), kSaveSize) == 0,
                     "the save reads back whole");

    passed &= expect(write(folder, "GameData.bin", save.data(), 100) == kDone &&
                         read(folder, "GameData.bin", buffer.data(), kBufferSize, &length) == kDone && length == 100,
                     "a shorter save replaces the whole file");
    std::size_t files = 0;
    for (const auto& entry : fs::directory_iterator(folder)) {
        // The game's own copies of its last saves (GameData.bin.1, .2) are kept on purpose.
        const std::string name = entry.path().filename().string();
        files += name == "GameData.bin.1" || name == "GameData.bin.2" ? 0u : 1u;
    }
    passed &= expect(files == 1, "a save leaves no temporary file beside it");

    length = 0;
    passed &= expect(read(folder, "GameData.bin", buffer.data(), 99, &length) == kTooLarge && length == 100,
                     "a buffer too small is refused with the file's length");

    for (const char* name : {"", ".", "..", "../GameData.bin", "a/b", "a\\b", "C:GameData.bin"}) {
        passed &= expect(write(folder, name, save.data(), 16) == kFailed && read(folder, name, buffer.data(),
                         kBufferSize, &length) == kFailed && remove(folder, name) == kFailed,
                         "a name that is not a plain file name is refused");
    }
    passed &= expect(!fs::exists(root / "title" / "GameData.bin"), "nothing is written outside the save folder");

    passed &= expect(remove(folder, "GameData.bin") == kDone, "a save is deleted");
    passed &= expect(remove(folder, "GameData.bin") == kNoFile &&
                         read(folder, "GameData.bin", buffer.data(), kBufferSize, &length) == kNoFile,
                     "a deleted save is gone");

    // Mods' global files (services-save 7): "luma/<plain name>" only, and written without backups.
    passed &= expect(mod_file_name("luma/gold-flower.bin") == "gold-flower.bin", "a mod's file name is its plain name");
    for (const char* name : {"luma/", "luma", "luma/../Nebula.sav", "luma/a/b.bin", "Luma/a.bin", "lumax/a.bin",
                             "GameData.bin", "luma/.."}) {
        passed &= expect(mod_file_name(name).empty(), name);
    }
    const fs::path mods = folder / std::string(kModFolder);
    fs::create_directories(mods);
    passed &= expect(write(mods, "m.bin", save.data(), 16, Backups::kNone) == kDone &&
                         write(mods, "m.bin", save.data() + 16, 8, Backups::kNone) == kDone,
                     "a mod's file is written twice");
    passed &= expect(read(mods, "m.bin", buffer.data(), kBufferSize, &length) == kDone && length == 8 &&
                         std::memcmp(buffer.data(), save.data() + 16, 8) == 0,
                     "the second write is read back whole");
    passed &= expect(!fs::exists(mods / "m.bin.1") && !fs::exists(mods / "m.bin.2"), "no backups beside a mod's file");

    std::error_code error;
    fs::remove_all(root, error);
    if (!passed) {
        return 1;
    }
    std::cout << "Save file tests passed\n";
    return 0;
}
