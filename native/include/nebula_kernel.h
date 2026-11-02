#ifndef NEBULA_KERNEL_H
#define NEBULA_KERNEL_H

/* The kernel's C entry points (Game/System/NebulaKernel.hpp is the game's interface): host events, the platform layer's hooks
   (native/sdk/os/os_native.c) and the C callers' guards. */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Host events, delivered in this order when several are pending: the timer (alarms), the frame clock, the
   controllers, the disc, the audio DMA. */
enum {
    NEBULA_EVENT_TIMER = 0,
    NEBULA_EVENT_FRAME = 1,
    NEBULA_EVENT_CONTROLLER = 2,
    NEBULA_EVENT_DISC = 3,
    NEBULA_EVENT_AUDIO = 4,
    NEBULA_EVENT_COUNT = 5,
};

/* From any host thread: the event's handler runs on the simulation thread at the next scheduling point. */
void nebula_raise_event(int event);
/* On the simulation thread, at no cost beyond the store: noted for the next scheduling point. */
void nebula_note_event(int event);

/* The C callers' event handlers (NebulaKernel::setEventHandler, enableEvent). */
void nebula_set_event_handler(int event, void (*handler)(void));
void nebula_enable_event(int event);

int nebula_interrupts_disable(void);
int nebula_interrupts_restore(int enabled);
int32_t nebula_scheduler_disable(void);
int32_t nebula_scheduler_enable(void);
void nebula_kernel_poll(void);

/* The platform layer calls these: the kernel's start on the host thread that runs the game, and the thread diagnostic. */
void nebula_kernel_init(void);
void nebula_kernel_dump(void);

/* The platform layer provides these (os_native.c). */
int64_t nebula_time(void);
void nebula_timer_arm(int64_t deadline);
/* Deterministic test mode: the kernel moves the clock itself (no host clock runs). */
void nebula_time_set(int64_t time);
void nebula_sleep_for_work(void);
void nebula_wake_simulation(void);
/* A game thread is a host thread. The kernel runs one at a time: each waits for its turn here, and passes it on. */
typedef struct NebulaHostThread NebulaHostThread;
/* A new host thread, waiting for its first turn; then `entry(arg)`, which never returns. */
NebulaHostThread* nebula_host_thread_create(void (*entry)(void*), void* arg, uint32_t stackSize);
/* The calling host thread. */
NebulaHostThread* nebula_host_thread_adopt(void);
void nebula_host_thread_pass(NebulaHostThread* pTo);
void nebula_host_thread_wait(NebulaHostThread* pSelf);
/* The end of the calling thread, whose turn has been passed on. */
void nebula_host_thread_exit(NebulaHostThread* pSelf);
/* Lets another host thread run. */
void nebula_host_thread_yield(void);
/* The end of a thread waiting for its turn. */
void nebula_host_thread_cancel(NebulaHostThread* pThread);

#ifdef __cplusplus
}
#endif

#endif
