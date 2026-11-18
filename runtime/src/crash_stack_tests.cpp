// SPDX-License-Identifier: GPL-3.0-only
// A thread that runs out of stack is still reported (crash_stack.h, P-017): in a child process, a fatal-signal handler
// on the alternate stack (as crash_log.cpp installs it) and a thread that names itself as Nebula's threads do, then
// recurses until its stack is gone. The handler must run and say so. Windows has no such signals (its crash watcher
// reports from another process): nothing to test there.
#include "galaxy/crash_stack.h"
#include "galaxy/windows_power.h"

#include <cstdio>

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define NEBULA_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define NEBULA_SANITIZED 1
#endif
#endif

#if !defined(_WIN32)
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <thread>

namespace {

int g_pipe = -1;
volatile bool g_stop = false;

void on_fatal(int, siginfo_t*, void*) {
    static const char kSaid[] = "reported";
    (void)!write(g_pipe, kSaid, sizeof(kSaid) - 1);
    _exit(0);
}

// A frame of about a kilobyte a call, never ending (g_stop is never set), never folded into a loop.
[[gnu::noinline]] int deeper(int depth) {
    volatile char pad[1024];
    pad[0] = static_cast<char>(depth);
    if (g_stop) return pad[0];
    return deeper(depth + 1) + pad[0];
}

// In a child: the handler, then a thread that (`named`) names itself and overflows. What the handler wrote.
bool overflow_reported(bool named) {
    int fds[2];
    if (pipe(fds) != 0) return false;
    const pid_t child = fork();
    if (child == 0) {
        close(fds[0]);
        g_pipe = fds[1];
        struct sigaction action {};
        action.sa_sigaction = on_fatal;
        action.sa_flags = SA_SIGINFO | SA_ONSTACK;
        sigemptyset(&action.sa_mask);
        sigaction(SIGSEGV, &action, nullptr);
        sigaction(SIGBUS, &action, nullptr);
        std::thread thread([named] {
            if (named) galaxy::host::name_current_thread(L"Nebula Overflow");
            deeper(0);
        });
        thread.join();
        _exit(1);
    }
    close(fds[1]);
    char said[16]{};
    const ssize_t got = read(fds[0], said, sizeof(said) - 1);
    close(fds[0]);
    int status = 0;
    waitpid(child, &status, 0);
    return got > 0 && std::strcmp(said, "reported") == 0;
}

bool expect(bool ok, const char* what) {
    if (!ok) std::fprintf(stderr, "FAILED: %s\n", what);
    return ok;
}

}  // namespace

int main() {
    bool ok = true;
    // A named thread has its stack; a second call keeps it.
    std::thread([&ok] {
        galaxy::host::name_current_thread(L"Nebula Test");
        ok &= expect(galaxy::host::has_crash_stack(), "a named thread has a crash stack");
        stack_t first{};
        sigaltstack(nullptr, &first);
        galaxy::host::crash_stack_for_this_thread();
        stack_t second{};
        sigaltstack(nullptr, &second);
        ok &= expect(first.ss_sp == second.ss_sp, "asked again, the same stack");
    }).join();
#if defined(NEBULA_SANITIZED)
    // A sanitizer gives each thread a signal stack of its own and runs the handler through its own: nothing to tell.
    std::printf("crash_stack: the overflow cases are not run under a sanitizer\n");
#else
    ok &= expect(overflow_reported(true), "a named thread's stack overflow is reported");
#if !defined(__ANDROID__)
    // The same thread without the name, as every thread was before (Android gives each thread a stack of its own): the
    // handler has no stack to run on, and the process ends without a word.
    ok &= expect(!overflow_reported(false), "an unnamed thread's overflow goes unreported (the case this fixes)");
#endif
#endif
    std::printf("%s\n", ok ? "crash_stack: every case holds" : "crash_stack: FAILED");
    return ok ? 0 : 1;
}
#else
int main() {
    std::printf("crash_stack: nothing to test on Windows\n");
    return 0;
}
#endif
