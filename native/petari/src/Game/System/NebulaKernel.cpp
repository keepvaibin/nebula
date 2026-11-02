// The game's threads and their synchronisation (Game/System/NebulaKernel.hpp): host threads, plain blocking primitives and
// two locks. The world lock: one thread at a time changes the game world, the main thread first when it wants to; a blocked
// thread lets go of it. The interrupt lock: a section the audio thread stays out of.
#include "Game/System/NebulaKernel.hpp"

#include <stdio.h>

#include <nebula_game_api.h>

#include "nebula_kernel.h"
#include "nebula_sdk.h"

namespace {
    thread_local NebulaThread* tSelf;
    thread_local bool tInterruptsOff;

    NebulaThread sMainThread;
    NebulaThread sAudioThread;
    NebulaThread* sThreads;  // every live thread, by `all`

    // All below under the state lock, which is held only to look at them, never across a wait.
    NebulaThread* sWorldOwner;
    NebulaWaitList sWorldWaiters;
    bool sMainWants;

    // Deterministic test mode (the host's test_frame_step): every thread is under the world lock, the sound runs on the thread
    // stepping the clock, and the next frame comes only when no thread can run. Two runs of a build take the same steps.
    bool sTestMode;

    void lockState() {
        nebula_host_lock(NEBULA_HOST_LOCK_STATE);
    }

    void unlockState() {
        nebula_host_unlock(NEBULA_HOST_LOCK_STATE);
    }

