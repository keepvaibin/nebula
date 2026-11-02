// The kernel's rules (Game/System/GameThreads.hpp): one world thread runs at a time, the main thread first when it wants to; a blocked
// thread lets go of the world; a mutex excludes; messages keep their order and a jammed one comes first; a cancelled thread never runs
// again; the audio thread stays out of a section with host events disabled, wakes a waiter, and waits for a mutex without stopping the world.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


#include "nebula_threads.h"
#include "Game/System/GameThreads.hpp"
#include "nebula_module.h"

namespace {
    char sOrder[64];
    int sOrderLength;
    int sFailures;

    void note(char c) {
        sOrder[sOrderLength++] = c;
        sOrder[sOrderLength] = '\0';
    }

    void expectOrder(const char* pWhat, const char* pWant) {
        if (strcmp(sOrder, pWant) != 0) {
            printf("FAIL %s: got %s, want %s\n", pWhat, sOrder, pWant);
            sFailures++;
        }
        sOrderLength = 0;
        sOrder[0] = '\0';
    }

    void expect(const char* pWhat, long long got, long long want) {
        if (got != want) {
            printf("FAIL %s: %lld (want %lld)\n", pWhat, got, want);
            sFailures++;
        }
    }

    // Main waits until every other thread able to run has run: a new thread comes last, and wakes it.
    GameEvent sIdle;
    GameThreadRecord sIdler;

    void* idlerMain(void*) {
        sIdle.signal();
        return nullptr;
    }

    void letOthersRun() {
        sIdle.init();
        GameThreads::create(&sIdler, idlerMain, nullptr, 0x4000);
        GameThreads::resume(&sIdler);
        sIdle.wait();
    }

    // A started thread runs when main blocks, in the order they were started.
    GameThreadRecord sFirst, sSecond;

    void* firstMain(void*) {
        note('1');
        return nullptr;
    }

    void* secondMain(void*) {
        note('2');
        return nullptr;
    }

    void threadOrder() {
        GameThreads::create(&sFirst, firstMain, nullptr, 0x4000);
        GameThreads::create(&sSecond, secondMain, nullptr, 0x4000);
        GameThreads::resume(&sFirst);
        GameThreads::resume(&sSecond);
        note('m');
        letOthersRun();
        note('n');
        expectOrder("thread order", "m12n");
    }

    // A thread waking main lets main run at its next kernel call, before it goes on.
    GameThreadRecord sYielder;
    GameEvent sMainWait;
    GameMutex sYieldMutex;

    void* yielderMain(void*) {
        note('a');
        sMainWait.signal();
        note('b');
        sYieldMutex.lock();  // main has the world from here
        note('c');
        sYieldMutex.unlock();
        return nullptr;
    }

    void mainFirst() {
        sMainWait.init();
        sYieldMutex.init();
        GameThreads::create(&sYielder, yielderMain, nullptr, 0x4000);
        GameThreads::resume(&sYielder);
        sMainWait.wait();
        note('m');
        letOthersRun();
        expectOrder("main first", "abmc");
    }

    // B blocks on the mutex A holds across a wait; A's unlock lets B in.
    GameMutex sMutex;
    GameEvent sHold;
    GameThreadRecord sOwner, sWaiter;

    void* ownerMain(void*) {
        sMutex.lock();
        note('A');
        sHold.wait();
        note('a');
        sMutex.unlock();
        return nullptr;
    }

    void* waiterMain(void*) {
        note('B');
        sMutex.lock();
        note('b');
        sMutex.unlock();
        return nullptr;
    }

    void mutexOrder() {
        sMutex.init();
        sHold.init();
        GameThreads::create(&sOwner, ownerMain, nullptr, 0x4000);
        GameThreads::create(&sWaiter, waiterMain, nullptr, 0x4000);
        GameThreads::resume(&sOwner);
        GameThreads::resume(&sWaiter);
        letOthersRun();
        note('m');
        sHold.signal();
        letOthersRun();
        letOthersRun();  // the waiter, woken behind the first idler
        expectOrder("mutex", "ABmab");
    }

