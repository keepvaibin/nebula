// The game's thread kernel (Game/System/NebulaKernel.hpp). Its scheduling rules are the console OS's, which the game's timing
// depends on: priority 0 runs first, a thread that becomes ready runs after the ones already ready at its priority,
// a wake-up or a priority change that readies a higher-priority thread switches to it at once (unless the scheduler
// is disabled), a thread waiting for a mutex lends its priority to the owner, and a suspended thread is never ready.
// Each thread is a host thread waiting for its turn; switching passes the turn on (os_native.c).
#include "Game/System/NebulaKernel.hpp"

#include <stdio.h>

#include <nebula_game_api.h>

#include "nebula_kernel.h"
#include "nebula_sdk.h"

namespace {
    const int32_t kPriorities = 32;

    // Scheduler state: touched only by the thread whose turn it is.
    NebulaThreadQueue sRunQueue[kPriorities];
    uint32_t sRunQueueBits;  // bit 31 - p: a thread of priority p is ready
    bool sRunQueueHint;      // something may outrank the running thread
    int32_t sReschedule;     // > 0: the scheduler is disabled
    NebulaThread sBootThread;
    NebulaThread* sCurrent;
    NebulaThreadQueue sActive;  // every thread not yet freed, by linkActive
    bool sIdling;               // no thread runs: the idle wait delivers host events
    uint64_t sSwitches;

    // Host events: host threads set bits in sPending; the thread whose turn it is delivers them at scheduling points.
    uint32_t sPending;
    uint32_t sEnabledEvents;  // events wait until their handler is set and enabled
    int32_t sSleeping;                                   // the thread whose turn it is sleeps for want of work
    bool sInterruptsEnabled;  // read by the audio thread
    // The audio thread: running, and which thread this is.
    int32_t sAudioRunning;
    thread_local bool tAudio;
    NebulaThread sAudioThread;
    NebulaKernel::EventHandler sHandlers[NEBULA_EVENT_COUNT];

    // Host events off for the thread whose turn it is, which also keeps the audio thread out (it stands aside if that one is
    // in: each announces itself, then looks at the other).
    void interruptsOff() {
        __atomic_store_n(&sInterruptsEnabled, false, __ATOMIC_SEQ_CST);
        while (__atomic_load_n(&sAudioRunning, __ATOMIC_SEQ_CST) != 0) {
            __atomic_store_n(&sInterruptsEnabled, true, __ATOMIC_SEQ_CST);
            while (__atomic_load_n(&sAudioRunning, __ATOMIC_SEQ_CST) != 0) {
                nebula_host_thread_yield();
            }
            __atomic_store_n(&sInterruptsEnabled, false, __ATOMIC_SEQ_CST);
        }
    }

    void interruptsOn() {
        __atomic_store_n(&sInterruptsEnabled, true, __ATOMIC_SEQ_CST);
    }

    int32_t highestReady() {
        return sRunQueueBits != 0 ? __builtin_clz(sRunQueueBits) : kPriorities;
    }

    // Thread queues, linked through `link` (run and wait queues) or `linkActive`.
    void enqueueTail(NebulaThreadQueue* pQueue, NebulaThread* pThread, NebulaThreadLink NebulaThread::*link) {
        NebulaThread* prev = pQueue->tail;
        if (prev == nullptr) {
            pQueue->head = pThread;
        } else {
            (prev->*link).next = pThread;
        }
        (pThread->*link).prev = prev;
        (pThread->*link).next = nullptr;
        pQueue->tail = pThread;
    }

    // After every thread of the same or higher priority.
    void enqueuePrio(NebulaThreadQueue* pQueue, NebulaThread* pThread) {
        NebulaThread* next = pQueue->head;
        while (next != nullptr && next->priority <= pThread->priority) {
            next = next->link.next;
        }
        if (next == nullptr) {
            enqueueTail(pQueue, pThread, &NebulaThread::link);
            return;
        }
        pThread->link.next = next;
        NebulaThread* prev = next->link.prev;
        next->link.prev = pThread;
        pThread->link.prev = prev;
        if (prev == nullptr) {
            pQueue->head = pThread;
        } else {
            prev->link.next = pThread;
        }
    }

