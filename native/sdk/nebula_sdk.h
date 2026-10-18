/* The native SDK layer's internal interface (native/sdk). */
#ifndef NEBULA_SDK_H
#define NEBULA_SDK_H

#include <stdarg.h>
#include <stdint.h>

#include <revolution/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A physical range of the game's memory and where it lives in the host process (for the GPU's address
   resolution). */
typedef struct NebulaMemoryRegion {
    uint32_t physical;
    uint32_t size;
    uint8_t* host;
} NebulaMemoryRegion;

int nebula_memory_init(void);
uint8_t* nebula_arena_base(void);
void nebula_memory_regions(NebulaMemoryRegion* out, int* count);
void nebula_memory_arenas(void** mem1Lo, void** mem1Hi, void** mem2Lo, void** mem2Hi);

/* Log lines and fatal errors (stderr until the host's log is attached). */
void nebula_log(const char* format, ...);
void nebula_vlog(const char* format, va_list args);
void nebula_fatal(const char* format, ...) __attribute__((noreturn));

/* The OS core (os/os_native.c). */
/* A device interrupt by its SDK number (__OS_INTERRUPT_*), from any host thread: the handler registered with
   __OSSetInterruptHandler runs on the simulation thread at the next point interrupts are enabled. */
void nebula_raise_interrupt(int interrupt);
/* Wakes the simulation thread if it is idle (new host event). */
void nebula_signal_simulation(void);
/* The console's time base: OSGetTime ticks (60.75 MHz) since boot. */
int64_t nebula_time_base(void);
/* Runs the game: the OS core's boot, then the game's entry on its boot thread. Does not return. */
void nebula_os_boot(void (*gameMain)(void)) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif

#endif
