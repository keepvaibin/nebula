// Achievements: the catalogue, unlocking once, keeping them across sessions, and writing them in the background
// (galaxy/achievements.h).
#include "galaxy/achievements.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

namespace {
int s_failures = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++s_failures;
    }
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}
}  // namespace

int main() {
    namespace ach = galaxy::achievements;
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "nebula_achievements_test.ini";
    std::filesystem::remove(path);
    ach::set_path_for_tests(path);

    expect(ach::list().size() == 34u, "thirty-four achievements");
    for (const ach::Info& info : ach::list()) {
        expect(!ach::unlocked(info.id), "every achievement starts locked");
        expect(ach::find(info.id) == &info, "each id is found, once");
    }
    for (std::size_t i = 1; i < ach::list().size(); ++i) {
        expect(ach::list()[i - 1].group <= ach::list()[i].group, "the catalogue is grouped: story, challenges, extras");
    }
    expect(!ach::unlock("nonsense"), "an unknown id does not unlock");
    expect(ach::unlock("comets"), "first unlock reports new");
    expect(!ach::unlock("comets"), "second unlock reports nothing new");
    expect(ach::unlocked("comets") && !ach::unlocked("hidden"), "only that one is unlocked");
    expect(ach::unlocked_date("hidden") == 0, "a locked one has no date");
    {
        const int date = ach::unlocked_date("comets");
        expect(date >= 20260101 && date % 100 >= 1 && date % 100 <= 31 && date / 100 % 100 >= 1 && date / 100 % 100 <= 12,
               "an unlocked one has its day");
    }

    ach::set_path_for_tests(path);  // a new session reads the file
    expect(ach::unlocked("comets"), "kept across sessions");
    expect(!ach::unlock("comets"), "still not new after a restart");
    expect(ach::unlock("lives") && ach::unlocked("lives"), "a second achievement");

    // The writer saves an unlock by itself, a moment later.
    expect(ach::unlock("bank"), "a third achievement");
    bool written = false;
    for (int i = 0; i < 100 && !written; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        written = read_file(path).find("bank=") != std::string::npos;
    }
    expect(written, "written in the background");

    // Every one at once (a save that meets them all), then a flush: all in the file.
    for (const ach::Info& info : ach::list()) (void)ach::unlock(info.id);
    ach::flush();
    {
        const std::string text = read_file(path);
        std::size_t lines = 0;
        for (const char c : text) lines += c == '\n' ? 1u : 0u;
        expect(lines == ach::list().size(), "a flush writes every unlock");
    }
    ach::set_path_for_tests(path);
    expect(ach::unlocked("no_miss_120") && ach::unlocked("coins999"), "read back after the batch");

    // An entry whose day cannot be read is unlocked all the same.
    {
        std::ofstream out(path, std::ios::trunc);
        out << "comets=yesterday\n";
    }
    ach::set_path_for_tests(path);
    expect(ach::unlocked("comets") && ach::unlocked_date("comets") == 1, "unlocked on an unreadable day: 1");

    std::filesystem::remove(path);
    if (s_failures == 0) std::printf("achievements: all passed\n");
    return s_failures == 0 ? 0 : 1;
}
