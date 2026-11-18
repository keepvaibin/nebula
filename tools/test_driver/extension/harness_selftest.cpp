// SPDX-License-Identifier: GPL-3.0-only
// The host half's after-step waits, without the game (tools/lumacore/tests/harness_wait_test.py builds and runs it).
//
// harness_selftest [--host] <plan> <frames> [[@<frame>:]<log line> ...]: reads the plan as the host does, then runs up to
// <frames> game frames or until the half ends the run. Each log line (a mod's fact is "game: [mod <id>] fact <key>
// <JSON>") is given before the first frame, or before game frame <frame> when it starts "@<frame>:". The facts go to
// the plan's out folder; the test reads them. The first line on standard output is the plan's clock: "clock steps" or
// "clock real" (tools/lumacore/tests/harness_time_test.py). With --host before the plan, the half is given a host
// that sizes its window, loses its device and starts the game again by printing "window <w> <h>", "lose_device" and
// "restart" (harness_steps_test.py); --again <n> after it makes that host's start the one with n starts before it.
// Without --host, no host.
#include "harness.h"

#include <nebula_host_extension.h>

#include <cstdint>
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

void resize_window(void*, std::uint32_t width, std::uint32_t height) {
    std::printf("window %u %u\n", width, height);
    std::fflush(stdout);
}

void lose_gpu_device(void*) {
    std::printf("lose_device\n");
    std::fflush(stdout);
}

std::uint32_t restart(void*) {
    std::printf("restart\n");
    std::fflush(stdout);
    return 1u;
}

}  // namespace

int main(int argc, char** argv) {
    const bool with_host = argc > 1 && std::string(argv[1]) == "--host";
    if (with_host) {
        --argc;
        ++argv;
    }
    std::uint32_t again = 0u;
    if (with_host && argc > 2 && std::string(argv[1]) == "--again") {
        again = static_cast<std::uint32_t>(std::strtoul(argv[2], nullptr, 10));
        argc -= 2;
        argv += 2;
    }
    if (argc < 3) {
        std::fprintf(stderr, "usage: harness_selftest [--host] <plan> <frames> [<log line> ...]\n");
        return 2;
    }
    static NebulaHostForExtension host{NEBULA_HOST_EXTENSION_VERSION, sizeof(NebulaHostForExtension)};
    host.resize_window = resize_window;
    host.lose_gpu_device = lose_gpu_device;
    host.restart = restart;
    host.started_again = again;
    if (!harness::start(with_host ? &host : nullptr, argv[1], log, quit)) {
        return 1;
    }
    std::printf("clock %s\n", harness::real_time() ? "real" : "steps");
    std::fflush(stdout);
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
