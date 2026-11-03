// The game's threads and their synchronisation (Game/System/GameThreads.hpp): host threads, plain blocking primitives and
// two locks. The world lock: one thread at a time changes the game world, the main thread first when it wants to; a blocked
// thread lets go of it. The audio lock: a section the audio thread stays out of.
#include "Game/System/GameThreads.hpp"

#include <stdio.h>

#include <nebula_game_api.h>

#include "nebula_threads.h"
#include "nebula_module.h"

namespace {
    // This thread's record and whether it left the audio section's exclusion: the host keeps them (slots 1 and 2).
    GameThreadRecord* selfSlot() {
        return static_cast< GameThreadRecord* >(nebula_host()->thread_slots(nebula_host()->user)[1]);
    }

    void setSelfSlot(GameThreadRecord* pRecord) {
        nebula_host()->thread_slots(nebula_host()->user)[1] = pRecord;
    }

    bool audioExcludedSlot() {
        return nebula_host()->thread_slots(nebula_host()->user)[2] != nullptr;
    }

    void setAudioExcludedSlot(bool excluded) {
        static char sOn;
        nebula_host()->thread_slots(nebula_host()->user)[2] = excluded ? &sOn : nullptr;
    }

    GameThreadRecord sMainThread;
    GameThreadRecord sAudioThread;
    GameThreadRecord* sThreads;  // every live thread, by `all`

    // All below under the state lock, which is held only to look at them, never across a wait.
    GameThreadRecord* sWorldOwner;
    GameWaitList sWorldWaiters;
    bool sMainWants;

    // Deterministic test mode (the host's test_frame_step): every thread is under the world lock, the sound runs on the thread
    // stepping the clock, and the next frame comes only when no thread can run. Two runs of a build take the same steps.
    bool sTestMode;
    GameWaitList sFrameWaiters;  // test mode: the main thread until the step

    void lockState() {
        nebula_host_lock(NEBULA_HOST_LOCK_STATE);
    }

    void unlockState() {
        nebula_host_unlock(NEBULA_HOST_LOCK_STATE);
    }

    void append(GameWaitList* pList, GameThreadRecord* pThread) {
        pThread->next = nullptr;
        pThread->prev = pList->tail;
        if (pList->tail != nullptr) {
            pList->tail->next = pThread;
        } else {
            pList->head = pThread;
        }
        pList->tail = pThread;
        pThread->list = pList;
    }

    void prepend(GameWaitList* pList, GameThreadRecord* pThread) {
        pThread->prev = nullptr;
        pThread->next = pList->head;
        if (pList->head != nullptr) {
            pList->head->prev = pThread;
        } else {
            pList->tail = pThread;
        }
        pList->head = pThread;
        pThread->list = pList;
    }

    void unlink(GameThreadRecord* pThread) {
        GameWaitList* list = pThread->list;
        if (pThread->prev != nullptr) {
            pThread->prev->next = pThread->next;
        } else {
            list->head = pThread->next;
        }
        if (pThread->next != nullptr) {
            pThread->next->prev = pThread->prev;
        } else {
            list->tail = pThread->prev;
        }
        pThread->list = nullptr;
    }

    void forget(GameThreadRecord* pThread) {
        for (GameThreadRecord** p = &sThreads; *p != nullptr; p = &(*p)->all) {
            if (*p == pThread) {
                *p = pThread->all;
                return;
            }
        }
    }

    // A thread that can go on: a world thread when the world is free or its turn comes, others at once.
    void wake(GameThreadRecord* pThread) {
        if (!pThread->world) {
            nebula_host_thread_pass(pThread->host);
        } else if (sWorldOwner == nullptr) {
            sWorldOwner = pThread;
            nebula_host_thread_pass(pThread->host);
        } else if (pThread == &sMainThread) {
            sMainWants = true;
            prepend(&sWorldWaiters, pThread);
        } else {
            append(&sWorldWaiters, pThread);
        }
    }

    void wakeAll(GameWaitList* pList) {
        while (pList->head != nullptr) {
            GameThreadRecord* thread = pList->head;
            unlink(thread);
            wake(thread);
        }
    }

    void wakeFirst(GameWaitList* pList) {
        if (pList->head != nullptr) {
            GameThreadRecord* thread = pList->head;
            unlink(thread);
            wake(thread);
        }
    }

    // The next waiter takes the world; true if that is `pSelf`, which then must not wait for it.
    bool passWorld(GameThreadRecord* pSelf) {
        GameThreadRecord* next = sWorldWaiters.head;
        sWorldOwner = next;
        if (next == nullptr) {
            return false;
        }
        unlink(next);
        if (next == &sMainThread) {
            sMainWants = false;
        }
        if (next == pSelf) {
            return true;
        }
        nebula_host_thread_pass(next->host);
        return false;
    }