    void dequeueItem(NebulaThreadQueue* pQueue, NebulaThread* pThread, NebulaThreadLink NebulaThread::*link) {
        NebulaThread* next = (pThread->*link).next;
        NebulaThread* prev = (pThread->*link).prev;
        if (next == nullptr) {
            pQueue->tail = prev;
        } else {
            (next->*link).prev = prev;
        }
        if (prev == nullptr) {
            pQueue->head = next;
        } else {
            (prev->*link).next = next;
        }
    }

    NebulaThread* dequeueHead(NebulaThreadQueue* pQueue) {
        NebulaThread* thread = pQueue->head;
        NebulaThread* next = thread->link.next;
        if (next == nullptr) {
            pQueue->tail = nullptr;
        } else {
            next->link.prev = nullptr;
        }
        pQueue->head = next;
        return thread;
    }

    void mutexEnqueueTail(NebulaMutexQueue* pQueue, NebulaMutex* pMutex) {
        NebulaMutex* prev = pQueue->tail;
        if (prev == nullptr) {
            pQueue->head = pMutex;
        } else {
            prev->link.next = pMutex;
        }
        pMutex->link.prev = prev;
        pMutex->link.next = nullptr;
        pQueue->tail = pMutex;
    }

    void mutexDequeue(NebulaMutexQueue* pQueue, NebulaMutex* pMutex) {
        NebulaMutex* next = pMutex->link.next;
        NebulaMutex* prev = pMutex->link.prev;
        if (next == nullptr) {
            pQueue->tail = prev;
        } else {
            next->link.prev = prev;
        }
        if (prev == nullptr) {
            pQueue->head = next;
        } else {
            prev->link.next = next;
        }
    }

    bool isSuspendedCount(int32_t count) {
        return count > 0;
    }

    void setRun(NebulaThread* pThread) {
        pThread->queue = &sRunQueue[pThread->priority];
        enqueueTail(pThread->queue, pThread, &NebulaThread::link);
        sRunQueueBits |= 1u << (31 - pThread->priority);
        sRunQueueHint = true;
    }

    void unsetRun(NebulaThread* pThread) {
        NebulaThreadQueue* queue = pThread->queue;
        dequeueItem(queue, pThread, &NebulaThread::link);
        if (queue->head == nullptr) {
            sRunQueueBits &= ~(1u << (31 - pThread->priority));
        }
        pThread->queue = nullptr;
    }

    // The base priority, raised to the highest waiter's on any mutex the thread owns.
    int32_t effectivePriority(const NebulaThread* pThread) {
        int32_t priority = pThread->base;
        for (const NebulaMutex* mutex = pThread->mutexes.head; mutex != nullptr; mutex = mutex->link.next) {
            const NebulaThread* blocked = mutex->waiters.head;
            if (blocked != nullptr && blocked->priority < priority) {
                priority = blocked->priority;
            }
        }
        return priority;
    }

    // Returns the owner of the mutex the thread waits for, whose priority may change in turn.
    NebulaThread* setEffectivePriority(NebulaThread* pThread, int32_t priority) {
        switch (pThread->state) {
        case NebulaThreadState_Ready:
            unsetRun(pThread);
            pThread->priority = priority;
            setRun(pThread);
            break;
        case NebulaThreadState_Waiting:
            dequeueItem(pThread->queue, pThread, &NebulaThread::link);
            pThread->priority = priority;
            enqueuePrio(pThread->queue, pThread);
            if (pThread->mutex != nullptr) {
                return pThread->mutex->owner;
            }
            break;
        case NebulaThreadState_Running:
            sRunQueueHint = true;
            pThread->priority = priority;
            break;
        }
        return nullptr;
    }

    void updatePriority(NebulaThread* pThread) {
        do {
            if (isSuspendedCount(pThread->suspend)) {
                break;
            }
            const int32_t priority = effectivePriority(pThread);
            if (pThread->priority == priority) {
                break;
            }
            pThread = setEffectivePriority(pThread, priority);
        } while (pThread != nullptr);
    }

    void promote(NebulaThread* pThread, int32_t priority) {
        do {
            if (pThread->suspend > 0 || pThread->priority <= priority) {
                break;
            }
            pThread = setEffectivePriority(pThread, priority);
        } while (pThread != nullptr);
    }

