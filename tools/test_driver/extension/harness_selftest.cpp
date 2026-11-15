// SPDX-License-Identifier: GPL-3.0-only
// The host half's after-step waits, without the game (tools/lumacore/tests/harness_wait_test.py builds and runs it).
//
// harness_selftest <plan> <frames> [[@<frame>:]<log line> ...]: reads the plan as the host does, then runs up to
// <frames> game frames or until the half ends the run. Each log line (a mod's fact is "game: [mod <id>] fact <key>
// <JSON>") is given before the first frame, or before game frame <frame> when it starts "@<frame>:". The facts go to
// the plan's out folder; the test reads them.
#include "harness.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

bool g_quit = false;

void log(const std::string& line) {
    std::fprintf(stderr, "%s\n", line.c_str());
}

void quit() {
    g_quit = true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: harness_selftest <plan> <frames> [<log line> ...]\n");
        return 2;
    }
    if (!harness::start(nullptr, argv[1], log, quit)) {
        return 1;
    }
    struct Line {
        long before;
        std::string text;
    };
    std::vector<Line> lines;
    for (int i = 3; i < argc; ++i) {
        std::string text = argv[i];
        long before = 1;
        const std::size_t colon = text.find(':');
        if (text[0] == '@' && colon != std::string::npos) {
            before = std::strtol(text.c_str() + 1, nullptr, 10);
            text = text.substr(colon + 1);
        }
        lines.push_back({before, text});
    }
    const auto give = [&](long frame) {
        for (const Line& line : lines) {
            if (line.before == frame) {
                const std::size_t space = line.text.find(' ');
                const std::string tag = line.text.substr(0, space);
                const std::string rest = space == std::string::npos ? std::string() : line.text.substr(space + 1);
                harness::log_line(tag.c_str(), rest.c_str());
            }
        }
    };
    const long frames = std::strtol(argv[2], nullptr, 10);
    for (long f = 1; f <= frames && !g_quit; ++f) {
        give(f);
        harness::frame(static_cast<std::uint64_t>(f));
    }
    return 0;
}
