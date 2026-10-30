// The game's thread kernel (Game/System/NebulaKernel.hpp). Its scheduling rules are the console OS's, which the game's timing
// depends on: priority 0 runs first, a thread that becomes ready runs after the ones already ready at its priority,
// a wake-up or a priority change that readies a higher-priority thread switches to it at once (unless the scheduler
// is disabled), a thread waiting for a mutex lends its priority to the owner, and a suspended thread is never ready.
// Switching is a stack switch on the simulation thread (os_native.c, context_switch.c).
#include "Game/System/NebulaKernel.hpp"

#include <stdio.h>

#include "nebula_kernel.h"
#include "nebula_sdk.h"

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
extern "C" void __sanitizer_start_switch_fiber(void** fake_stack_save, const void* bottom, size_t size);
extern "C" void __sanitizer_finish_switch_fiber(void* fake_stack_save, const void** bottom_old, size_t* size_old);
#define NEBULA_ASAN_START(save, bottom, size) __sanitizer_start_switch_fiber((save), (bottom), (size))
#define NEBULA_ASAN_FINISH(save) __sanitizer_finish_switch_fiber((save), nullptr, nullptr)
#define NEBULA_NO_ASAN __attribute__((no_sanitize("address")))
#endif
#endif
#ifndef NEBULA_ASAN_START
#define NEBULA_ASAN_START(save, bottom, size) ((void)0)
#define NEBULA_ASAN_FINISH(save) ((void)0)
#define NEBULA_NO_ASAN
#endif

namespace {
    const int32_t kPriorities = 32;
    const uint32_t kStackCanary = 0xDEADBABEu;

    // Scheduler state: touched only on the simulation thread.
    NebulaThreadQueue sRunQueue[kPriorities];
    uint32_t sRunQueueBits;  // bit 31 - p: a thread of priority p is ready
    bool sRunQueueHint;      // something may outrank the running thread
    int32_t sReschedule;     // > 0: the scheduler is disabled
    NebulaThread sBootThread;
    NebulaThread* sCurrent;
    NebulaThreadQueue sActive;  // every thread not yet freed, by linkActive
    bool sIdling;               // no thread runs: the idle wait delivers host events
    void* sIdleSp;              // the stack pointer saved by a switch made from the idle wait
    NebulaThread* sReleasing;   // an exited thread whose stack is freed by the next switch
    uint64_t sSwitches;

    // Host events: host threads set bits in sPending; the simulation thread delivers them at scheduling points.
    uint32_t sPending;
    uint32_t sEnabledEvents = 1u << NEBULA_EVENT_TIMER;  // device events wait until their handler is set and enabled
    int32_t sSleeping;                                   // the simulation thread sleeps for want of work
    bool sInterruptsEnabled;
    NebulaKernel::EventHandler sHandlers[NEBULA_EVENT_COUNT];

    NebulaAlarm* sAlarmHead;
    NebulaAlarm* sAlarmTail;

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

    // No thread can run: deliver host events, sleeping until one arrives, until a thread is ready.
    void idle() {
        while (sRunQueueBits == 0) {
            sInterruptsEnabled = true;
            deliver();
            sInterruptsEnabled = false;
            if (sRunQueueBits != 0) {
                break;
            }
            __atomic_store_n(&sSleeping, 1, __ATOMIC_SEQ_CST);
            if ((__atomic_load_n(&sPending, __ATOMIC_SEQ_CST) & sEnabledEvents) == 0) {
                nebula_sleep_for_work();
            }
            __atomic_store_n(&sSleeping, 0, __ATOMIC_SEQ_CST);
        }
    }

    void releaseStack(NebulaThread* pThread) {
        if (pThread == sCurrent) {
            // It still runs on its stack until the switch away.
            if (sReleasing != nullptr) {
                nebula_thread_stack_release(sReleasing);
            }
            sReleasing = pThread;
        } else {
            nebula_thread_stack_release(pThread);
        }
    }

