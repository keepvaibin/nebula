#pragma once

// The game's threads and their synchronisation (docs/NATIVE_PORT.md, "Platform boundary", Threads). Every game
// thread runs on its own native stack on the simulation thread, one at a time: the highest-priority ready thread runs
// (0 is the highest), equal priorities in the order they became ready, and a running thread keeps running until it
// blocks, yields, or a higher-priority thread becomes ready. Host events (a finished disc read, the frame clock, an
// audio block, a timer) reach the game only at the kernel's scheduling points: entering or leaving an interrupt
// guard, a block, a wake, a yield and NebulaKernel::poll. So code that makes no kernel call cannot be interrupted, and
// a section guarded against host events only needs a guard when it makes one.

#include <stdint.h>

struct NebulaThread;
struct NebulaMutex;

// Threads waiting for something (a wake-up, a message, a mutex), highest priority first.
struct NebulaThreadQueue {
    NebulaThread* head;
    NebulaThread* tail;

    void init();
    // The running thread waits here until wakeup().
    void sleep();
    // Every waiting thread becomes ready.
    void wakeup();
};

struct NebulaThreadLink {
    NebulaThread* next;
    NebulaThread* prev;
};

struct NebulaMutexQueue {
    NebulaMutex* head;
    NebulaMutex* tail;
};

struct NebulaMutexLink {
    NebulaMutex* next;
    NebulaMutex* prev;
};

// A recursive mutex. A thread waiting for it lends its priority to the owner until the owner unlocks.
struct NebulaMutex {
    NebulaThreadQueue waiters;
    NebulaThread* owner;
    int32_t count;
    NebulaMutexLink link;

    void init();
    void lock();
    void unlock();
};

// A fixed ring of messages: send appends, jam prepends, receive takes the first; a full or empty queue blocks the
// caller or fails, as asked.
struct NebulaMessageQueue {
    NebulaThreadQueue senders;
    NebulaThreadQueue receivers;
    void** slots;
    int32_t capacity;
    int32_t first;
    int32_t used;

    void init(void** pSlots, int32_t count);
    bool send(void* pMessage, bool block);
    bool jam(void* pMessage, bool block);
    bool receive(void** ppMessage, bool block);
    bool isFull() const { return used >= capacity; }
};

enum NebulaThreadState {
    NebulaThreadState_Free = 0,
    NebulaThreadState_Ready = 1,
    NebulaThreadState_Running = 2,
    NebulaThreadState_Waiting = 4,
    NebulaThreadState_Exited = 8,
};

struct NebulaThread {
    void* nativeSp;  // the stack pointer while switched out
    uint16_t state;
    uint16_t detached;
    int32_t suspend;
    int32_t priority;  // effective: base, or a waiter's on a mutex it owns
    int32_t base;
    void* value;  // the exit value
    NebulaThreadQueue* queue;
    NebulaThreadLink link;
    NebulaThreadQueue joinQueue;
    NebulaMutex* mutex;  // the mutex it waits for
    NebulaMutexQueue mutexes;  // the mutexes it owns
    NebulaThreadLink linkActive;
    uint8_t* stackTop;
    uint8_t* stackEnd;
};

struct NebulaAlarm;
typedef void (*NebulaAlarmHandler)(NebulaAlarm* pAlarm);

// A timer: its handler runs as a host event when the time base passes its time, once or every period.
struct NebulaAlarm {
    NebulaAlarmHandler handler;
    uint32_t tag;
    int64_t fire;
    NebulaAlarm* prev;
    NebulaAlarm* next;
    int64_t period;
    int64_t start;
    void* userData;

    void init();
    // `ticks` time-base ticks from now.
    void set(int64_t ticks, NebulaAlarmHandler pHandler);
    // At `start` (a time-base time) and every `period` ticks after.
    void setPeriodic(int64_t start, int64_t period, NebulaAlarmHandler pHandler);
    void cancel();
};

namespace NebulaKernel {
    // The time base: 60.75 MHz ticks (OS_TIMER_CLOCK).
    int64_t time();

    bool create(NebulaThread* pThread, void* (*pEntry)(void*), void* pArg, uint32_t stackSize, int32_t priority,
                bool detached);
    NebulaThread* current();
    void exit(void* pValue);
    void cancel(NebulaThread* pThread);
    void detach(NebulaThread* pThread);
    // The previous suspend count.
    int32_t resume(NebulaThread* pThread);
    int32_t suspend(NebulaThread* pThread);
    void yield();
    bool setPriority(NebulaThread* pThread, int32_t priority);
    int32_t priority(const NebulaThread* pThread);
    bool isSuspended(const NebulaThread* pThread);
    bool isTerminated(const NebulaThread* pThread);
    // While disabled (a count), the running thread keeps running whatever becomes ready.
    int32_t disableScheduler();
    int32_t enableScheduler();

    // Host events wait while disabled. disableInterrupts returns the previous state for restoreInterrupts.
    bool disableInterrupts();
    bool enableInterrupts();
    bool restoreInterrupts(bool enabled);
    // A spin on work another thread finishes: delivers what is pending (which may switch to the thread it wakes), or
    // sleeps until a host thread raises something.
    void poll();

    // Host events by number (NEBULA_EVENT_*, nebula_kernel.h): the handler runs with host events and the scheduler
    // disabled, then the kernel reschedules.
    typedef void (*EventHandler)();
    void setEventHandler(int32_t event, EventHandler pHandler);
    void enableEvent(int32_t event);
}  // namespace NebulaKernel

class NebulaInterruptGuard {
public:
    NebulaInterruptGuard() : mEnabled(NebulaKernel::disableInterrupts()) {}
    ~NebulaInterruptGuard() { NebulaKernel::restoreInterrupts(mEnabled); }

private:
    bool mEnabled;
};
