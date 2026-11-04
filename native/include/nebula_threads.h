#ifndef NEBULA_THREADS_H
#define NEBULA_THREADS_H

/* The game's threads' C side: the host threads (GameHostThreads.c) and the start. */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The start on the host thread that runs the game, and the thread diagnostic. */
void nebula_threads_init(void);
void nebula_threads_dump(void);

/* A game thread is a host thread, which sleeps here until something wakes it. */
typedef struct NebulaHostThread NebulaHostThread;
/* The calling host thread. */
NebulaHostThread* nebula_host_thread_adopt(void);
void nebula_host_thread_pass(NebulaHostThread* pTo);
void nebula_host_thread_wait(NebulaHostThread* pSelf);
/* The kernel's two locks: its own state, and the section the audio thread stays out of. A lock can be left by the thread that took it. */
enum { NEBULA_HOST_LOCK_STATE = 0, NEBULA_HOST_LOCK_AUDIO = 1 };
void nebula_host_lock(int which);
void nebula_host_unlock(int which);

#ifdef __cplusplus
}
#endif

#endif
