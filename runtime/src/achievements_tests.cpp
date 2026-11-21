// Achievements: the catalogue, unlocking once, keeping them across sessions, and writing them in the background
// (galaxy/achievements.h).
#include "galaxy/achievements.h"
#include "galaxy/test_temp.h"
#include "galaxy/test_wait.h"

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
    const std::filesystem::path path = galaxy::test_temp_path("nebula_achievements_test.ini");
    std::filesystem::remove(path);
    ach::set_path_for_tests(path);

    expect(ach::list().size() == 35u, "thirty-five achievements");
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

    // The writer saves an unlock by itself, a moment later. The test waits for the file itself (on a loaded machine the
    // writer's moment can come seconds late, and 2 s failed there, 11-18); the deadline only reports that it never came.
    expect(ach::unlock("bank"), "a third achievement");
    const bool written = galaxy::test_wait([&] { return read_file(path).find("bank=") != std::string::npos; },
                                           std::chrono::milliseconds(20));
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

    // A mod's achievements: keys under "mod:", kept in the same file beside the game's own.
    expect(!ach::unlock("mod:example/first"), "unlock() takes only the game's own ids");
    expect(!ach::unlock_mod("example/first"), "a mod's key needs the mod: prefix");
    expect(!ach::unlock_mod("mod:"), "an empty mod key is refused");
    expect(!ach::unlock_mod("mod:a=b") && !ach::unlock_mod("mod:a\nb") && !ach::unlock_mod("mod:a\rb"),
           "a key that would read back as another line is refused");
    expect(ach::unlock_mod("mod:example/first"), "a mod's achievement unlocks");
    expect(!ach::unlock_mod("mod:example/first"), "and only once");
    expect(ach::unlocked("mod:example/first") && ach::unlocked_date("mod:example/first") >= 20260101, "with its day");
    ach::set_path_for_tests(path);
    expect(ach::unlocked("mod:example/first") && ach::unlocked("comets"), "both kept across sessions");
    expect(!ach::unlocked("mod:example/second"), "another of the mod's is still locked");

    // Save export and import (P-119): another install's unlocks merged in, the earlier time kept.
    {
        std::ofstream out(path, std::ios::trunc);
        out << "comets=2026-03-01T00:00:00Z\n";
    }
    ach::set_path_for_tests(path);
    const unsigned merged = ach::merge("comets=2025-01-01T00:00:00Z\ncoins999=2026-05-05T00:00:00Z\r\n"
                                       "not_an_id=2026-01-01T00:00:00Z\nmod:other/one=2026-02-02T00:00:00Z\nno equals\n");
    expect(merged == 3u, "three unlocks taken: an earlier time, a new one, a mod's");
    expect(ach::unlocked("coins999") && ach::unlocked("mod:other/one") && !ach::unlocked("not_an_id"), "the catalogue's and mods' only");
    expect(ach::text().find("comets=2025-01-01T00:00:00Z\n") != std::string::npos, "the earlier time kept");
    expect(ach::merge(ach::text()) == 0u, "an install's own unlocks again change nothing");
    ach::flush();
    ach::set_path_for_tests(path);
    expect(ach::unlocked("coins999") && ach::unlocked_date("comets") != 0, "written to the file");

    // Handed over by another process: merged at the first read, then its file removed.
    {
        std::ofstream out(path.parent_path() / std::string(ach::kImportFileName), std::ios::trunc);
        out << "gateway=2026-06-06T00:00:00Z\n";
    }
    ach::set_path_for_tests(path);
    expect(ach::unlocked("gateway"), "a handed-over import is merged");
    expect(!std::filesystem::exists(path.parent_path() / std::string(ach::kImportFileName)), "and its file removed");
    ach::set_path_for_tests(path);
    expect(ach::unlocked("gateway"), "and kept in the file");

    std::filesystem::remove(path);
    if (s_failures == 0) std::printf("achievements: all passed\n");
    return s_failures == 0 ? 0 : 1;
}