    // Lets go of the world; true if it came straight back. In test mode, with no thread able to run, this thread moves the clock
    // on, holding the world so that what a step wakes runs after the step, in the order it was woken.
    bool releaseWorld(GameThreadRecord* pSelf) {
        if (passWorld(pSelf)) {
            return true;
        }
        if (!sTestMode) {
            return false;
        }
        while (sWorldOwner == nullptr) {
            sWorldOwner = pSelf;
            unlockState();
            const NebulaHostServices* host = nebula_host();
            host->test_frame_step(host->user);
            lockState();
            while (sFrameWaiters.head != nullptr) {
                GameThreadRecord* waiter = sFrameWaiters.head;
                unlink(waiter);
                append(&sWorldWaiters, waiter);
            }
            if (passWorld(pSelf)) {
                return true;
            }
        }
        return false;
    }

    // This thread sleeps on `pList` until woken, with the state lock held on entry and exit. A section the audio thread stays out of
    // ends for the wait: a section never lasts across a sleep.
    void block(GameWaitList* pList) {
        GameThreadRecord* self = selfSlot();
        if (self == &sAudioThread && sTestMode) {
            nebula_fatal("test mode: the audio thread has to wait, and no thread can run while it does");
        }
        append(pList, self);
        const bool inSection = audioExcludedSlot();
        if (inSection) {
            setAudioExcludedSlot(false);
            nebula_host_unlock(NEBULA_HOST_LOCK_AUDIO);
        }
        const bool mine = self->world && releaseWorld(self);
        unlockState();
        if (!mine) {
            nebula_host_thread_wait(self->host);
        }
        if (inSection) {
            nebula_host_lock(NEBULA_HOST_LOCK_AUDIO);
            setAudioExcludedSlot(true);
        }
        lockState();
    }

    // A thread other than main lets main have the world at each kernel call (not in a section the audio thread stays out of: main may
    // want it). With the state lock held.
    void yieldToMain() {
        GameThreadRecord* self = selfSlot();
        if (!sMainWants || audioExcludedSlot() || self == nullptr || !self->world || self == &sMainThread || sWorldOwner != self) {
            return;
        }
        append(&sWorldWaiters, self);
        const bool mine = passWorld(self);
        unlockState();
        if (!mine) {
            nebula_host_thread_wait(self->host);
        }
        lockState();
    }

    void threadMain(void* pArg) {
        GameThreadRecord* self = static_cast< GameThreadRecord* >(pArg);
        setSelfSlot(self);
        GameThreads::exit(self->entry(self->arg));
    }
}  // namespace

void GameEvent::init() {
    waiters.head = waiters.tail = nullptr;
    signalled = false;
}

void GameEvent::wait() {
    lockState();
    while (!signalled) {
        block(&waiters);
    }
    signalled = false;
    unlockState();
}

void GameEvent::signal() {
    lockState();
    signalled = true;
    wakeAll(&waiters);
    unlockState();
}

void GameMutex::init() {
    waiters.head = waiters.tail = nullptr;
    owner = nullptr;
    count = 0;
}

void GameMutex::lock() {
    lockState();
    yieldToMain();
    GameThreadRecord* self = selfSlot();
    while (owner != nullptr && owner != self) {
        block(&waiters);
    }
    owner = self;
    count++;
    unlockState();
}

void GameMutex::unlock() {
    lockState();
    if (owner == selfSlot() && --count == 0) {
        owner = nullptr;
        wakeFirst(&waiters);
    }
    yieldToMain();
    unlockState();
}

void GameMessageQueue::init(void** pSlots, int32_t count) {
    senders.head = senders.tail = nullptr;
    receivers.head = receivers.tail = nullptr;
    slots = pSlots;
    capacity = count;
    first = 0;
    used = 0;
}

bool GameMessageQueue::send(void* pMessage, bool blocks) {
    lockState();
    while (capacity <= used) {
        if (!blocks) {
            unlockState();
            return false;
        }
        block(&senders);
    }
    slots[(first + used) % capacity] = pMessage;
    used++;
    wakeAll(&receivers);
    unlockState();
    return true;
}

bool GameMessageQueue::jam(void* pMessage, bool blocks) {
    lockState();
    while (capacity <= used) {
        if (!blocks) {
            unlockState();
            return false;
        }
        block(&senders);
    }
    first = (first + capacity - 1) % capacity;
    slots[first] = pMessage;
    used++;
    wakeAll(&receivers);
    unlockState();
    return true;
}

bool GameMessageQueue::receive(void** ppMessage, bool blocks) {
    lockState();
    while (used == 0) {
        if (!blocks) {
            unlockState();
            return false;
        }
        block(&receivers);
    }
    if (ppMessage != nullptr) {
        *ppMessage = slots[first];
    }
    first = (first + 1) % capacity;
    used--;
    wakeAll(&senders);
    unlockState();
    return true;
}

namespace GameThreads {
    bool create(GameThreadRecord* pThread, void* (*pEntry)(void*), void* pArg, uint32_t stackSize) {
        pThread->entry = pEntry;
        pThread->arg = pArg;
        pThread->world = true;
        pThread->started = false;
        pThread->ended = false;
        pThread->list = nullptr;
        pThread->next = pThread->prev = nullptr;
        pThread->host = nebula_host_thread_create(threadMain, pThread, stackSize);
        lockState();
        pThread->all = sThreads;
        sThreads = pThread;
        unlockState();
        return true;
    }

