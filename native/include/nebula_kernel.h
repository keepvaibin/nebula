#ifndef NEBULA_KERNEL_H
#define NEBULA_KERNEL_H

/* The kernel's C entry points (Game/System/NebulaKernel.hpp is the game's interface): host events, the platform layer's hooks
   (native/sdk/os/os_native.c) and the C callers' guards. */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The platform layer calls these: the kernel's start on the host thread that runs the game, and the thread diagnostic. */
void nebula_kernel_init(void);
void nebula_kernel_dump(void);

/* The platform layer provides these (os_native.c). */
/* A game thread is a host thread, which sleeps here until something wakes it. */
typedef struct NebulaHostThread NebulaHostThread;
/* A new host thread, asleep; once woken `entry(arg)`, which never returns. */
NebulaHostThread* nebula_host_thread_create(void (*entry)(void*), void* arg, uint32_t stackSize);
/* The calling host thread. */
NebulaHostThread* nebula_host_thread_adopt(void);
void nebula_host_thread_pass(NebulaHostThread* pTo);
void nebula_host_thread_wait(NebulaHostThread* pSelf);
/* The end of the calling thread. */
void nebula_host_thread_exit(NebulaHostThread* pSelf);
/* The kernel's two locks: its own state, and the section the audio thread stays out of. A lock can be left by the thread that took it. */
enum { NEBULA_HOST_LOCK_STATE = 0, NEBULA_HOST_LOCK_INTERRUPTS = 1 };
void nebula_host_lock(int which);
void nebula_host_unlock(int which);
/* The end of a sleeping thread. */
void nebula_host_thread_cancel(NebulaHostThread* pThread);

#ifdef __cplusplus
}
#endif

#endif
