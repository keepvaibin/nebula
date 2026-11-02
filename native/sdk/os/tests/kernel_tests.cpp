// The kernel's order of events, which the game's timing depends on (Game/System/NebulaKernel.hpp): a higher-priority thread runs
// as soon as it can (a resume, a message it waits for), a lower one only when the higher ones block, a thread waiting for a mutex lends its
// priority to the owner, a jammed message is received first, a cancelled thread never runs again, and the audio thread
// stays out of a section with host events disabled.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <revolution/os.h>

#include "nebula_kernel.h"
#include "Game/System/NebulaKernel.hpp"
#include "nebula_sdk.h"

// No host: the log goes to stderr.
extern "C" const struct NebulaHostServices* nebula_host(void) {
    return nullptr;
}

namespace {
    char sOrder[64];
    int sOrderLength;
    int sFailures;

    void note(char c) {
        sOrder[sOrderLength++] = c;
    }

    void expectOrder(const char* pWhat, const char* pWant) {
        sOrder[sOrderLength] = 0;
        if (strcmp(sOrder, pWant) != 0) {
            printf("FAIL %s: %s (want %s)\n", pWhat, sOrder, pWant);
            sFailures++;
        }
        sOrderLength = 0;
    }

    void expect(const char* pWhat, long long got, long long want) {
        if (got != want) {
            printf("FAIL %s: %lld (want %lld)\n", pWhat, got, want);
            sFailures++;
        }
    }

    // Main (16) waits until every other thread able to run has run: the lowest-priority thread wakes it.
    NebulaEvent sIdle;
    NebulaThread sIdler;

    void* idlerMain(void*) {
        sIdle.signal();
        return nullptr;
    }

    void letOthersRun() {
        sIdle.init();
        NebulaKernel::create(&sIdler, idlerMain, nullptr, 0x4000, 31, true);
        NebulaKernel::resume(&sIdler);
        sIdle.wait();
    }

    NebulaMessageQueue sQueue;
    void* sQueueSlots[4];
    NebulaThread sHigh, sLow;

    void* highMain(void*) {
        note('H');
        void* message;
        sQueue.receive(&message, true);
        note(static_cast< char >(reinterpret_cast< uintptr_t >(message)));
        return nullptr;
    }

    void* lowMain(void*) {
        note('L');
        return nullptr;
    }

    void threadOrder() {
        sQueue.init(sQueueSlots, 4);
        NebulaKernel::create(&sHigh, highMain, nullptr, 0x4000, 10, false);
        NebulaKernel::create(&sLow, lowMain, nullptr, 0x4000, 20, false);
        NebulaKernel::resume(&sHigh);  // runs at once, blocks on the queue
        note('m');
        NebulaKernel::resume(&sLow);  // lower: waits
        sQueue.send(reinterpret_cast< void* >(static_cast< uintptr_t >('h')), true);  // wakes high, which runs at once
        note('n');
        letOthersRun();  // low runs while main waits
        note('o');
        expectOrder("thread order", "HmhnLo");
    }

    // A (20) owns the mutex and waits on a queue; B (12) blocks on the mutex and lends A its priority; A, woken, runs
    // before main (16), unlocks, drops back to 20 and B takes the mutex at once.
    NebulaMutex sMutex;
    NebulaThreadQueue sHold;
    NebulaThread sOwner, sWaiter;

    void* ownerMain(void*) {
        sMutex.lock();
        note('A');
        sHold.sleep();
        note('a');
        sMutex.unlock();
        note('u');
        return nullptr;
    }

    void* waiterMain(void*) {
        note('B');
        sMutex.lock();
        note('b');
        sMutex.unlock();
        return nullptr;
    }

    void mutexInheritance() {
        sMutex.init();
        sHold.init();
        NebulaKernel::create(&sOwner, ownerMain, nullptr, 0x4000, 20, true);
        NebulaKernel::create(&sWaiter, waiterMain, nullptr, 0x4000, 12, true);
        NebulaKernel::resume(&sOwner);  // lower than main: waits
        letOthersRun();  // the owner runs, locks, sleeps on the queue
        note('m');
        NebulaKernel::resume(&sWaiter);  // runs at once, blocks on the mutex
        expect("owner's priority while B waits", sOwner.priority, 12);
        note('w');
        sHold.wakeup();  // the owner, at 12, outranks main
        expect("owner's priority after unlocking", sOwner.priority, 20);
        note('z');
        letOthersRun();
        expectOrder("mutex inheritance", "AmBwabzu");
    }

