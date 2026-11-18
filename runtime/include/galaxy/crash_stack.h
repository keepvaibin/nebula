// SPDX-License-Identifier: GPL-3.0-only
// The stack a crash report runs on (P-017). A fatal signal's handler (platform/sdl/crash_log.cpp) runs on the thread
// that faulted, and when the fault is that thread's own stack running out, the handler has no stack left to run on:
// the process ends without a word. POSIX gives each thread its own alternate stack for its signals; every thread
// Nebula starts takes one where it names itself (windows_power.h name_current_thread, host_main.cpp name_thread), the
// main thread where the crash report is installed. Windows reports a crash from its own watcher process.
#pragma once

#if !defined(_WIN32)
#include <pthread.h>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstddef>

namespace galaxy::host {

namespace crash_stack_detail {

// The report formats a few lines and walks the stack (crash_log.cpp report): a few kilobytes. 64 KB, as the mods' guard
// gives a thread (luma_guard.cpp kAltBytes), above one page that faults if even that runs out.
constexpr std::size_t kBytes = 64 * 1024;

// The guard page below the stack: one page.
inline std::size_t guard_bytes() noexcept {
    const long page = sysconf(_SC_PAGESIZE);
    return page > 0 ? static_cast<std::size_t>(page) : 4096u;
}

// A thread's stack (its mapping: the guard page, then the stack), freed as the thread ends: off as its signal stack
// first, if it still is.
inline void free_stack(void* mapping) noexcept {
    char* const map = static_cast<char*>(mapping);
    const std::size_t guard = guard_bytes();
    stack_t now{};
    if (sigaltstack(nullptr, &now) == 0 && now.ss_sp == map + guard) {
        stack_t off{};
        off.ss_flags = SS_DISABLE;
        sigaltstack(&off, nullptr);
    }
    munmap(map, guard + kBytes);
}

// The thread's stack is the value of this key, whose destructor frees it as the thread ends. A thread_local with a
// destructor would serve too, but in a header each file that uses it makes its own initialisation routine, and Apple's
// linker takes those as one symbol defined twice.
struct StackKey {
    pthread_key_t key{};
    bool made = false;  // no key when the system has none left
};
inline const StackKey& stack_key() noexcept {
    static const StackKey key = [] {
        StackKey made;
        made.made = pthread_key_create(&made.key, &free_stack) == 0;
        return made;
    }();
    return key;
}

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
    const std::size_t guard = guard_bytes();
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
    // Without a key (none left), the stack stays until the process ends rather than being taken away while in use.
    if (const StackKey& key = stack_key(); key.made) (void)pthread_setspecific(key.key, bytes);
}

}  // namespace galaxy::host
#else
namespace galaxy::host {
[[nodiscard]] inline bool has_crash_stack() noexcept { return false; }
inline void crash_stack_for_this_thread() noexcept {}
}  // namespace galaxy::host
#endif