    bool workPending() {
        return __builtin_expect((__atomic_load_n(&sPending, __ATOMIC_RELAXED) & sEnabledEvents) != 0, 0);
    }

    void deliver();

    // Deterministic test mode (the host's test_frame_step): with no thread able to run, the next frame comes, with that
    // frame's sound. Two runs of a build then take the same steps. False outside the mode.
    bool stepClock() {
        const NebulaHostServices* host = nebula_host();
        if (host == nullptr || host->test_frame_step == nullptr) {
            return false;
        }
        host->test_frame_step(host->user);
        return true;
    }

    void sleepForWork() {
        if (!stepClock()) {
            nebula_sleep_for_work();
        }
    }

    // No thread can run: deliver host events, sleeping until one arrives, until a thread is ready.
    void idle() {
        while (sRunQueueBits == 0) {
            interruptsOn();
            deliver();
            interruptsOff();
            if (sRunQueueBits != 0) {
                break;
            }
            interruptsOn();
            __atomic_store_n(&sSleeping, 1, __ATOMIC_SEQ_CST);
            if ((__atomic_load_n(&sPending, __ATOMIC_SEQ_CST) & sEnabledEvents) == 0) {
                sleepForWork();
            }
            __atomic_store_n(&sSleeping, 0, __ATOMIC_SEQ_CST);
            interruptsOff();
        }
    }

    // The turn goes to `pTo`; `pFrom` waits for its next one, or ends if it has exited (its record may be freed from here on).
    void switchTo(NebulaThread* pFrom, NebulaThread* pTo) {
        sSwitches++;
        NebulaHostThread* const self = pFrom->host;
        const bool ended = pFrom->state == NebulaThreadState_Exited || pFrom->state == NebulaThreadState_Free;
        nebula_host_thread_pass(pTo->host);
        if (ended) {
            nebula_host_thread_exit(self);
        }
        nebula_host_thread_wait(self);
    }

    void selectThread(bool yield) {
        if (sReschedule > 0 || sIdling || tAudio) {
            return;
        }
        NebulaThread* current = sCurrent;
        if (current != nullptr && current->state == NebulaThreadState_Running) {
            if (!yield && current->priority <= highestReady()) {
                return;
            }
            current->state = NebulaThreadState_Ready;
            setRun(current);
        }
        if (sRunQueueBits == 0) {
            sCurrent = nullptr;
            sIdling = true;
            idle();
            sIdling = false;
        }
        sRunQueueHint = false;
        const int32_t priority = highestReady();
        NebulaThreadQueue* queue = &sRunQueue[priority];
        NebulaThread* next = dequeueHead(queue);
        if (queue->head == nullptr) {
            sRunQueueBits &= ~(1u << (31 - priority));
        }
        next->queue = nullptr;
        next->state = NebulaThreadState_Running;
        sCurrent = next;
        if (next != current) {
            switchTo(current, next);
        }
    }

    void reschedule() {
        if (sRunQueueHint) {
            selectThread(false);
        }
    }

    void unlockAllMutexes(NebulaThread* pThread) {
        while (pThread->mutexes.head != nullptr) {
            NebulaMutex* mutex = pThread->mutexes.head;
            mutexDequeue(&pThread->mutexes, mutex);
            mutex->count = 0;
            mutex->owner = nullptr;
            mutex->waiters.wakeup();
        }
    }

    // The end of a thread: exited (its value kept for a join) or freed (detached).
    void finish(NebulaThread* pThread) {
        if (pThread->detached) {
            dequeueItem(&sActive, pThread, &NebulaThread::linkActive);
            pThread->state = NebulaThreadState_Free;
        } else {
            pThread->state = NebulaThreadState_Exited;
        }
        if (pThread != sCurrent) {
            nebula_host_thread_cancel(pThread->host);
        }
        unlockAllMutexes(pThread);
        pThread->joinQueue.wakeup();
    }

    // A new thread starts with host events enabled and ends in exit with its function's result.
    void threadMain(void* pArg) {
        NebulaThread* const self = static_cast< NebulaThread* >(pArg);
        NebulaKernel::enableInterrupts();
        NebulaKernel::exit(self->entry(self->arg));
        nebula_fatal("a thread returned from its exit");
    }

