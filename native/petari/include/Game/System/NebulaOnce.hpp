#pragma once

// Work done once, by the first thread that asks, while every other thread that asks meanwhile waits for it to end
// (P-183). The game module has no C runtime to give it std::call_once or a function-local static's guard, so this is
// the same with the compiler's atomics. Meant for short work (an index built in microseconds): the others spin.

#include <nebula_types.h>

// A spinning thread's pause: the core's hint that this is a wait, not work.
inline void nebulaCpuPause() {
#if defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
    __asm__ volatile("yield");
#endif
}

// Zero-initialized as a global or static: no constructor runs before the first call.
struct NebulaOnce {
    // 0 not done, 1 being done, 2 done.
    u32 mState;

    bool isDone() const { return __atomic_load_n(&mState, __ATOMIC_ACQUIRE) == 2u; }

    // Runs `work` on the first call; every call returns only once it has ended, and what it wrote is seen.
    template < typename Work >
    void run(Work work) {
        if (isDone()) {
            return;
        }
        u32 notDone = 0u;
        if (__atomic_compare_exchange_n(&mState, &notDone, 1u, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            work();
            __atomic_store_n(&mState, 2u, __ATOMIC_RELEASE);
            return;
        }
        while (!isDone()) {
            nebulaCpuPause();
        }
    }
};
