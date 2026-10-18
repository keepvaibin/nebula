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
/* The module's own image (its code and data), for diagnostics. */
void nebula_image_range(uintptr_t* base, uintptr_t* size);
void nebula_memory_regions(NebulaMemoryRegion* out, int* count);
void nebula_memory_arenas(void** mem1Lo, void** mem1Hi, void** mem2Lo, void** mem2Hi);

/* The host's services (sdk/core.c); NULL before nebula_game_attach. */
const struct NebulaHostServices* nebula_host(void);

/* Log lines and fatal errors, through the host's log and fatal services (stderr before the host attaches). */
void nebula_log(const char* format, ...) __attribute__((format(printf, 1, 2)));
void nebula_vlog(const char* format, va_list args) __attribute__((format(printf, 1, 0)));
void nebula_fatal(const char* format, ...) __attribute__((noreturn, format(printf, 1, 2)));

/* The data tables the game keeps in main.dol, read from the user's disc (dol_data.c). */
void nebula_load_dol_data(void);

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