    void append(NebulaWaitList* pList, NebulaThread* pThread) {
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

    void prepend(NebulaWaitList* pList, NebulaThread* pThread) {
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

    void unlink(NebulaThread* pThread) {
        NebulaWaitList* list = pThread->list;
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

    void forget(NebulaThread* pThread) {
        for (NebulaThread** p = &sThreads; *p != nullptr; p = &(*p)->all) {
            if (*p == pThread) {
                *p = pThread->all;
                return;
            }
        }
    }

    // A thread that can go on: a world thread when the world is free or its turn comes, others at once.
    void wake(NebulaThread* pThread) {
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

    void wakeAll(NebulaWaitList* pList) {
        while (pList->head != nullptr) {
            NebulaThread* thread = pList->head;
            unlink(thread);
            wake(thread);
        }
    }

    void wakeFirst(NebulaWaitList* pList) {
        if (pList->head != nullptr) {
            NebulaThread* thread = pList->head;
            unlink(thread);
            wake(thread);
        }
    }

    // The next waiter takes the world; true if that is `pSelf`, which then must not wait for it.
    bool passWorld(NebulaThread* pSelf) {
        NebulaThread* next = sWorldWaiters.head;
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
    bool releaseWorld(NebulaThread* pSelf) {
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
            if (passWorld(pSelf)) {
                return true;
            }
        }
        return false;
    }

    // This thread sleeps on `pList` until woken, with the state lock held on entry and exit. A section with host events disabled
    // ends for the wait: the console's sleeping thread let interrupts back on.
    void block(NebulaWaitList* pList) {
        NebulaThread* self = tSelf;
        if (self == &sAudioThread && sTestMode) {
            nebula_fatal("test mode: the audio thread has to wait, and no thread can run while it does");
        }
        append(pList, self);
        const bool interruptsOff = tInterruptsOff;
        if (interruptsOff) {
            tInterruptsOff = false;
            nebula_host_unlock(NEBULA_HOST_LOCK_INTERRUPTS);
        }
        const bool mine = self->world && releaseWorld(self);
        unlockState();
        if (!mine) {
            nebula_host_thread_wait(self->host);
        }
        if (interruptsOff) {
            nebula_host_lock(NEBULA_HOST_LOCK_INTERRUPTS);
            tInterruptsOff = true;
        }
        lockState();
    }

    // A thread other than main lets main have the world at each kernel call (not with host events disabled: main may want that
    // section). With the state lock held.
    void yieldToMain() {
        NebulaThread* self = tSelf;
        if (!sMainWants || tInterruptsOff || self == nullptr || !self->world || self == &sMainThread || sWorldOwner != self) {
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
        NebulaThread* self = static_cast< NebulaThread* >(pArg);
        tSelf = self;
        NebulaKernel::exit(self->entry(self->arg));
    }
}  // namespace

void NebulaEvent::init() {
    waiters.head = waiters.tail = nullptr;
    signalled = false;
}

void NebulaEvent::wait() {
    lockState();
    while (!signalled) {
        block(&waiters);
    }
    signalled = false;
    unlockState();
}

void NebulaEvent::signal() {
    lockState();
    signalled = true;
    wakeAll(&waiters);
    unlockState();
}

void NebulaFrameGate::init() {
    waiters.head = waiters.tail = nullptr;
    frames = 0;
}

void NebulaFrameGate::advance() {
    lockState();
    frames++;
    wakeAll(&waiters);
    unlockState();
}

void NebulaFrameGate::waitNext() {
    lockState();
    const uint32_t seen = frames;
    while (frames == seen) {
        block(&waiters);
    }
    unlockState();
}

void NebulaMutex::init() {
    waiters.head = waiters.tail = nullptr;
    owner = nullptr;
    count = 0;
}

void NebulaMutex::lock() {
    lockState();
    yieldToMain();
    NebulaThread* self = tSelf;
    while (owner != nullptr && owner != self) {
        block(&waiters);
    }
    owner = self;
    count++;
    unlockState();
}

void NebulaMutex::unlock() {
    lockState();
    if (owner == tSelf && --count == 0) {
        owner = nullptr;
        wakeFirst(&waiters);
    }
    yieldToMain();
    unlockState();
}

void NebulaMessageQueue::init(void** pSlots, int32_t count) {
    senders.head = senders.tail = nullptr;
    receivers.head = receivers.tail = nullptr;
    slots = pSlots;
    capacity = count;
    first = 0;
    used = 0;
}

bool NebulaMessageQueue::send(void* pMessage, bool blocks) {
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

bool NebulaMessageQueue::jam(void* pMessage, bool blocks) {
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

bool NebulaMessageQueue::receive(void** ppMessage, bool blocks) {
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

namespace NebulaKernel {
    bool create(NebulaThread* pThread, void* (*pEntry)(void*), void* pArg, uint32_t stackSize) {
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

    NebulaThread* current() {
        return tSelf;
    }

    void exit(void* pValue) {
        lockState();
        NebulaThread* self = tSelf;
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
    void cancel(NebulaThread* pThread) {
        if (pThread == tSelf) {
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

    void resume(NebulaThread* pThread) {
        lockState();
        if (!pThread->started) {
            pThread->started = true;
            wake(pThread);
        }
        unlockState();
    }

    void leaveWorld() {
        if (sTestMode) {
            return;
        }
        lockState();
        NebulaThread* self = tSelf;
        self->world = false;
        passWorld(self);
        unlockState();
    }

    bool isTerminated(const NebulaThread* pThread) {
        return pThread->ended;
    }

    bool disableInterrupts() {
        if (tInterruptsOff) {
            return false;
        }
        nebula_host_lock(NEBULA_HOST_LOCK_INTERRUPTS);
        tInterruptsOff = true;
        return true;
    }

    bool restoreInterrupts(bool enabled) {
        const bool previous = !tInterruptsOff;
        if (enabled && tInterruptsOff) {
            tInterruptsOff = false;
            nebula_host_unlock(NEBULA_HOST_LOCK_INTERRUPTS);
        } else if (!enabled && !tInterruptsOff) {
            nebula_host_lock(NEBULA_HOST_LOCK_INTERRUPTS);
            tInterruptsOff = true;
        }
        return previous;
    }

    void enterAudio() {
        // In test mode this runs on the thread stepping the clock: its record stands aside.
        if (sAudioThread.host == nullptr && !sTestMode) {
            sAudioThread.host = nebula_host_thread_adopt();
        }
        sAudioThread.outer = tSelf;
        tSelf = &sAudioThread;
        disableInterrupts();
    }

    void leaveAudio() {
        restoreInterrupts(true);
        tSelf = sAudioThread.outer;
    }
}  // namespace NebulaKernel

extern "C" {
// The main thread, the host thread this runs on, with the world.
void nebula_kernel_init(void) {
    NebulaThread* thread = &sMainThread;
    thread->host = nebula_host_thread_adopt();
    thread->world = true;
    thread->started = true;
    tSelf = thread;
    sWorldOwner = thread;
    sThreads = thread;
    const NebulaHostServices* host = nebula_host();
    sTestMode = host != nullptr && host->test_frame_step != nullptr;
}

// A diagnostic for the host's reports: every thread's state. A minidump has their stacks: they are host threads.
void nebula_kernel_dump(void) {
    nebula_log("threads (current %p, world %p):\n", static_cast< void* >(tSelf), static_cast< void* >(sWorldOwner));
    for (const NebulaThread* t = sThreads; t != nullptr; t = t->all) {
        nebula_log("  %p started %d sleeps on %p\n", static_cast< const void* >(t), t->started ? 1 : 0,
                   static_cast< const void* >(t->list));
    }
}
}  // extern "C"