    void checkStackDepth(const NebulaThread* pThread, const void* pSp) {
        static bool sWarned;
        const uint8_t* const sp = static_cast< const uint8_t* >(pSp);
        if (pThread->stackEnd != nullptr && sp - pThread->stackEnd < 0x2000 && !sWarned) {
            sWarned = true;
            nebula_log("thread %p is within %d bytes of its stack's end\n", static_cast< const void* >(pThread),
                       static_cast< int >(sp - pThread->stackEnd));
        }
    }

    void switchTo(NebulaThread* pFrom, NebulaThread* pTo) {
        if (pFrom != nullptr) {
            checkStackDepth(pFrom, __builtin_frame_address(0));
        }
        sSwitches++;
        void* fake = nullptr;
        NEBULA_ASAN_START(&fake, pTo->stackEnd, static_cast< size_t >(pTo->stackTop - pTo->stackEnd));
        nebula_switch_stack(pFrom != nullptr ? &pFrom->nativeSp : &sIdleSp, pTo->nativeSp);
        NEBULA_ASAN_FINISH(fake);
        // Running again, on another thread's switch: an exited thread that switch left is off its stack now.
        if (sReleasing != nullptr && sReleasing != pFrom) {
            nebula_thread_stack_release(sReleasing);
            sReleasing = nullptr;
        }
    }

