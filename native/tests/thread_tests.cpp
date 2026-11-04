// The game's synchronisation (Game/System/GameThreads.hpp): a mutex nests, messages keep their order, a full ring refuses and an
// empty one has nothing.
#include <stdio.h>

#include "nebula_threads.h"
#include "Game/System/GameThreads.hpp"
#include "nebula_module.h"

namespace {
    int sFailures;

    void expect(const char* pWhat, long long got, long long want) {
        if (got != want) {
            printf("FAIL %s: %lld (want %lld)
", pWhat, got, want);
            sFailures++;
        }
    }
}  // namespace

// The module's file wants the game's main; no host: the log goes to stderr.
extern "C" void nebula_game_main(void) {
}

int main() {
    nebula_threads_init();

    GameMutex mutex;
    mutex.init();
    mutex.lock();
    mutex.lock();
    expect("nested count", mutex.count, 2);
    mutex.unlock();
    expect("still held", mutex.owner != nullptr, 1);
    mutex.unlock();
    expect("released", mutex.owner != nullptr, 0);

    void* slots[2];
    GameMessageQueue queue;
    queue.init(slots, 2);
    void* message = nullptr;
    expect("empty", queue.receive(&message), 0);
    expect("first in", queue.send(reinterpret_cast< void* >(1)), 1);
    expect("second in", queue.send(reinterpret_cast< void* >(2)), 1);
    expect("full", queue.send(reinterpret_cast< void* >(3)), 0);
    expect("first out", queue.receive(&message), 1);
    expect("first value", reinterpret_cast< long long >(message), 1);
    expect("second out", queue.receive(&message), 1);
    expect("second value", reinterpret_cast< long long >(message), 2);

    if (sFailures == 0) {
        printf("thread tests pass
");
    }
    return sFailures == 0 ? 0 : 1;
}
