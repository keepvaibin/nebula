// SPDX-License-Identifier: GPL-3.0-only
// The stack a crash report runs on (P-017). A fatal signal's handler (platform/sdl/crash_log.cpp) runs on the thread
// that faulted, and when the fault is that thread's own stack running out, the handler has no stack left to run on:
// the process ends without a word. POSIX gives each thread its own alternate stack for its signals; every thread
// Nebula starts takes one where it names itself (windows_power.h name_current_thread, host_main.cpp name_thread), the
// main thread where the crash report is installed. Windows reports a crash from its own watcher process.
#pragma once

#if !defined(_WIN32)
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstddef>

namespace galaxy::host {

namespace crash_stack_detail {

// The report formats a few lines and walks the stack (crash_log.cpp report): a few kilobytes. 64 KB, as the mods' guard
// gives a thread (luma_guard.cpp kAltBytes), above one page that faults if even that runs out.
constexpr std::size_t kBytes = 64 * 1024;

struct Stack {
    char* map = nullptr;  // the guard page, then the stack
    std::size_t size = 0;

    ~Stack() {
        if (map == nullptr) return;
        stack_t now{};
        if (sigaltstack(nullptr, &now) == 0 && now.ss_sp == map + (size - kBytes)) {
            stack_t off{};
            off.ss_flags = SS_DISABLE;
            sigaltstack(&off, nullptr);
        }
        munmap(map, size);
    }
};

inline thread_local Stack t_stack;

}  // namespace crash_stack_detail

// Whether the calling thread has an alternate signal stack.
[[nodiscard]] inline bool has_crash_stack() noexcept {
    stack_t now{};
    return sigaltstack(nullptr, &now) == 0 && (now.ss_flags & SS_DISABLE) == 0;
}

// The calling thread's alternate signal stack: made once, freed when the thread ends. Nothing when the thread has one
// (Android gives each thread one; a mod's guard may have given it one).
inline void crash_stack_for_this_thread() noexcept {
    using namespace crash_stack_detail;
    if (has_crash_stack()) return;
    const long page = sysconf(_SC_PAGESIZE);
    const std::size_t guard = page > 0 ? static_cast<std::size_t>(page) : 4096u;
    void* const map = mmap(nullptr, guard + kBytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (map == MAP_FAILED) return;
    char* const bytes = static_cast<char*>(map);
    (void)mprotect(bytes, guard, PROT_NONE);
    stack_t alt{};
    alt.ss_sp = bytes + guard;
    alt.ss_size = kBytes;
    if (sigaltstack(&alt, nullptr) != 0) {
        munmap(map, guard + kBytes);
        return;
    }
    t_stack.map = bytes;
    t_stack.size = guard + kBytes;
}

}  // namespace galaxy::host
#else
namespace galaxy::host {
[[nodiscard]] inline bool has_crash_stack() noexcept { return false; }
inline void crash_stack_for_this_thread() noexcept {}
}  // namespace galaxy::host
#endif