    // Runs what is pending, one event at a time, in event order; called with host events enabled. A handler runs
    // with them disabled and the scheduler disabled around it, then the kernel reschedules (which may switch threads;
    // this resumes when the interrupted thread runs again).
    void deliver() {
        for (;;) {
            const uint32_t deliverable = __atomic_load_n(&sPending, __ATOMIC_ACQUIRE) & sEnabledEvents;
            if (deliverable == 0) {
                return;
            }
            const int32_t event = __builtin_ctz(deliverable);
            __atomic_fetch_and(&sPending, ~(1u << event), __ATOMIC_SEQ_CST);
            const NebulaKernel::EventHandler handler = sHandlers[event];
            interruptsOff();
            if (handler != nullptr) {
                NebulaKernel::disableScheduler();
                handler();
                NebulaKernel::enableScheduler();
            }
            reschedule();
            interruptsOn();
        }
    }

    // Events enabled again with work pending: the delivery, then the previous state (disabled).
    __attribute__((noinline)) bool deliverFromDisabled() {
        deliver();
        return false;
    }
}  // namespace

void NebulaThreadQueue::init() {
    head = tail = nullptr;
}

void NebulaThreadQueue::sleep() {
    if (tAudio) {
        // Its callers look again.
        NebulaKernel::leaveAudio();
        nebula_host_thread_yield();
        NebulaKernel::enterAudio();
        return;
    }
    const bool enabled = NebulaKernel::disableInterrupts();
    NebulaThread* current = sCurrent;
    current->state = NebulaThreadState_Waiting;
    current->queue = this;
    enqueuePrio(this, current);
    sRunQueueHint = true;
    reschedule();
    NebulaKernel::restoreInterrupts(enabled);
}

void NebulaThreadQueue::wakeup() {
    const bool enabled = NebulaKernel::disableInterrupts();
    while (head != nullptr) {
        NebulaThread* thread = dequeueHead(this);
        thread->state = NebulaThreadState_Ready;
        if (!isSuspendedCount(thread->suspend)) {
            setRun(thread);
        }
    }
    reschedule();
    NebulaKernel::restoreInterrupts(enabled);
}

void NebulaEvent::init() {
    waiters.init();
    signalled = false;
}

void NebulaEvent::wait() {
    const bool enabled = NebulaKernel::disableInterrupts();
    while (!signalled) {
        waiters.sleep();
    }
    signalled = false;
    NebulaKernel::restoreInterrupts(enabled);
}

void NebulaEvent::signal() {
    const bool enabled = NebulaKernel::disableInterrupts();
    signalled = true;
    waiters.wakeup();
    NebulaKernel::restoreInterrupts(enabled);
}

void NebulaMutex::init() {
    waiters.init();
    owner = nullptr;
    count = 0;
}

void NebulaMutex::lock() {
    const bool enabled = NebulaKernel::disableInterrupts();
    NebulaThread* current = NebulaKernel::current();
    for (;;) {
        NebulaThread* holder = *static_cast< NebulaThread* volatile* >(&owner);
        if (holder == nullptr) {
            owner = current;
            count++;
            mutexEnqueueTail(&current->mutexes, this);
            break;
        }
        if (holder == current) {
            count++;
            break;
        }
        if (tAudio) {
            waiters.sleep();
            continue;
        }
        current->mutex = this;
        promote(owner, current->priority);
        waiters.sleep();
        current->mutex = nullptr;
    }
    NebulaKernel::restoreInterrupts(enabled);
}

void NebulaMutex::unlock() {
    const bool enabled = NebulaKernel::disableInterrupts();
    NebulaThread* current = NebulaKernel::current();
    if (owner == current && --count == 0) {
        mutexDequeue(&current->mutexes, this);
        owner = nullptr;
        if (current->priority < current->base) {
            current->priority = effectivePriority(current);
        }
        waiters.wakeup();
    }
    NebulaKernel::restoreInterrupts(enabled);
}

void NebulaMessageQueue::init(void** pSlots, int32_t count) {
    senders.init();
    receivers.init();
    slots = pSlots;
    capacity = count;
    first = 0;
    used = 0;
}

