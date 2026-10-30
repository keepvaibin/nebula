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

/* The platform layer calls these: the kernel's start on the boot stack, and the thread diagnostic. */
void nebula_kernel_init(void* bootStackTop, void* bootStackEnd);
void nebula_kernel_dump(void);

/* The platform layer provides these (os_native.c). */
int64_t nebula_time(void);
void nebula_timer_arm(int64_t deadline);
void nebula_sleep_for_work(void);
void nebula_wake_simulation(void);
void* nebula_thread_stack(const void* owner, uint32_t requested, uint32_t* size);
void nebula_thread_stack_release(const void* owner);
void* nebula_prepare_stack(void* top, size_t size, void (*entry)(void*), void* arg);
void nebula_switch_stack(void** fromSp, void* toSp);

#ifdef __cplusplus
}
#endif

#endif
