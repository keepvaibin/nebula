// Tests for galaxy/whole_lines.h: many threads writing lines of many parts at once to std::cerr and std::clog, and
// lines made with Line, reach the output whole; a thread's unfinished line is written at its end.
//
// whole_lines_tests [--without]: --without leaves the standard streams as they are, to show the fault the buffer
// fixes (lines broken by other threads' writes); it then exits 1 when any line is broken, as the test does.
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS  // freopen: the test points stderr at its file and back
#endif
#include "galaxy/whole_lines.h"
#include "galaxy/test_temp.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int kThreads = 8;
constexpr int kLines = 3000;
constexpr int kParts = 12;

// "w<thread> <line> p0 p1 ... p11" and nothing else: a broken line has another thread's parts in it, or misses its own.
bool whole(const std::string& line, std::set<std::pair<int, int>>& seen) {
    std::istringstream in(line);
    std::string head;
    int thread = -1, number = -1;
    if (!(in >> head >> number) || head.size() < 2 || head[0] != 'w') {
        return false;
    }
    thread = std::atoi(head.c_str() + 1);
    for (int p = 0; p < kParts; ++p) {
        std::string part;
        if (!(in >> part) || part != "p" + std::to_string(p)) {
            return false;
        }
    }
    std::string rest;
    if (in >> rest) {
        return false;
    }
    return seen.insert({thread, number}).second;
}

}  // namespace

int main(int argc, char** argv) {
    const bool without = argc > 1 && std::strcmp(argv[1], "--without") == 0;
    const std::filesystem::path path = galaxy::test_temp_path("whole-lines-test.txt");
    // The lines go to stderr, now the file; the verdict to stdout.
    std::FILE* const console = stdout;
    if (std::freopen(path.string().c_str(), "wb", stderr) == nullptr) {
        std::fprintf(console, "FAILED: stderr could not go to %s\n", path.string().c_str());
        return 1;
    }
    if (!without) {
        galaxy::whole_lines::install();
    }
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([t] {
            for (int n = 0; n < kLines; ++n) {
                if (n % 3 == 2) {
                    galaxy::whole_lines::Line line;
                    line.add("w%d %d", t, n);
                    for (int p = 0; p < kParts; ++p) line.add(" p%d", p);
                    line.write(stderr);
                } else {
                    std::ostream& out = n % 3 == 0 ? std::cerr : std::clog;
                    out << 'w' << t << ' ' << n;
                    for (int p = 0; p < kParts; ++p) out << " p" << p;
                    out << '\n';
                }
            }
            // An unfinished line: written whole when the thread ends.
            std::cerr << "w" << t << " " << kLines;
            for (int p = 0; p < kParts; ++p) std::cerr << " p" << p;
        });
    }
    for (std::thread& thread : threads) thread.join();
    galaxy::whole_lines::flush();
    std::ifstream in(path, std::ios::binary);
    std::set<std::pair<int, int>> seen;
    int broken = 0;
    std::string line;
    while (std::getline(in, line)) {
        if (!whole(line, seen)) {
            if (broken < 3) std::fprintf(console, "broken line: %.160s\n", line.c_str());
            ++broken;
        }
    }
    in.close();
    const std::size_t want = static_cast<std::size_t>(kThreads) * (kLines + 1);
    std::error_code ignored;
    std::freopen(
#if defined(_WIN32)
        "NUL",
#else
        "/dev/null",
#endif
        "wb", stderr);
    std::filesystem::remove(path, ignored);
    if (broken != 0 || seen.size() != want) {
        std::fprintf(console, "FAILED%s: %d broken lines, %zu of %zu lines whole\n", without ? " (without the buffer)" : "",
                     broken, seen.size(), want);
        return 1;
    }
    std::fprintf(console, "whole lines: %zu lines from %d threads, every one whole\n", seen.size(), kThreads);
    return 0;
}