bool NebulaMessageQueue::send(void* pMessage, bool block) {
    const bool enabled = NebulaKernel::disableInterrupts();
    while (capacity <= used) {
        if (!block) {
            NebulaKernel::restoreInterrupts(enabled);
            return false;
        }
        senders.sleep();
    }
    slots[(first + used) % capacity] = pMessage;
    used++;
    receivers.wakeup();
    NebulaKernel::restoreInterrupts(enabled);
    return true;
}

bool NebulaMessageQueue::jam(void* pMessage, bool block) {
    const bool enabled = NebulaKernel::disableInterrupts();
    while (capacity <= used) {
        if (!block) {
            NebulaKernel::restoreInterrupts(enabled);
            return false;
        }
        senders.sleep();
    }
    first = (first + capacity - 1) % capacity;
    slots[first] = pMessage;
    used++;
    receivers.wakeup();
    NebulaKernel::restoreInterrupts(enabled);
    return true;
}

bool NebulaMessageQueue::receive(void** ppMessage, bool block) {
    const bool enabled = NebulaKernel::disableInterrupts();
    while (used == 0) {
        if (!block) {
            NebulaKernel::restoreInterrupts(enabled);
            return false;
        }
        receivers.sleep();
    }
    if (ppMessage != nullptr) {
        *ppMessage = slots[first];
    }
    first = (first + 1) % capacity;
    used--;
    senders.wakeup();
    NebulaKernel::restoreInterrupts(enabled);
    return true;
}

namespace NebulaKernel {
    bool create(NebulaThread* pThread, void* (*pEntry)(void*), void* pArg, uint32_t stackSize, int32_t priority,
                bool detached) {
        if (priority < 0 || priority >= kPriorities) {
            return false;
        }
        pThread->state = NebulaThreadState_Ready;
        pThread->detached = detached ? 1 : 0;
        pThread->priority = pThread->base = priority;
        pThread->suspend = 1;
        pThread->value = reinterpret_cast< void* >(-1);
        pThread->queue = nullptr;
        pThread->mutex = nullptr;
        pThread->joinQueue.init();
        pThread->mutexes.head = pThread->mutexes.tail = nullptr;
        pThread->entry = pEntry;
        pThread->arg = pArg;
        pThread->host = nebula_host_thread_create(threadMain, pThread, stackSize);
        const bool enabled = disableInterrupts();
        enqueueTail(&sActive, pThread, &NebulaThread::linkActive);
        restoreInterrupts(enabled);
        return true;
    }

    NebulaThread* current() {
        return tAudio ? &sAudioThread : sCurrent;
    }

    void enterAudio() {
        tAudio = true;
        sAudioThread.state = NebulaThreadState_Running;
        sAudioThread.priority = sAudioThread.base = 0;
        __atomic_store_n(&sAudioRunning, 1, __ATOMIC_SEQ_CST);
        while (!__atomic_load_n(&sInterruptsEnabled, __ATOMIC_SEQ_CST)) {
            nebula_host_thread_yield();
        }
    }

    // What it woke runs at the next scheduling point; the event brings one on.
    void leaveAudio() {
        const bool woke = sRunQueueHint;
        __atomic_store_n(&sAudioRunning, 0, __ATOMIC_SEQ_CST);
        tAudio = false;
        if (woke) {
            nebula_raise_event(NEBULA_EVENT_AUDIO);
        }
    }

    void exit(void* pValue) {
        disableInterrupts();
        NebulaThread* current = sCurrent;
        current->value = pValue;
        finish(current);
        sRunQueueHint = true;
        reschedule();
        nebula_fatal("an exited thread ran again");
    }

    void cancel(NebulaThread* pThread) {
        const bool enabled = disableInterrupts();
        switch (pThread->state) {
        case NebulaThreadState_Ready:
            if (!isSuspendedCount(pThread->suspend)) {
                unsetRun(pThread);
            }
            break;
        case NebulaThreadState_Running:
            sRunQueueHint = true;
            break;
        case NebulaThreadState_Waiting:
            dequeueItem(pThread->queue, pThread, &NebulaThread::link);
            pThread->queue = nullptr;
            if (!isSuspendedCount(pThread->suspend) && pThread->mutex != nullptr) {
                updatePriority(pThread->mutex->owner);
            }
            break;
        default:
            restoreInterrupts(enabled);
            return;
        }
        finish(pThread);
        reschedule();
        restoreInterrupts(enabled);
    }