    // A jammed message is received before those already queued; a full queue refuses without blocking.
    void messageOrder() {
        void* slots[3];
        NebulaMessageQueue queue;
        queue.init(slots, 3);
        queue.send(reinterpret_cast< void* >(static_cast< uintptr_t >('x')), true);
        queue.send(reinterpret_cast< void* >(static_cast< uintptr_t >('y')), true);
        queue.jam(reinterpret_cast< void* >(static_cast< uintptr_t >('j')), true);
        expect("full queue refuses", queue.send(nullptr, false), false);
        for (int i = 0; i < 3; i++) {
            void* message;
            queue.receive(&message, true);
            note(static_cast< char >(reinterpret_cast< uintptr_t >(message)));
        }
        expect("empty queue refuses", queue.receive(nullptr, false), false);
        expectOrder("message order", "jxy");
    }

    // A thread cancelled while it waits never runs again; its joiners are released.
    NebulaThread sCancelled;
    NebulaThreadQueue sNever;

    void* cancelledMain(void*) {
        note('c');
        sNever.sleep();
        note('X');
        return nullptr;
    }

    void cancel() {
        sNever.init();
        NebulaKernel::create(&sCancelled, cancelledMain, nullptr, 0x4000, 10, false);
        NebulaKernel::resume(&sCancelled);
        NebulaKernel::cancel(&sCancelled);
        expect("cancelled thread terminated", NebulaKernel::isTerminated(&sCancelled), true);
        sNever.wakeup();
        letOthersRun();
        expectOrder("cancel", "c");
    }

    // The audio thread stays out of a section with host events disabled; what it sends wakes a waiting thread.
    NebulaHostThread* sAudioHost;
    volatile int sAudioEntered;
    NebulaMessageQueue sAudioQueue;
    void* sAudioSlots[1];

    void audioMain(void*) {
        NebulaKernel::enterAudio();
        sAudioEntered = 1;
        sAudioQueue.send(reinterpret_cast< void* >(static_cast< uintptr_t >('w')), false);
        NebulaKernel::leaveAudio();
        nebula_host_thread_exit(sAudioHost);
    }

    void audioThread() {
        sAudioQueue.init(sAudioSlots, 1);
        const bool enabled = NebulaKernel::disableInterrupts();
        sAudioHost = nebula_host_thread_create(audioMain, nullptr, 0x10000);
        nebula_host_thread_pass(sAudioHost);
        for (volatile int i = 0; i < 20000000; i++) {
        }
        expect("audio waits for a section with events disabled", sAudioEntered, 0);
        NebulaKernel::restoreInterrupts(enabled);
        void* message;
        sAudioQueue.receive(&message, true);
        expect("audio entered", sAudioEntered, 1);
        note(static_cast< char >(reinterpret_cast< uintptr_t >(message)));
        expectOrder("audio message", "w");
    }

    // Costs of the hot paths, printed for docs/NATIVE_PERFORMANCE.md (not pass/fail).
    double seconds() {
        struct timespec now;
        timespec_get(&now, TIME_UTC);
        return static_cast< double >(now.tv_sec) + static_cast< double >(now.tv_nsec) * 1e-9;
    }

    void measure() {
        const int kPairs = 10000000;
        double start = seconds();
        for (int i = 0; i < kPairs; i++) {
            NebulaKernel::restoreInterrupts(NebulaKernel::disableInterrupts());
        }
        printf("perf: disableInterrupts+restoreInterrupts %.2f ns\n", (seconds() - start) * 1e9 / kPairs);

        NebulaMutex mutex;
        mutex.init();
        start = seconds();
        for (int i = 0; i < kPairs; i++) {
            mutex.lock();
            mutex.unlock();
        }
        printf("perf: lock+unlock %.2f ns\n", (seconds() - start) * 1e9 / kPairs);
    }

    // The arena is aligned to its size and laid out as the console's memory (MEM1's arena past its first megabyte,
    // MEM2's at 0x10000000), so what the game derives from an address's low bits has the console's value.
    void checkArena() {
        uint8_t* const base = nebula_arena_base();
        void *mem1Lo, *mem1Hi, *mem2Lo, *mem2Hi;
        nebula_memory_arenas(&mem1Lo, &mem1Hi, &mem2Lo, &mem2Hi);
        uint8_t* const mem1 = static_cast< uint8_t* >(mem1Lo);
        uint8_t* const mem2 = static_cast< uint8_t* >(mem2Lo);
        const uintptr_t alignment = static_cast< uintptr_t >(1) << 29;
        const bool aligned = reinterpret_cast< uintptr_t >(base) % alignment == 0;
        if (!aligned || mem1 - base != 0x00100000 || mem2 - base != 0x10000000) {
            printf("FAIL arena: base %p, MEM1 %p, MEM2 %p\n", static_cast< void* >(base), static_cast< void* >(mem1),
                   static_cast< void* >(mem2));
            sFailures++;
        }
    }

    void gameMain() {
        checkArena();
        threadOrder();
        mutexInheritance();
        messageOrder();
        cancel();
        audioThread();
        measure();
        printf(sFailures != 0 ? "%d failures\n" : "all passed\n", sFailures);
        fflush(stdout);
        exit(sFailures != 0);
    }
}  // namespace

int main() {
    nebula_os_boot(gameMain);
}
