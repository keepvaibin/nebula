// Achievements: the catalogue, unlocking once, and keeping them across sessions (galaxy/achievements.h).
#include "galaxy/achievements.h"

#include <cstdio>
#include <filesystem>
#include <string>

namespace {
int s_failures = 0;
void expect(bool ok, const char* what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++s_failures;
    }
}
}  // namespace

int main() {
    namespace ach = galaxy::achievements;
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "nebula_achievements_test.ini";
    std::filesystem::remove(path);
    ach::set_path_for_tests(path);

    expect(ach::list().size() == 30u, "thirty achievements");
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

    ach::set_path_for_tests(path);  // a new session reads the file
    expect(ach::unlocked("comets"), "kept across sessions");
    expect(!ach::unlock("comets"), "still not new after a restart");
    expect(ach::unlock("lives") && ach::unlocked("lives"), "a second achievement");

    std::filesystem::remove(path);
    if (s_failures == 0) std::printf("achievements: all passed\n");
    return s_failures == 0 ? 0 : 1;
}