    void detach(NebulaThread* pThread) {
        const bool enabled = disableInterrupts();
        pThread->detached = 1;
        if (pThread->state == NebulaThreadState_Exited) {
            dequeueItem(&sActive, pThread, &NebulaThread::linkActive);
            pThread->state = NebulaThreadState_Free;
        }
        pThread->joinQueue.wakeup();
        restoreInterrupts(enabled);
    }

    int32_t resume(NebulaThread* pThread) {
        const bool enabled = disableInterrupts();
        const int32_t previous = pThread->suspend--;
        if (pThread->suspend < 0) {
            pThread->suspend = 0;
        } else if (pThread->suspend == 0) {
            switch (pThread->state) {
            case NebulaThreadState_Ready:
                pThread->priority = effectivePriority(pThread);
                setRun(pThread);
                break;
            case NebulaThreadState_Waiting:
                dequeueItem(pThread->queue, pThread, &NebulaThread::link);
                pThread->priority = effectivePriority(pThread);
                enqueuePrio(pThread->queue, pThread);
                if (pThread->mutex != nullptr) {
                    updatePriority(pThread->mutex->owner);
                }
                break;
            }
            reschedule();
        }
        restoreInterrupts(enabled);
        return previous;
    }

    int32_t suspend(NebulaThread* pThread) {
        const bool enabled = disableInterrupts();
        const int32_t previous = pThread->suspend++;
        if (previous == 0) {
            switch (pThread->state) {
            case NebulaThreadState_Running:
                sRunQueueHint = true;
                pThread->state = NebulaThreadState_Ready;
                break;
            case NebulaThreadState_Ready:
                unsetRun(pThread);
                break;
            case NebulaThreadState_Waiting:
                // Last in its queue, below every priority, while suspended.
                dequeueItem(pThread->queue, pThread, &NebulaThread::link);
                pThread->priority = kPriorities;
                enqueueTail(pThread->queue, pThread, &NebulaThread::link);
                if (pThread->mutex != nullptr) {
                    updatePriority(pThread->mutex->owner);
                }
                break;
            }
            reschedule();
        }
        restoreInterrupts(enabled);
        return previous;
    }

    void yield() {
        const bool enabled = disableInterrupts();
        selectThread(true);
        restoreInterrupts(enabled);
    }

    bool setPriority(NebulaThread* pThread, int32_t priority) {
        if (priority < 0 || priority >= kPriorities) {
            return false;
        }
        const bool enabled = disableInterrupts();
        if (pThread->base != priority) {
            pThread->base = priority;
            updatePriority(pThread);
            reschedule();
        }
        restoreInterrupts(enabled);
        return true;
    }

    int32_t priority(const NebulaThread* pThread) {
        return pThread->base;
    }

    bool isSuspended(const NebulaThread* pThread) {
        return pThread->suspend > 0;
    }

    bool isTerminated(const NebulaThread* pThread) {
        return pThread->state == NebulaThreadState_Exited || pThread->state == NebulaThreadState_Free;
    }

    int32_t disableScheduler() {
        const bool enabled = disableInterrupts();
        const int32_t count = sReschedule++;
        restoreInterrupts(enabled);
        return count;
    }

    int32_t enableScheduler() {
        const bool enabled = disableInterrupts();
        const int32_t count = sReschedule--;
        restoreInterrupts(enabled);
        return count;
    }

    // Work that arrived while events were enabled is delivered before they are disabled: it would have come before
    // this point, not at the end of the section that follows.
    bool disableInterrupts() {
        if (tAudio || !sInterruptsEnabled) {
            return false;
        }
        if (workPending()) {
            deliver();
        }
        interruptsOff();
        return true;
    }

    bool enableInterrupts() {
        if (sInterruptsEnabled) {
            return true;
        }
        interruptsOn();
        return workPending() ? deliverFromDisabled() : false;
    }

    bool restoreInterrupts(bool enabled) {
        if (tAudio) {
            return false;
        }
        const bool previous = sInterruptsEnabled;
        if (!enabled) {
            if (previous && workPending()) {
                deliver();
            }
            if (previous) {
                interruptsOff();
            }
            return previous;
        }
        if (previous) {
            return true;
        }
        interruptsOn();
        return workPending() ? deliverFromDisabled() : false;
    }

