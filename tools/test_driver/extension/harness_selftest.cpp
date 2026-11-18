// SPDX-License-Identifier: GPL-3.0-only
// The host half's after-step waits, without the game (tools/lumacore/tests/harness_wait_test.py builds and runs it).
//
// harness_selftest [--host] <plan> <frames> [[@<frame>:]<log line> ...]: reads the plan as the host does, then runs up to
// <frames> game frames or until the half ends the run. Each log line (a mod's fact is "game: [mod <id>] fact <key>
// <JSON>") is given before the first frame, or before game frame <frame> when it starts "@<frame>:". The facts go to
// the plan's out folder; the test reads them. The first line on standard output is the plan's clock: "clock steps" or
// "clock real" (tools/lumacore/tests/harness_time_test.py). With --host before the plan, the half is given a host
// that sizes its window and loses its device by printing "window <w> <h>" and "lose_device" (harness_steps_test.py);
// without it, no host. With --pipelines-until <frame> after --host, the host shows its settings menu with one pipeline
// left until that game frame; each frame's picture is asked for as the host does after presenting, and a picture taken
// prints "picture <file name> <game frame>" (harness_steps_test.py: a picture held for pipelines holds the steps).
// With --keys-until <frame> after --host (after --pipelines-until when both), the host's menus have a key waiting
// until that game frame (menu_keys_waiting: a busy window's thread).
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

long g_frame = 0;
long g_pipelines_until = 0;
long g_keys_until = 0;

std::uint32_t menu_open(void*) {
    return g_pipelines_until != 0 ? NEBULA_EXT_MENU_SETTINGS : NEBULA_EXT_MENU_NONE;
}

std::uint32_t pipelines_left(void*) {
    return g_frame < g_pipelines_until ? 1u : 0u;
}

std::uint32_t menu_keys_waiting(void*) {
    return g_frame < g_keys_until ? 1u : 0u;
}

}  // namespace

int main(int argc, char** argv) {
    const bool with_host = argc > 1 && std::string(argv[1]) == "--host";
    if (with_host) {
        --argc;
        ++argv;
    }
    if (with_host && argc > 2 && std::string(argv[1]) == "--pipelines-until") {
        g_pipelines_until = std::strtol(argv[2], nullptr, 10);
        argc -= 2;
        argv += 2;
    }
    if (with_host && argc > 2 && std::string(argv[1]) == "--keys-until") {
        g_keys_until = std::strtol(argv[2], nullptr, 10);
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
    if (g_pipelines_until != 0) {
        host.menu_open = menu_open;
        host.pipelines_left = pipelines_left;
    }
    if (g_keys_until != 0) {
        host.menu_keys_waiting = menu_keys_waiting;
    }
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
        g_frame = f;
        give(f);
        harness::frame(static_cast<std::uint64_t>(f));
        if (g_pipelines_until != 0 && !g_quit) {
            if (const char* const path = harness::picture_path(static_cast<std::uint64_t>(f), NEBULA_EXT_PICTURE_GAME)) {
                const std::string file = path;
                std::printf("picture %s %ld\n", file.substr(file.find_last_of("/\\") + 1).c_str(), f);
                std::fflush(stdout);
            }
        }
    }
    return 0;
}
