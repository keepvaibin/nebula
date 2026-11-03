/* The host threads under the game's threads (GameThreads.cpp): each sleeps until woken; the two locks. The host holds them. */
#include <nebula_game_api.h>

#include "nebula_threads.h"
#include "nebula_module.h"

NEBULA_GAME_API void nebula_game_debug_dump(void) {
    nebula_threads_dump();
    const NebulaHostServices* host = nebula_host();
    host->threads_dump(host->user, (const void*)&nebula_game_debug_dump);
}

NebulaHostThread* nebula_host_thread_create(void (*entry)(void*), void* arg, uint32_t stackSize) {
    (void)stackSize;
    const NebulaHostServices* host = nebula_host();
    return host->thread_create(host->user, entry, arg);
}

NebulaHostThread* nebula_host_thread_adopt(void) {
    const NebulaHostServices* host = nebula_host();
    return host->thread_adopt(host->user);
}

void nebula_host_thread_pass(NebulaHostThread* pTo) {
    const NebulaHostServices* host = nebula_host();
    host->thread_pass(host->user, pTo);
}

void nebula_host_thread_wait(NebulaHostThread* pSelf) {
    const NebulaHostServices* host = nebula_host();
    host->thread_wait(host->user, pSelf);
}

void nebula_host_thread_exit(NebulaHostThread* pSelf) {
    const NebulaHostServices* host = nebula_host();
    host->thread_exit(host->user, pSelf);
}

void nebula_host_thread_cancel(NebulaHostThread* pThread) {
    const NebulaHostServices* host = nebula_host();
    host->thread_cancel(host->user, pThread);
}

void nebula_host_lock(int which) {
    const NebulaHostServices* host = nebula_host();
    host->lock(host->user, which);
}

void nebula_host_unlock(int which) {
    const NebulaHostServices* host = nebula_host();
    host->unlock(host->user, which);
}
