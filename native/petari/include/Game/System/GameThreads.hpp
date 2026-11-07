#pragma once

// The game's synchronisation (docs/NATIVE_PORT.md, "Platform boundary", Threads). The game runs on one thread. The sound engine
// runs on the host's audio clock and takes no lock: the game talks to it by its command ring (Game/Sound/SoundCommand.hpp).

#include <stdint.h>

struct GameThreadRecord;
struct NebulaHostThread;

struct GameWaitList {
    GameThreadRecord* head;
    GameThreadRecord* tail;
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

struct GameThreadRecord {
    NebulaHostThread* host;
    GameWaitList* list;  // the list it sleeps on
    GameThreadRecord* next;
    GameThreadRecord* prev;
};

namespace GameThreads {
    // Until the next frame is due.
    void waitFrame();
}  // namespace GameThreads
