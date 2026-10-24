// Stats: counting, names, keeping them across sessions, the background write (galaxy/stats.h).
#include "galaxy/stats.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
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

std::string read(const std::filesystem::path& path) {
    std::ifstream in(path);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}
}  // namespace

int main() {
    namespace st = galaxy::stats;
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "nebula_stats_test.ini";
    std::filesystem::remove(path);
    st::set_path_for_tests(path);

    expect(st::valid_name("jumps") && st::valid_name("play_seconds") && st::valid_name("a1"), "plain names");
    expect(!st::valid_name("") && !st::valid_name("Jumps") && !st::valid_name("a b") && !st::valid_name("a=b") &&
               !st::valid_name(std::string(33, 'a')),
           "bad names");
    expect(st::get("jumps") == 0u, "a counter starts at zero");
    st::add("jumps", 3);
    st::add("jumps", 4);
    st::add("coins", 10);
    st::add("bad name", 5);
    st::add("spins", 0);
    expect(st::get("jumps") == 7u && st::get("coins") == 10u, "counts add up");
    expect(st::get("bad name") == 0u && st::get("spins") == 0u, "bad names and zero deltas leave nothing");

    // The writer saves it on its own a moment later.
    bool written = false;
    for (int i = 0; i < 100 && !written; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        written = read(path) == "coins=10\njumps=7\n";
    }
    expect(written, "the background writer saves the counts");

    st::add("jumps", UINT64_MAX);
    expect(st::get("jumps") == UINT64_MAX, "a counter stops at its maximum");
    st::flush();
    expect(read(path) == "coins=10\njumps=18446744073709551615\n", "flush writes at once");

    st::set_path_for_tests(path);  // a new session reads the file
    expect(st::get("coins") == 10u && st::get("jumps") == UINT64_MAX, "kept across sessions");

    {
        std::ofstream out(path, std::ios::trunc);
        out << "coins=12\nnot a line\nBAD=4\nspins=x\ndeaths=2\n";
    }
    st::set_path_for_tests(path);
    expect(st::get("coins") == 12u && st::get("deaths") == 2u && st::get("spins") == 0u, "a damaged file keeps its good lines");

    for (int i = 0; i < 80; ++i) st::add("c" + std::to_string(i), 1);
    expect(st::get("c70") == 0u && st::get("c10") == 1u, "at most 64 counters");

    st::set_path_for_tests(path);
    std::filesystem::remove(path);
    if (s_failures == 0) std::printf("stats: all passed\n");
    return s_failures == 0 ? 0 : 1;
}
