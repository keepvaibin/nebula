/* The game module's interface to Nebula's host (docs/NATIVE_PORT.md, "Module layout"): plain C, so the game module
   (built against the game's own C++ library) and the host (the host's C++ library) share nothing else. The host fills
   NebulaHostServices and calls nebula_game_attach, then runs the game on its simulation thread with nebula_game_run;
   events from host threads (the renderer's PE tokens and stream consumption, the video retrace, audio, input, disc)
   come back through the nebula_game_* entry points, which may be called from any thread. */
#ifndef NEBULA_GAME_API_H
#define NEBULA_GAME_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#if defined(NEBULA_GAME_MODULE)
#define NEBULA_GAME_API __declspec(dllexport)
#else
#define NEBULA_GAME_API __declspec(dllimport)
#endif
#else
#define NEBULA_GAME_API __attribute__((visibility("default")))
#endif

#define NEBULA_GAME_API_VERSION 2u

/* A physical range of the game's memory and where it is in the host process: what the GPU's addresses resolve to. */
typedef struct NebulaGameMemoryRegion {
    uint32_t physical;
    uint32_t size;
    uint8_t* host;
} NebulaGameMemoryRegion;

typedef struct NebulaHostServices {
    uint32_t version;
    uint32_t size;
    void* user;

    /* One line of the game's reports (OSReport) or the native layer's log, without a trailing newline. */
    void (*log)(void* user, const char* text);

    /* GX: the game's command stream (the console's main FIFO), in order, as spans of the module's command ring. A
       span is contiguous, ends where the game flushed (GXFlush, a draw sync, a draw done: a command boundary) and
       stays valid and unchanged until the host releases it (nebula_game_gx_release), so the renderer parses it in
       place. When the ring is full the game waits for releases, as the console's CPU waits on a full GP FIFO. */
    void (*gx_submit)(void* user, const uint8_t* bytes, size_t size);
    /* A PE event at the end of the stream so far: a draw-sync token (finish = 0) or a draw done (finish = 1). The
       renderer answers it with nebula_game_gx_event when its parse reaches that point. */
    void (*gx_pe_marker)(void* user, int finish, uint16_t token);
    /* An EFB pixel (type 0: colour, 1: depth), as GXPeekARGB/GXPeekZ read it. */
    uint32_t (*gx_peek_efb)(void* user, uint16_t x, uint16_t y, uint32_t type);
} NebulaHostServices;

/* Returns NEBULA_GAME_API_VERSION, or 0 if the host's services are too old. */
NEBULA_GAME_API uint32_t nebula_game_attach(const NebulaHostServices* host);
/* Runs the game on the calling thread, the simulation thread. Does not return. */
NEBULA_GAME_API void nebula_game_run(void);
/* The game's memory regions for the renderer (valid once nebula_game_run has started the game). */
NEBULA_GAME_API int nebula_game_memory_regions(NebulaGameMemoryRegion* out, int max);
/* The renderer reached a PE draw-sync token (finish = 0) or a draw done (finish = 1). */
NEBULA_GAME_API void nebula_game_gx_event(int finish, uint16_t token);
/* The host is done with the next `size` bytes of the command stream (spans are released in order, whole or in part):
   the ring may reuse them. The host must release without waiting on further progress of the game, which may itself be
   waiting for room. */
NEBULA_GAME_API void nebula_game_gx_release(size_t size);

#ifdef __cplusplus
}
#endif

#endif
