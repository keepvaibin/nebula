// The game's synchronisation (Game/System/GameThreads.hpp): a mutex and the frame wait.
#include "Game/System/GameThreads.hpp"

#include <nebula_game_api.h>

#include "nebula_threads.h"
#include "nebula_module.h"

namespace {
    // This thread's record: the host keeps it (slot 1).
    GameThreadRecord* selfSlot() {
        return static_cast< GameThreadRecord* >(nebula_host()->thread_slots(nebula_host()->user)[1]);
    }

    void setSelfSlot(GameThreadRecord* pRecord) {
        nebula_host()->thread_slots(nebula_host()->user)[1] = pRecord;
    }

    GameThreadRecord sMainThread;

    // Deterministic test mode (the host's test_frame_step): the frame's wait steps the clock.
    bool sTestMode;

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

    void wakeFirst(GameWaitList* pList) {
        GameThreadRecord* thread = pList->head;
        if (thread == nullptr) {
            return;
        }
        pList->head = thread->next;
        if (thread->next != nullptr) {
            thread->next->prev = nullptr;
        } else {
            pList->tail = nullptr;
        }
        thread->list = nullptr;
        nebula_host_thread_pass(thread->host);
    }

    // This thread sleeps on `pList` until woken, with the state lock held on entry and exit.
    void block(GameWaitList* pList) {
        GameThreadRecord* self = selfSlot();
        append(pList, self);
        unlockState();
        nebula_host_thread_wait(self->host);
        lockState();
    }
}  // namespace

void GameMutex::init() {
    waiters.head = waiters.tail = nullptr;
    owner = nullptr;
    count = 0;
}

void GameMutex::lock() {
    lockState();
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
    unlockState();
}

namespace GameThreads {
    void waitFrame() {
        const NebulaHostServices* host = nebula_host();
        if (sTestMode) {
            host->test_frame_step(host->user);
        } else {
            host->frame_wait(host->user);
        }
    }
}  // namespace GameThreads

extern "C" {
// The game's thread, the host thread this runs on.
void nebula_threads_init(void) {
    sMainThread.host = nebula_host_thread_adopt();
    setSelfSlot(&sMainThread);
    const NebulaHostServices* host = nebula_host();
    sTestMode = host != nullptr && host->test_frame_step != nullptr;
}

// A diagnostic for the host's reports. A minidump has the stacks.
void nebula_threads_dump(void) {
    nebula_log("threads: current %p, game %p\n", static_cast< void* >(selfSlot()), static_cast< void* >(&sMainThread));
}
}  // extern "C"
