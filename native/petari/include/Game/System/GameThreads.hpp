#pragma once

// The game's synchronisation (docs/NATIVE_PORT.md, "Platform boundary", Threads). The game runs on one thread; the host's audio
// clock calls in on another, between enterAudio and leaveAudio, when no section keeps it out (excludeAudio).

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

// A ring of messages between the game and the audio clock; a full one refuses, an empty one has nothing.
struct GameMessageQueue {
    void** slots;
    int32_t capacity;
    int32_t first;
    int32_t used;

    void init(void** pSlots, int32_t count);
    bool send(void* pMessage);
    bool receive(void** ppMessage);
    bool isFull() const { return used >= capacity; }
};

struct GameThreadRecord {
    NebulaHostThread* host;
    GameWaitList* list;  // the list it sleeps on
    GameThreadRecord* next;
    GameThreadRecord* prev;
    GameThreadRecord* outer;  // the audio thread's: the record it stands in for
};

namespace GameThreads {
    // Until the next frame is due.
    void waitFrame();

    // A section the audio thread stays out of. excludeAudio returns the previous state for restoreAudio.
    bool excludeAudio();
    bool restoreAudio(bool enabled);

    // The audio thread (JASDriver, on the host's audio clock) runs between these, when no thread is in such a section, and none
    // enters one until it leaves.
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
