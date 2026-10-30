// The kernel's order of events, which the game's timing depends on (Game/System/NebulaKernel.hpp): a higher-priority thread runs
// as soon as it can (a resume, a message it waits for), a lower one only when the higher ones block, a sleep lets the
// others run and wakes through the timer, alarms fire in deadline order, a thread waiting for a mutex lends its
// priority to the owner, a jammed message is received first, and a cancelled thread never runs again.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

    // A sleep: an alarm resumes the suspended thread.
    void onSleepAlarm(NebulaAlarm* pAlarm) {
        NebulaKernel::resume(static_cast< NebulaThread* >(pAlarm->userData));
    }

    void sleepTicks(int64_t ticks) {
        const bool enabled = NebulaKernel::disableInterrupts();
        NebulaAlarm alarm;
        alarm.init();
        alarm.userData = NebulaKernel::current();
        alarm.set(ticks, onSleepAlarm);
        NebulaKernel::suspend(NebulaKernel::current());
        alarm.cancel();
        NebulaKernel::restoreInterrupts(enabled);
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
        sleepTicks(OSMillisecondsToTicks(2));  // low runs while main sleeps
        note('o');
        expectOrder("thread order", "HmhnLo");
    }

    NebulaAlarm sAlarms[3];

    void onAlarm(NebulaAlarm* pAlarm) {
        note(static_cast< char >('a' + (pAlarm - sAlarms)));
    }

    void alarmOrder() {
        for (int i = 0; i < 3; i++) {
            sAlarms[i].init();
        }
        sAlarms[0].set(OSMillisecondsToTicks(6), onAlarm);
        sAlarms[1].set(OSMillisecondsToTicks(2), onAlarm);
        sAlarms[2].set(OSMillisecondsToTicks(4), onAlarm);
        sleepTicks(OSMillisecondsToTicks(10));
        expectOrder("alarm order", "bca");

        const int64_t before = NebulaKernel::time();
        sleepTicks(OSMillisecondsToTicks(5));
        const int64_t slept = NebulaKernel::time() - before;
        if (slept < OSMillisecondsToTicks(5) || slept > OSMillisecondsToTicks(40)) {
            printf("FAIL sleep(5 ms) took %lld ms\n", static_cast< long long >(OSTicksToMilliseconds(slept)));
            sFailures++;
        }
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
        sleepTicks(OSMillisecondsToTicks(1));  // the owner runs, locks, sleeps on the queue
        note('m');
        NebulaKernel::resume(&sWaiter);  // runs at once, blocks on the mutex
        expect("owner's priority while B waits", sOwner.priority, 12);
        note('w');
        sHold.wakeup();  // the owner, at 12, outranks main
        expect("owner's priority after unlocking", sOwner.priority, 20);
        note('z');
        sleepTicks(OSMillisecondsToTicks(1));
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
        sleepTicks(OSMillisecondsToTicks(1));
        expectOrder("cancel", "c");
    }

    // Costs of the hot paths and the timer's lateness, printed for docs/NATIVE_PERFORMANCE.md (not pass/fail).
    volatile int64_t sFiredAt;

    void onLateAlarm(NebulaAlarm*) {
        sFiredAt = NebulaKernel::time();
    }

    double ticksToNs(int64_t ticks) {
        return static_cast< double >(ticks) * 1e9 / static_cast< double >(OS_TIMER_CLOCK);
    }

    void measure() {
        const int kPairs = 10000000;
        int64_t start = NebulaKernel::time();
        for (int i = 0; i < kPairs; i++) {
            NebulaKernel::restoreInterrupts(NebulaKernel::disableInterrupts());
        }
        printf("perf: disableInterrupts+restoreInterrupts %.2f ns\n", ticksToNs(NebulaKernel::time() - start) / kPairs);

        NebulaMutex mutex;
        mutex.init();
        start = NebulaKernel::time();
        for (int i = 0; i < kPairs; i++) {
            mutex.lock();
            mutex.unlock();
        }
        printf("perf: lock+unlock %.2f ns\n", ticksToNs(NebulaKernel::time() - start) / kPairs);

        static const char* const kModes[] = {"busy", "asleep"};
        for (int mode = 0; mode < 2; mode++) {
            double worst = 0, sum = 0;
            const int kRuns = 50;
            for (int run = 0; run < kRuns; run++) {
                NebulaAlarm alarm;
                alarm.init();
                sFiredAt = 0;
                const int64_t due = NebulaKernel::time() + OSMicrosecondsToTicks(1000);
                alarm.set(OSMicrosecondsToTicks(1000), onLateAlarm);
                while (sFiredAt == 0) {
                    if (mode == 1) {
                        sleepTicks(OSMicrosecondsToTicks(5000));
                        continue;
                    }
                    for (int i = 0; i < 100; i++) {
                        NebulaKernel::restoreInterrupts(NebulaKernel::disableInterrupts());
                    }
                }
                const double late = ticksToNs(sFiredAt - due) / 1000.0;
                sum += late;
                if (late > worst) {
                    worst = late;
                }
            }
            printf("perf: 1 ms alarm, thread %s: late mean %.1f us, worst %.1f us\n", kModes[mode], sum / kRuns, worst);
        }
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
        alarmOrder();
        mutexInheritance();
        messageOrder();
        cancel();
        measure();
        printf(sFailures != 0 ? "%d failures\n" : "all passed\n", sFailures);
        fflush(stdout);
        exit(sFailures != 0);
    }
}  // namespace

int main() {
    nebula_os_boot(gameMain);
}