    // A spin on another thread's work makes no kernel call: what is pending is delivered here (which may switch to the
    // woken thread), and with nothing pending this thread sleeps until a host thread raises something. The
    // frame clock raises every retrace, so a wait on state a host thread writes without raising lasts at most one.
    void poll() {
        if (tAudio || !sInterruptsEnabled) {
            return;
        }
        if (!workPending()) {
            __atomic_store_n(&sSleeping, 1, __ATOMIC_SEQ_CST);
            if ((__atomic_load_n(&sPending, __ATOMIC_SEQ_CST) & sEnabledEvents) == 0) {
                sleepForWork();
            }
            __atomic_store_n(&sSleeping, 0, __ATOMIC_SEQ_CST);
        }
        if (workPending()) {
            deliver();
        }
    }

    void setEventHandler(int32_t event, EventHandler pHandler) {
        sHandlers[event] = pHandler;
    }

    // An event pending while disabled is delivered once enabled, at the next scheduling point.
    void enableEvent(int32_t event) {
        const bool enabled = disableInterrupts();
        sEnabledEvents |= 1u << event;
        restoreInterrupts(enabled);
    }
}  // namespace NebulaKernel

extern "C" {
void nebula_raise_event(int event) {
    __atomic_fetch_or(&sPending, 1u << event, __ATOMIC_SEQ_CST);
    if (__atomic_load_n(&sSleeping, __ATOMIC_SEQ_CST) != 0) {
        nebula_wake_simulation();
    }
}

void nebula_note_event(int event) {
    __atomic_fetch_or(&sPending, 1u << event, __ATOMIC_SEQ_CST);
}

void nebula_set_event_handler(int event, void (*handler)(void)) {
    NebulaKernel::setEventHandler(event, handler);
}

void nebula_enable_event(int event) {
    NebulaKernel::enableEvent(event);
}

int nebula_interrupts_disable(void) {
    return NebulaKernel::disableInterrupts() ? 1 : 0;
}

int nebula_interrupts_restore(int enabled) {
    return NebulaKernel::restoreInterrupts(enabled != 0) ? 1 : 0;
}

int32_t nebula_scheduler_disable(void) {
    return NebulaKernel::disableScheduler();
}

int32_t nebula_scheduler_enable(void) {
    return NebulaKernel::enableScheduler();
}

void nebula_kernel_poll(void) {
    NebulaKernel::poll();
}

// The boot thread: the game's main thread, at priority 16, the host thread it already runs on.
void nebula_kernel_init(void) {
    NebulaThread* thread = &sBootThread;
    thread->host = nebula_host_thread_adopt();
    thread->state = NebulaThreadState_Running;
    thread->detached = 1;
    thread->priority = thread->base = 16;
    thread->suspend = 0;
    thread->value = reinterpret_cast< void* >(-1);
    thread->mutex = nullptr;
    thread->joinQueue.init();
    thread->mutexes.head = thread->mutexes.tail = nullptr;
    sCurrent = thread;
    sRunQueueBits = 0;
    sRunQueueHint = false;
    for (int32_t priority = 0; priority < kPriorities; priority++) {
        sRunQueue[priority].init();
    }
    sActive.init();
    enqueueTail(&sActive, thread, &NebulaThread::linkActive);
    sReschedule = 0;
    sInterruptsEnabled = false;
}

// A diagnostic for the host's reports: every thread's state. A minidump has their stacks: they are host threads.
void nebula_kernel_dump(void) {
    nebula_log("threads (current %p, %llu switches since boot):\n", static_cast< void* >(sCurrent),
               static_cast< unsigned long long >(sSwitches));
    for (const NebulaThread* t = sActive.head; t != nullptr; t = t->linkActive.next) {
        nebula_log("  %p prio %d/%d state %u suspend %d queue %p mutex %p\n", static_cast< const void* >(t), t->priority,
                   t->base, t->state, t->suspend, static_cast< const void* >(t->queue),
                   static_cast< const void* >(t->mutex));
    }
}
}  // extern "C"
