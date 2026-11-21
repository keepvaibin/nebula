// Tests for galaxy/save_files.h: a save written and read back whole, replaced by a shorter one, a buffer too small,
// missing files, deletes, and names that would leave the save folder.
#include "galaxy/save_files.h"
#include "galaxy/test_temp.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <time.h>
#endif

namespace {

// The calling thread's processor time so far.
std::chrono::nanoseconds thread_cpu_time() {
#if defined(_WIN32)
    FILETIME created, exited, kernel, user;
    GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user);
    const auto ticks = [](FILETIME t) { return (static_cast<std::uint64_t>(t.dwHighDateTime) << 32) | t.dwLowDateTime; };
    return std::chrono::nanoseconds((ticks(kernel) + ticks(user)) * 100u);
#else
    timespec t{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
    return std::chrono::seconds(t.tv_sec) + std::chrono::nanoseconds(t.tv_nsec);
#endif
}

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

    // Mods' global files (services/save.md 7): "luma/<plain name>" only, and written without backups.
    passed &= expect(mod_file_name("luma/gold-flower.bin") == "gold-flower.bin", "a mod's file name is its plain name");
    for (const char* name : {"luma/", "luma", "luma/../Nebula.sav", "luma/a/b.bin", "Luma/a.bin", "lumax/a.bin",
                             "GameData.bin", "luma/.."}) {
        passed &= expect(mod_file_name(name).empty(), name);
    }
    // save.game's offer (services/save.md 8): the replaced file's copy, "luma/replaced/<plain name>" only.
    passed &= expect(replaced_file_name("luma/replaced/2026-11-12-031500-file2.bin") == "2026-11-12-031500-file2.bin",
                     "a replaced file's name is its plain name");
    passed &= expect(mod_file_name("luma/replaced/a.bin").empty(), "a replaced file is not a mod's global file");
    for (const char* name : {"luma/replaced/", "luma/replaced", "luma/replaced/../a.bin", "luma/replaced/a/b.bin",
                             "luma/replacedx/a.bin", "luma/Replaced/a.bin", "luma/a.bin", "replaced/a.bin",
                             "luma/replaced/.."}) {
        passed &= expect(replaced_file_name(name).empty(), name);
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

    // A newer build's file (P-079): the same tag, a higher format word. Kept as <name>.v<format> before the first
    // write over it, and kept as it was through every later write (the two backups would rotate it out after two).
    const auto header = [](std::uint32_t format, unsigned char fill) {
        std::array<unsigned char, 32> bytes{};
        std::memcpy(bytes.data(), "NBSV", 4);
        for (int i = 0; i < 4; i++) bytes[4 + i] = static_cast<unsigned char>(format >> (8 * i));
        for (std::size_t i = 8; i < bytes.size(); i++) bytes[i] = fill;
        return bytes;
    };
    const auto newer = header(5, 0x55);
    const auto ours = header(4, 0x44);
    const fs::path newerFolder = root / "newer";
    passed &= expect(write(newerFolder, "Nebula.sav", newer.data(), 32) == kDone, "a newer build's save is on the drive");
    for (int i = 0; i < 3; i++) {
        passed &= expect(write(newerFolder, "Nebula.sav", ours.data(), 32) == kDone, "this build saves three times");
    }
    passed &= expect(read(newerFolder, "Nebula.sav.v5", buffer.data(), kBufferSize, &length) == kDone && length == 32 &&
                         std::memcmp(buffer.data(), newer.data(), 32) == 0,
                     "the newer save is kept whole as Nebula.sav.v5");
    passed &= expect(read(newerFolder, "Nebula.sav.2", buffer.data(), kBufferSize, &length) == kDone &&
                         std::memcmp(buffer.data(), ours.data(), 32) == 0,
                     "the backups rotate as before");
    const fs::path sameFolder = root / "same";
    passed &= expect(write(sameFolder, "Nebula.sav", ours.data(), 32) == kDone &&
                         write(sameFolder, "Nebula.sav", newer.data(), 32) == kDone &&
                         write(sameFolder, "GameData.bin", newer.data(), 32) == kDone &&
                         write(sameFolder, "GameData.bin", save.data(), 32) == kDone,
                     "an older format, and another tag, are written over");
    passed &= expect(!fs::exists(sameFolder / "Nebula.sav.v4") && !fs::exists(sameFolder / "GameData.bin.v5"),
                     "only a newer format of the same tag is kept aside");

    // A save request's wait sleeps (P-080: the game's thread spun a whole core while a slow save was written). The waiter
    // is given the wait to itself for 800 ms of wall time, long enough for a spinning wait to show in its CPU time. The
    // waiter's thread may start later than the worker's result: then its wait ends at once without sleeping (`slept`
    // false), and that attempt shows nothing about sleeping, so it is made again (a busy machine starts a thread seconds
    // late: N-1073). A wait that never sleeps fails every attempt.
    {
        StatusBoard board;
        constexpr std::int32_t kPending = 1;
        std::int32_t done = kDone;
        passed &= expect(!board.wait(&done, kPending), "a request already done is not waited for");
        bool slept = false;
        bool ended_with_result = false;
        std::chrono::nanoseconds used{};
        for (int attempt = 0; attempt < 10 && !slept; ++attempt) {
            std::int32_t status = kPending;
            std::int32_t seen = kPending;
            std::atomic<bool> entering{false};
            std::thread waiter([&] {
                const std::chrono::nanoseconds before = thread_cpu_time();
                entering.store(true);
                slept = board.wait(&status, kPending);
                seen = status;
                used = thread_cpu_time() - before;
            });
            while (!entering.load()) std::this_thread::yield();
            std::this_thread::sleep_for(std::chrono::milliseconds(800));
            board.store(&status, kFailed);
            waiter.join();
            ended_with_result = seen == kFailed;
        }
        passed &= expect(slept && ended_with_result, "the wait ends with the worker's result");
        passed &= expect(used < std::chrono::milliseconds(80), "the waiting thread sleeps (under 10% of the 800 ms)");
    }

    std::error_code error;
    fs::remove_all(root, error);
    if (!passed) {
        return 1;
    }
    std::cout << "Save file tests passed\n";
    return 0;
}
