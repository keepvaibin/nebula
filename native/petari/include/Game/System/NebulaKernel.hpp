#pragma once

// The game's threads and their synchronisation (docs/NATIVE_PORT.md, "Platform boundary", Threads). Every game thread is a host
// thread. One at a time changes the game world (the world lock, the main thread first): a thread lets go of it when it blocks, and
// lets main have it at each kernel call. A section with host events disabled keeps the audio thread out.

#include <stdint.h>

struct NebulaThread;
struct NebulaHostThread;

// Threads asleep until something happens.
struct NebulaWaitList {
    NebulaThread* head;
    NebulaThread* tail;
};

// One thread waits until another says so. A signal with nobody waiting is kept for the next wait; each wait takes it.
struct NebulaEvent {
    NebulaWaitList waiters;
    bool signalled;

    void init();
    void wait();
    void signal();
    // A thread sits in wait.
    bool isWaiting() const { return waiters.head != nullptr; }
};

// Threads waiting for the next frame.
struct NebulaFrameGate {
    NebulaWaitList waiters;
    uint32_t frames;

    void init();
    void advance();
    void waitNext();
};

// A recursive mutex.
struct NebulaMutex {
    NebulaWaitList waiters;
    NebulaThread* owner;
    int32_t count;

    void init();
    void lock();
    void unlock();
};

// A fixed ring of messages: send appends, jam prepends, receive takes the first; a full or empty queue blocks the
// caller or fails, as asked.
struct NebulaMessageQueue {
    NebulaWaitList senders;
    NebulaWaitList receivers;
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

struct NebulaThread {
    NebulaHostThread* host;
    void* (*entry)(void*);
    void* arg;
    void* value;  // the exit value
    bool world;   // under the world lock
    bool started;
    bool ended;
    NebulaWaitList* list;  // the list it sleeps on or waits for the world on
    NebulaThread* next;
    NebulaThread* prev;
    NebulaThread* all;
    NebulaThread* outer;  // the audio thread's: the record it stands in for
};

namespace NebulaKernel {
    bool create(NebulaThread* pThread, void* (*pEntry)(void*), void* pArg, uint32_t stackSize);
    NebulaThread* current();
    void exit(void* pValue);
    void cancel(NebulaThread* pThread);
    // A new thread starts.
    void resume(NebulaThread* pThread);
    // This thread works off the game world from now on (file loaders): it runs alongside the others, under its mutexes. Test mode
    // keeps it under the world lock, so that two runs take the same steps.
    void leaveWorld();
    bool isTerminated(const NebulaThread* pThread);

    // disableInterrupts returns the previous state for restoreInterrupts.
    bool disableInterrupts();
    bool restoreInterrupts(bool enabled);

    // The audio thread (JASDriver, on the host's audio clock) runs between these, when no thread is in a section with host events
    // disabled, and none enters one until it leaves: what the console's audio interrupt and high-priority thread were to the game.
    void enterAudio();
    void leaveAudio();
}  // namespace NebulaKernel

class NebulaInterruptGuard {
public:
    NebulaInterruptGuard() : mEnabled(NebulaKernel::disableInterrupts()) {}
    ~NebulaInterruptGuard() { NebulaKernel::restoreInterrupts(mEnabled); }

private:
    bool mEnabled;
};
