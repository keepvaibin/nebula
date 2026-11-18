// SPDX-License-Identifier: GPL-3.0-only
// A call on a thread of its own, waited for at most a time: for the last work of a process that ends the hard way (a
// crash, a fatal error), where the thread that failed may hold a lock the call needs. No heap: the call's state is the
// caller's (static), as the heap may be what failed.
#pragma once

#include <atomic>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <pthread.h>
#include <time.h>
#endif

namespace galaxy {

struct BoundedCall {
    void (*call)() = nullptr;
    std::atomic<bool> done{false};
};

namespace detail {
#if defined(_WIN32)
inline DWORD WINAPI bounded_call_thread(LPVOID state) {
#else
inline void* bounded_call_thread(void* state) {
#endif
    auto* const bounded = static_cast<BoundedCall*>(state);
    bounded->call();
    bounded->done.store(true, std::memory_order_release);
#if defined(_WIN32)
    return 0;
#else
    return nullptr;
#endif
}
}  // namespace detail

// Runs `state.call` on a new thread and waits for it at most `wait_ms`. True when it ended in time; false when it did
// not (it runs on, left to the process's end) or no thread could start. One use of `state` at a time.
inline bool call_bounded(BoundedCall& state, unsigned wait_ms) noexcept {
    state.done.store(false, std::memory_order_relaxed);
#if defined(_WIN32)
    const HANDLE thread = CreateThread(nullptr, 0, detail::bounded_call_thread, &state, 0, nullptr);
    if (thread == nullptr) return false;
    WaitForSingleObject(thread, wait_ms);
    CloseHandle(thread);
#else
    pthread_t thread;
    if (pthread_create(&thread, nullptr, detail::bounded_call_thread, &state) != 0) return false;
    pthread_detach(thread);
    for (unsigned waited = 0; waited < wait_ms && !state.done.load(std::memory_order_acquire); waited += 5u) {
        timespec step{0, 5'000'000};
        nanosleep(&step, nullptr);
    }
#endif
    return state.done.load(std::memory_order_acquire);
}

}  // namespace galaxy