    GameThreadRecord* current() {
        return selfSlot();
    }

    void exit(void* pValue) {
        lockState();
        GameThreadRecord* self = selfSlot();
        self->value = pValue;
        self->ended = true;
        forget(self);
        NebulaHostThread* const host = self->host;
        if (self->world) {
            releaseWorld(self);
        }
        unlockState();
        nebula_host_thread_exit(host);
        nebula_fatal("an exited thread ran again");
    }

    // A thread that has not started or sleeps on a list; one that runs ends itself.
    void cancel(GameThreadRecord* pThread) {
        if (pThread == selfSlot()) {
            exit(nullptr);
        }
        lockState();
        if (pThread->ended) {
            unlockState();
            return;
        }
        if (pThread->started && (pThread->list == nullptr || pThread->list == &sWorldWaiters)) {
            nebula_fatal("a thread was cancelled while it could run");
        }
        if (pThread->list != nullptr) {
            unlink(pThread);
        }
        pThread->ended = true;
        forget(pThread);
        nebula_host_thread_cancel(pThread->host);
        unlockState();
    }

    void resume(GameThreadRecord* pThread) {
        lockState();
        if (!pThread->started) {
            pThread->started = true;
            wake(pThread);
        }
        unlockState();
    }

    void waitFrame() {
        lockState();
        if (sTestMode) {
            block(&sFrameWaiters);
            unlockState();
            return;
        }
        GameThreadRecord* self = selfSlot();
        passWorld(self);
        unlockState();
        const NebulaHostServices* host = nebula_host();
        host->frame_wait(host->user);
        lockState();
        if (sWorldOwner == nullptr) {
            sWorldOwner = self;
        } else {
            sMainWants = true;
            prepend(&sWorldWaiters, self);
            unlockState();
            nebula_host_thread_wait(self->host);
            lockState();
        }
        unlockState();
    }

    void leaveWorld() {
        if (sTestMode) {
            return;
        }
        lockState();
        GameThreadRecord* self = selfSlot();
        self->world = false;
        passWorld(self);
        unlockState();
    }

    bool isTerminated(const GameThreadRecord* pThread) {
        return pThread->ended;
    }

    bool excludeAudio() {
        if (audioExcludedSlot()) {
            return false;
        }
        nebula_host_lock(NEBULA_HOST_LOCK_AUDIO);
        setAudioExcludedSlot(true);
        return true;
    }

    bool restoreAudio(bool enabled) {
        const bool previous = !audioExcludedSlot();
        if (enabled && audioExcludedSlot()) {
            setAudioExcludedSlot(false);
            nebula_host_unlock(NEBULA_HOST_LOCK_AUDIO);
        } else if (!enabled && !audioExcludedSlot()) {
            nebula_host_lock(NEBULA_HOST_LOCK_AUDIO);
            setAudioExcludedSlot(true);
        }
        return previous;
    }

    void enterAudio() {
        // In test mode this runs on the thread stepping the clock: its record stands aside.
        // The output device may change its thread (SDL's does when it reopens).
        void** const ppSlots = nebula_host()->thread_slots(nebula_host()->user);
        if (ppSlots[3] == nullptr && !sTestMode) {
            ppSlots[3] = nebula_host_thread_adopt();
        }
        if (!sTestMode) {
            sAudioThread.host = static_cast< NebulaHostThread* >(ppSlots[3]);
        }
        sAudioThread.outer = selfSlot();
        setSelfSlot(&sAudioThread);
        excludeAudio();
    }

    void leaveAudio() {
        restoreAudio(true);
        setSelfSlot(sAudioThread.outer);
    }
}  // namespace GameThreads

extern "C" {
// The main thread, the host thread this runs on, with the world.
void nebula_threads_init(void) {
    GameThreadRecord* thread = &sMainThread;
    thread->host = nebula_host_thread_adopt();
    thread->world = true;
    thread->started = true;
    setSelfSlot(thread);
    sWorldOwner = thread;
    sThreads = thread;
    const NebulaHostServices* host = nebula_host();
    sTestMode = host != nullptr && host->test_frame_step != nullptr;
}

// A diagnostic for the host's reports: every thread's state. A minidump has their stacks: they are host threads.
void nebula_threads_dump(void) {
    nebula_log("threads (current %p, world %p):\n", static_cast< void* >(selfSlot()), static_cast< void* >(sWorldOwner));
    for (const GameThreadRecord* t = sThreads; t != nullptr; t = t->all) {
        nebula_log("  %p entry %p started %d ended %d world %d sleeps on %p\n", static_cast< const void* >(t), reinterpret_cast< const void* >(t->entry),
                   t->started ? 1 : 0, t->ended ? 1 : 0, t->world ? 1 : 0, static_cast< const void* >(t->list));
    }
}
}  // extern "C"
