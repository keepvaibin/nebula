// SPDX-License-Identifier: GPL-3.0-only
// The crash watcher (crash_watch.h) with a real watcher process: this program, started again with --crash-watch.
// A dump request that gives up while the watcher still writes its dump, then a second request: the second gets its
// own answer and file (P-045: the late answer of the first woke it, and it took that answer as its own).
#include "crash_watch.h"

#include <dbghelp.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace {

bool expect(bool condition, const char* what) {
    if (!condition) std::printf("FAILED: %s\n", what);
    return condition;
}

}  // namespace

int main(int argc, char** argv) {
    namespace cw = galaxy::crash_watch;
    if (argc >= 2 && std::strcmp(argv[1], cw::kSwitch) == 0) return cw::run(argc, argv);
    namespace fs = std::filesystem;
    const fs::path folder = fs::temp_directory_path() / ("galaxy_crash_watch_tests-" + std::to_string(GetCurrentProcessId()));
    std::error_code ignored;
    fs::remove_all(folder, ignored);
    fs::create_directories(folder);
    // Memory a full dump writes: far more than a 20 ms wait.
    std::vector<unsigned char> memory(384u << 20);
    for (std::size_t i = 0; i < memory.size(); i += 4096) memory[i] = static_cast<unsigned char>(i >> 12);

    cw::start(folder, std::chrono::steady_clock::now());
    bool passed = true;
    wchar_t path[1024];
    DWORD error = 0, snapshot = 0;
    // The watcher is ready once it has the game open: a small dump with a long wait.
    cw::Result result = cw::Result::kNoWatcher;
    // Its start is waited for until the deadline, which only reports that it never came (N-1073).
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (result == cw::Result::kNoWatcher && std::chrono::steady_clock::now() < deadline) {
        result = cw::write_dump(GetCurrentThreadId(), nullptr, MiniDumpNormal, path, 1024, &error, &snapshot, 30000, L"ready");
        if (result == cw::Result::kNoWatcher) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    passed &= expect(result == cw::Result::kWritten, "the watcher writes a dump");

    wchar_t first[1024];
    result = cw::write_dump(GetCurrentThreadId(), nullptr, MiniDumpWithFullMemory, first, 1024, &error, &snapshot, 20, L"first");
    passed &= expect(result == cw::Result::kFailed && error == WAIT_TIMEOUT, "a full dump outlasts a 20 ms wait");

    wchar_t second[1024];
    result = cw::write_dump(GetCurrentThreadId(), nullptr, MiniDumpNormal, second, 1024, &error, &snapshot, 60000, L"second");
    passed &= expect(result == cw::Result::kWritten, "the next request is answered");
    passed &= expect(std::wcsstr(second, L"second") != nullptr && fs::is_regular_file(second, ignored) &&
                         fs::file_size(second, ignored) > 0u,
                     "its answer is its own: its dump file is written when it returns");
    passed &= expect(fs::is_regular_file(first, ignored), "the first dump was written too");

    fs::remove_all(folder, ignored);
    if (!passed) return 1;
    std::printf("crash watch tests passed\n");
    return 0;
}
