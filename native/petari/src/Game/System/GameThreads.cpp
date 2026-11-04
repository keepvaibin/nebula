// The game's synchronisation (Game/System/GameThreads.hpp): a mutex, a ring of messages and the section the audio thread stays out of.
#include "Game/System/GameThreads.hpp"

#include <nebula_game_api.h>

#include "nebula_threads.h"
#include "nebula_module.h"

namespace {
    // This thread's record and whether it is in the audio section: the host keeps them (slots 1 and 2).
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

    // Deterministic test mode (the host's test_frame_step): the sound runs on the thread stepping the clock, at the frame's wait.
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

    // This thread sleeps on `pList` until woken, with the state lock held on entry and exit. The audio section ends for the
    // wait: a section never lasts across a sleep.
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
        unlockState();
        nebula_host_thread_wait(self->host);
        if (inSection) {
            nebula_host_lock(NEBULA_HOST_LOCK_AUDIO);
            setAudioExcludedSlot(true);
        }
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

void GameMessageQueue::init(void** pSlots, int32_t count) {
    slots = pSlots;
    capacity = count;
    first = 0;
    used = 0;
}

bool GameMessageQueue::send(void* pMessage) {
    lockState();
    const bool room = used < capacity;
    if (room) {
        slots[(first + used) % capacity] = pMessage;
        used++;
    }
    unlockState();
    return room;
}

bool GameMessageQueue::receive(void** ppMessage) {
    lockState();
    const bool any = used != 0;
    if (any) {
        *ppMessage = slots[first];
        first = (first + 1) % capacity;
        used--;
    }
    unlockState();
    return any;
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
// The game's thread, the host thread this runs on.
void nebula_threads_init(void) {
    sMainThread.host = nebula_host_thread_adopt();
    setSelfSlot(&sMainThread);
    const NebulaHostServices* host = nebula_host();
    sTestMode = host != nullptr && host->test_frame_step != nullptr;
}

// A diagnostic for the host's reports. A minidump has the stacks.
void nebula_threads_dump(void) {
    nebula_log("threads: current %p, game %p, audio %p\n", static_cast< void* >(selfSlot()), static_cast< void* >(&sMainThread),
               static_cast< void* >(&sAudioThread));
}
}  // extern "C"