    void selectThread(bool yield) {
        if (sReschedule > 0 || sIdling) {
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
        releaseStack(pThread);
        unlockAllMutexes(pThread);
        pThread->joinQueue.wakeup();
    }

    struct ThreadStart {
        void* (*entry)(void*);
        void* arg;
    };

    // A new thread starts with host events enabled and ends in exit with its function's result.
    void threadMain(void* pArg) {
        NEBULA_ASAN_FINISH(nullptr);
        const ThreadStart start = *static_cast< const ThreadStart* >(pArg);
        NebulaKernel::enableInterrupts();
        NebulaKernel::exit(start.entry(start.arg));
        nebula_fatal("a thread returned from its exit");
    }

    // Alarms, earliest first; the platform's timer raises NEBULA_EVENT_TIMER at the head's time.
    void armTimer(const NebulaAlarm* pAlarm) {
        nebula_timer_arm(pAlarm->fire);
    }

    void insertAlarm(NebulaAlarm* pAlarm, int64_t fire, NebulaAlarmHandler pHandler) {
        if (pAlarm->period > 0) {
            const int64_t now = nebula_time();
            fire = pAlarm->start;
            if (pAlarm->start < now) {
                fire += pAlarm->period * ((now - pAlarm->start) / pAlarm->period + 1);
            }
        }
        pAlarm->handler = pHandler;
        pAlarm->fire = fire;
        for (NebulaAlarm* next = sAlarmHead; next != nullptr; next = next->next) {
            if (next->fire <= fire) {
                continue;
            }
            pAlarm->prev = next->prev;
            next->prev = pAlarm;
            pAlarm->next = next;
            NebulaAlarm* prev = pAlarm->prev;
            if (prev != nullptr) {
                prev->next = pAlarm;
            } else {
                sAlarmHead = pAlarm;
                armTimer(pAlarm);
            }
            return;
        }
        pAlarm->next = nullptr;
        NebulaAlarm* prev = sAlarmTail;
        sAlarmTail = pAlarm;
        pAlarm->prev = prev;
        if (prev != nullptr) {
            prev->next = pAlarm;
        } else {
            sAlarmHead = sAlarmTail = pAlarm;
            armTimer(pAlarm);
        }
    }

    void onTimer() {
        NebulaAlarm* alarm = sAlarmHead;
        if (alarm == nullptr) {
            return;
        }
        if (nebula_time() < alarm->fire) {
            armTimer(alarm);
            return;
        }
        NebulaAlarm* next = alarm->next;
        sAlarmHead = next;
        if (next == nullptr) {
            sAlarmTail = nullptr;
        } else {
            next->prev = nullptr;
        }
        const NebulaAlarmHandler handler = alarm->handler;
        alarm->handler = nullptr;
        if (alarm->period > 0) {
            insertAlarm(alarm, 0, handler);
        }
        if (sAlarmHead != nullptr) {
            armTimer(sAlarmHead);
        }
        NebulaKernel::disableScheduler();
        handler(alarm);
        NebulaKernel::enableScheduler();
        reschedule();
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
            if (event == NEBULA_EVENT_TIMER) {
                sInterruptsEnabled = false;
                onTimer();
                sInterruptsEnabled = true;
                continue;
            }
            const NebulaKernel::EventHandler handler = sHandlers[event];
            if (handler != nullptr) {
                sInterruptsEnabled = false;
                NebulaKernel::disableScheduler();
                handler();
                NebulaKernel::enableScheduler();
                reschedule();
                sInterruptsEnabled = true;
            }
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

void NebulaMutex::init() {
    waiters.init();
    owner = nullptr;
    count = 0;
}

void NebulaMutex::lock() {
    const bool enabled = NebulaKernel::disableInterrupts();
    NebulaThread* current = sCurrent;
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
        current->mutex = this;
        promote(owner, current->priority);
        waiters.sleep();
        current->mutex = nullptr;
    }
    NebulaKernel::restoreInterrupts(enabled);
}

void NebulaMutex::unlock() {
    const bool enabled = NebulaKernel::disableInterrupts();
    NebulaThread* current = sCurrent;
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

void NebulaAlarm::init() {
    handler = nullptr;
    tag = 0;
}

void NebulaAlarm::set(int64_t ticks, NebulaAlarmHandler pHandler) {
    const bool enabled = NebulaKernel::disableInterrupts();
    period = 0;
    insertAlarm(this, nebula_time() + ticks, pHandler);
    NebulaKernel::restoreInterrupts(enabled);
}

void NebulaAlarm::setPeriodic(int64_t startTime, int64_t interval, NebulaAlarmHandler pHandler) {
    const bool enabled = NebulaKernel::disableInterrupts();
    period = interval;
    start = startTime;
    insertAlarm(this, 0, pHandler);
    NebulaKernel::restoreInterrupts(enabled);
}

void NebulaAlarm::cancel() {
    const bool enabled = NebulaKernel::disableInterrupts();
    if (handler == nullptr) {
        NebulaKernel::restoreInterrupts(enabled);
        return;
    }
    NebulaAlarm* following = next;
    if (following == nullptr) {
        sAlarmTail = prev;
    } else {
        following->prev = prev;
    }
    if (prev != nullptr) {
        prev->next = following;
    } else {
        sAlarmHead = following;
        if (following != nullptr) {
            armTimer(following);
        }
    }
    handler = nullptr;
    NebulaKernel::restoreInterrupts(enabled);
}

namespace NebulaKernel {
    int64_t time() {
        return nebula_time();
    }

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
        // A native stack from the platform's pool: x64 frames are larger than the console's the game sized for.
        uint32_t size;
        uint8_t* top = static_cast< uint8_t* >(nebula_thread_stack(pThread, stackSize, &size));
        const uintptr_t aligned = reinterpret_cast< uintptr_t >(top) & ~static_cast< uintptr_t >(15);
        uint8_t* record = reinterpret_cast< uint8_t* >(aligned) - 32;
        ThreadStart* start = reinterpret_cast< ThreadStart* >(record);
        start->entry = pEntry;
        start->arg = pArg;
        pThread->nativeSp = nebula_prepare_stack(record, size - 48, threadMain, start);
        pThread->stackTop = top;
        pThread->stackEnd = top - size;
        *reinterpret_cast< uint32_t* >(pThread->stackEnd) = kStackCanary;
        const bool enabled = disableInterrupts();
        enqueueTail(&sActive, pThread, &NebulaThread::linkActive);
        restoreInterrupts(enabled);
        return true;
    }

    NebulaThread* current() {
        return sCurrent;
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
        if (!sInterruptsEnabled) {
            return false;
        }
        if (workPending()) {
            deliver();
        }
        sInterruptsEnabled = false;
        return true;
    }

    bool enableInterrupts() {
        if (sInterruptsEnabled) {
            return true;
        }
        sInterruptsEnabled = true;
        return workPending() ? deliverFromDisabled() : false;
    }

    bool restoreInterrupts(bool enabled) {
        const bool previous = sInterruptsEnabled;
        if (!enabled) {
            if (previous && workPending()) {
                deliver();
            }
            sInterruptsEnabled = false;
            return previous;
        }
        if (previous) {
            return true;
        }
        sInterruptsEnabled = true;
        return workPending() ? deliverFromDisabled() : false;
    }

    // A spin on another thread's work makes no kernel call: what is pending is delivered here (which may switch to the
    // woken thread), and with nothing pending the simulation thread sleeps until a host thread raises something. The
    // frame clock raises every retrace, so a wait on state a host thread writes without raising lasts at most one.
    void poll() {
        if (!sInterruptsEnabled) {
            return;
        }
        if (!workPending()) {
            __atomic_store_n(&sSleeping, 1, __ATOMIC_SEQ_CST);
            if ((__atomic_load_n(&sPending, __ATOMIC_SEQ_CST) & sEnabledEvents) == 0) {
                nebula_sleep_for_work();
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

// The boot thread: the game's main thread, at priority 16, on the boot stack it already runs on.
void nebula_kernel_init(void* bootStackTop, void* bootStackEnd) {
    NebulaThread* thread = &sBootThread;
    thread->state = NebulaThreadState_Running;
    thread->detached = 1;
    thread->priority = thread->base = 16;
    thread->suspend = 0;
    thread->value = reinterpret_cast< void* >(-1);
    thread->mutex = nullptr;
    thread->joinQueue.init();
    thread->mutexes.head = thread->mutexes.tail = nullptr;
    thread->stackTop = static_cast< uint8_t* >(bootStackTop);
    thread->stackEnd = static_cast< uint8_t* >(bootStackEnd);
    *reinterpret_cast< uint32_t* >(thread->stackEnd) = kStackCanary;
    sCurrent = thread;
    sRunQueueBits = 0;
    sRunQueueHint = false;
    for (int32_t priority = 0; priority < kPriorities; priority++) {
        sRunQueue[priority].init();
    }
    sActive.init();
    enqueueTail(&sActive, thread, &NebulaThread::linkActive);
    sReschedule = 0;
    sAlarmHead = sAlarmTail = nullptr;
    sInterruptsEnabled = false;
}

// A diagnostic for the host's reports: every thread's state and the addresses into the module's image found on its
// stack, newest first, as offsets from the image base (symbolize them with the module's PDB). The stack of the thread
// running now is read where it stands; a reading taken while the game runs is approximate.
NEBULA_NO_ASAN void nebula_kernel_dump(void) {
    uintptr_t image, size;
    nebula_image_range(&image, &size);
    nebula_log("threads (current %p, %llu switches since boot):\n", static_cast< void* >(sCurrent),
               static_cast< unsigned long long >(sSwitches));
    for (const NebulaThread* t = sActive.head; t != nullptr; t = t->linkActive.next) {
        char line[1536];
        int at = snprintf(line, sizeof(line), "  %p prio %d/%d state %u suspend %d queue %p mutex %p:",
                          static_cast< const void* >(t), t->priority, t->base, t->state, t->suspend,
                          static_cast< const void* >(t->queue), static_cast< const void* >(t->mutex));
        const uintptr_t* sp = static_cast< const uintptr_t* >(t->nativeSp);
        const uintptr_t* top = reinterpret_cast< const uintptr_t* >(t->stackTop);
        int found = 0;
        const int room = static_cast< int >(sizeof(line)) - 16;
        for (int i = 0; sp != nullptr && sp + i < top && i < 8192 && found < 48 && at < room; i++) {
            const uintptr_t v = sp[i];
            if (v - image < size) {
                at += snprintf(line + at, sizeof(line) - static_cast< size_t >(at), " +%llx",
                               static_cast< unsigned long long >(v - image));
                found++;
            }
        }
        // The deepest the stack has been: its lowest nonzero word (stacks start zeroed).
        const uintptr_t* low = reinterpret_cast< const uintptr_t* >(t->stackEnd) + 1;
        while (low < top && *low == 0) {
            low++;
        }
        const unsigned used =
            static_cast< unsigned >(reinterpret_cast< const uint8_t* >(top) - reinterpret_cast< const uint8_t* >(low));
        const unsigned capacity = static_cast< unsigned >(t->stackTop - t->stackEnd);
        nebula_log("%s\n    stack %u of %u bytes used\n", line, used, capacity);
    }
}
}  // extern "C"