    // A jammed message is received before those already queued; a full queue refuses without blocking.
    void messageOrder() {
        void* slots[3];
        GameMessageQueue queue;
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

    // A signal with nobody waiting is kept for the next wait, and taken by it.
    void eventKept() {
        GameEvent event;
        event.init();
        event.signal();
        event.wait();
        expect("signal taken", event.signalled, false);
    }

    // A thread cancelled while it waits never runs again.
    GameThreadRecord sCancelled;
    GameEvent sNever;

    void* cancelledMain(void*) {
        note('c');
        sNever.wait();
        note('X');
        return nullptr;
    }

    void cancel() {
        sNever.init();
        GameThreads::create(&sCancelled, cancelledMain, nullptr, 0x4000);
        GameThreads::resume(&sCancelled);
        letOthersRun();
        GameThreads::cancel(&sCancelled);
        expect("cancelled thread terminated", GameThreads::isTerminated(&sCancelled), true);
        sNever.signal();
        letOthersRun();
        expectOrder("cancel", "c");
    }

    // The audio thread stays out of a section with host events disabled; what it sends wakes a waiting thread. It waits for a mutex
    // a world thread holds without holding its section, or that thread could never finish.
    NebulaHostThread* sAudioHost;
    volatile int sAudioEntered;
    volatile int sAudioFinished;
    GameMessageQueue sAudioQueue;
    void* sAudioSlots[1];
    GameMutex sAudioMutex;

    void audioMain(void*) {
        GameThreads::enterAudio();
        sAudioEntered = 1;
        sAudioQueue.send(reinterpret_cast< void* >(static_cast< uintptr_t >('w')), false);
        sAudioMutex.lock();
        sAudioMutex.unlock();
        GameThreads::leaveAudio();
        sAudioFinished = 1;
        nebula_host_thread_exit(sAudioHost);
    }

    void audioThread() {
        sAudioQueue.init(sAudioSlots, 1);
        sAudioMutex.init();
        sAudioMutex.lock();
        const bool enabled = GameThreads::excludeAudio();
        sAudioHost = nebula_host_thread_create(audioMain, nullptr, 0x10000);
        nebula_host_thread_pass(sAudioHost);
        for (volatile int i = 0; i < 20000000; i++) {
        }
        expect("audio waits for a section with events disabled", sAudioEntered, 0);
        GameThreads::restoreAudio(enabled);
        void* message;
        sAudioQueue.receive(&message, true);
        expect("audio entered", sAudioEntered, 1);
        note(static_cast< char >(reinterpret_cast< uintptr_t >(message)));
        expectOrder("audio message", "w");
        // It now waits for this mutex; the section it held must be free.
        for (volatile int i = 0; i < 20000000; i++) {
        }
        {
            GameAudioExclusion guard;
        }
        sAudioMutex.unlock();
        while (sAudioFinished == 0) {
        }
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
            GameThreads::restoreAudio(GameThreads::excludeAudio());
        }
        printf("perf: excludeAudio+restoreAudio %.2f ns\n", (seconds() - start) * 1e9 / kPairs);

        GameMutex mutex;
        mutex.init();
        start = seconds();
        for (int i = 0; i < kPairs; i++) {
            mutex.lock();
            mutex.unlock();
        }
        printf("perf: lock+unlock %.2f ns\n", (seconds() - start) * 1e9 / kPairs);
    }

    void gameMain() {
        threadOrder();
        mainFirst();
        mutexOrder();
        messageOrder();
        eventKept();
        cancel();
        audioThread();
        measure();
        printf(sFailures != 0 ? "%d failures\n" : "all passed\n", sFailures);
        fflush(stdout);
        exit(sFailures != 0);
    }
}  // namespace

// The module's file wants the game's main; no host: the log goes to stderr.
extern "C" void nebula_game_main(void) {
}

int main() {
    nebula_threads_init();
    gameMain();
    return 0;
}
