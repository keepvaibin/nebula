#include "galaxy/output_relay.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <fcntl.h>
#include <io.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

// Standard error is a 4 KB pipe whose reader stalls for 800 ms, as a paused launcher would. With the relay, 220 KB
// of log lines are written without waiting on it, and every byte still arrives, in order, once the reader resumes.
int main() {
    HANDLE read = nullptr;
    HANDLE write = nullptr;
    if (!CreatePipe(&read, &write, nullptr, 4096u)) {
        std::printf("FAILED: CreatePipe\n");
        return 1;
    }
    const int fd = _open_osfhandle(reinterpret_cast<intptr_t>(write), _O_BINARY);
    if (fd == -1 || _dup2(fd, 2) != 0) {
        std::printf("FAILED: stderr onto the test pipe\n");
        return 1;
    }
    _close(fd);
    SetStdHandle(STD_ERROR_HANDLE, reinterpret_cast<HANDLE>(_get_osfhandle(2)));

    std::string received;
    std::atomic<std::size_t> target{0u};
    std::thread reader([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        const auto give_up = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        char buffer[8192];
        while (std::chrono::steady_clock::now() < give_up) {
            const std::size_t want = target.load();
            if (want != 0u && received.size() >= want) {
                return;
            }
            DWORD queued = 0;
            DWORD got = 0;
            if (PeekNamedPipe(read, nullptr, 0, nullptr, &queued, nullptr) && queued != 0u &&
                ReadFile(read, buffer, sizeof(buffer), &got, nullptr)) {
                received.append(buffer, got);
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    });

    galaxy::output::start_relay();
    std::string expected;
    const auto begin = std::chrono::steady_clock::now();
    for (int line = 0; line < 4096; ++line) {
        char text[64];
        const int length = std::snprintf(text, sizeof(text), "[relay-test] line=%05d %s\n", line,
                                         "abcdefghijklmnopqrstuvwxyz0123");
        std::fputs(text, stderr);
        expected.append(text, static_cast<std::size_t>(length));
    }
    std::fflush(stderr);
    const auto written_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin).count();
    target.store(expected.size());
    galaxy::output::drain_relay(std::chrono::milliseconds(5000));
    reader.join();

    bool passed = true;
    if (written_ms > 200) {
        std::printf("FAILED: 220 KB of log lines took %lld ms against a stalled reader\n",
                    static_cast<long long>(written_ms));
        passed = false;
    } else {
        std::printf("ok: 220 KB of log lines in %lld ms against a reader stalled for 800 ms\n",
                    static_cast<long long>(written_ms));
    }
    if (received != expected) {
        std::printf("FAILED: %zu of %zu bytes arrived intact\n", received.size(), expected.size());
        passed = false;
    } else {
        std::printf("ok: every byte arrived in order\n");
    }
    if (galaxy::output::relay_dropped_bytes() != 0u) {
        std::printf("FAILED: the relay dropped bytes under its cap\n");
        passed = false;
    }
    return passed ? 0 : 1;
}
