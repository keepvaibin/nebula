#pragma once

// The game's threads and their synchronisation (docs/NATIVE_PORT.md, "Platform boundary", Threads). Every game thread is a host
// thread. One at a time changes the game world (the world lock, the main thread first): a thread lets go of it when it blocks, and
// lets main have it at each kernel call. excludeAudio keeps the audio thread out of a section.

#include <stdint.h>

struct GameThreadRecord;
struct NebulaHostThread;

// Threads asleep until something happens.
struct GameWaitList {
    GameThreadRecord* head;
    GameThreadRecord* tail;
};

// One thread waits until another says so. A signal with nobody waiting is kept for the next wait; each wait takes it.
struct GameEvent {
    GameWaitList waiters;
    bool signalled;

    void init();
    void wait();
    void signal();
    // A thread sits in wait.
    bool isWaiting() const { return waiters.head != nullptr; }
};

// A recursive mutex.
struct GameMutex {
    GameWaitList waiters;
    GameThreadRecord* owner;
    int32_t count;

    void init();
    void lock();
    void unlock();
};

// A fixed ring of messages: send appends, jam prepends, receive takes the first; a full or empty queue blocks the
// caller or fails, as asked.
struct GameMessageQueue {
    GameWaitList senders;
    GameWaitList receivers;
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

struct GameThreadRecord {
    NebulaHostThread* host;
    void* (*entry)(void*);
    void* arg;
    void* value;  // the exit value
    bool world;   // under the world lock
    bool started;
    bool ended;
    GameWaitList* list;  // the list it sleeps on or waits for the world on
    GameThreadRecord* next;
    GameThreadRecord* prev;
    GameThreadRecord* all;
    GameThreadRecord* outer;  // the audio thread's: the record it stands in for
};

namespace GameThreads {
    // The main thread lets go of the world until its next frame is due.
    void waitFrame();
    bool create(GameThreadRecord* pThread, void* (*pEntry)(void*), void* pArg, uint32_t stackSize);
    GameThreadRecord* current();
    void exit(void* pValue);
    void cancel(GameThreadRecord* pThread);
    // A new thread starts.
    void resume(GameThreadRecord* pThread);
    // This thread works off the game world from now on (file loaders): it runs alongside the others, under its mutexes. Test mode
    // keeps it under the world lock, so that two runs take the same steps.
    void leaveWorld();
    bool isTerminated(const GameThreadRecord* pThread);

    // A section the audio thread stays out of. excludeAudio returns the previous state for
    // restoreAudio.
    bool excludeAudio();
    bool restoreAudio(bool enabled);

    // The audio thread (JASDriver, on the host's audio clock) runs between these, when no thread is in such a section, and none enters
    // one until it leaves.
    void enterAudio();
    void leaveAudio();
}  // namespace GameThreads

class GameAudioExclusion {
public:
    GameAudioExclusion() : mEnabled(GameThreads::excludeAudio()) {}
    ~GameAudioExclusion() { GameThreads::restoreAudio(mEnabled); }

private:
    bool mEnabled;
};
