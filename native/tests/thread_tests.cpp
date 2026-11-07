// The game's synchronisation (Game/System/GameThreads.hpp): a mutex nests.
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

    if (sFailures == 0) {
        printf("thread tests pass
");
    }
    return sFailures == 0 ? 0 : 1;
}
