/* SPDX-License-Identifier: GPL-3.0-only */
/* LumaCore services: what every service's table shares (services-registry contract). A service is found through
   luma.h: an import row LUMA_SERVICE(slot, "fs", "1.0"), or core->service(mod, "fs", 1, 0) after start.

   Every service function takes the calling mod first, runs on the game thread unless its contract says otherwise, and
   returns a status: 0 or more is success, less than 0 an error. A mistake of the mod (LUMA_E_ARGUMENT, LUMA_E_THREAD,
   LUMA_E_HANDLE, LUMA_E_LIMIT, a struct too small) is also reported on the Mods page; an answer of the world (a file
   that is not there, a refused connection) is only returned. No call blocks: what waits on the outside world returns
   LUMA_PENDING and arrives later as a LumaEvent at the frame's start.

   Every struct a mod passes to a service, or a service fills for a mod, starts with `uint32_t size` set to sizeof as
   the mod was built: a service reads and writes only that much, so a mod built against an earlier minor keeps
   working. */
#ifndef LUMA_SERVICES_H
#define LUMA_SERVICES_H

#include <stdint.h>

#include "luma.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Every service table starts with this. `size` is the table's size in the game that runs: a later minor only adds at
   the end, so a mod built against an earlier minor reads only what it knows. */
typedef struct LumaServiceTable {
    uint32_t size;
    uint16_t major;
    uint16_t minor;
} LumaServiceTable;

/* What a service function returns. Values are only ever added. */
enum {
    LUMA_OK = 0,
    LUMA_PENDING = 1,       /* accepted: the answer comes as an event */
    LUMA_E_ARGUMENT = -1,   /* an argument the function does not take: the mod's mistake, reported */
    LUMA_E_THREAD = -2,     /* called off the game thread where the function does not allow it: reported */
    LUMA_E_HANDLE = -3,     /* a handle that is closed, not this mod's, or never was: reported */
    LUMA_E_LIMIT = -4,      /* a limit the service's contract states was reached: reported */
    LUMA_E_DENIED = -5,     /* outside what the player granted (a host the manifest does not name) */
    LUMA_E_ABSENT = -6,     /* this platform or this device has no such thing */
    LUMA_E_NOT_FOUND = -7,  /* no such file, host, device, name */
    LUMA_E_EXISTS = -8,     /* it exists already */
    LUMA_E_IO = -9,         /* the system failed to read, write, open or send */
    LUMA_E_TIMEOUT = -10,   /* no answer within the time the call or the contract gives */
    LUMA_E_CLOSED = -11,    /* the other side closed, or the thing was closed meanwhile */
    LUMA_E_CANCELLED = -12, /* the mod cancelled it, or the game is taking the mod out */
    LUMA_E_MEMORY = -13,    /* no memory for it */
    LUMA_E_BUSY = -14,      /* not now: a queue is full or the device is in use; try again on a later frame */
    LUMA_E_FORMAT = -15,    /* the data is not of the form the function reads */
    LUMA_E_TOO_LARGE = -16, /* more than the function or the contract allows at once */
    LUMA_E_UNREACHABLE = -17, /* the network cannot reach the other side */
    LUMA_E_STATE = -18        /* not valid now: a draw call outside a draw point's function (draw 1.0) */
};

/* A handle to something a service made for the mod (a file, a socket, a sound). Never 0; never given twice in one
   session, so a closed handle stays recognisably closed. */
typedef uint64_t LumaHandle;
/* A colour a service takes or gives: 0xRRGGBBAA, 8 bits each, alpha 0xFF opaque (registry 3, R-9e). */
typedef uint32_t LumaColor;

/* An answer from the outside world, delivered on the game thread at the frame's start. Everything it points at is
   valid only during the call: copy what you keep. */
typedef struct LumaEvent {
    uint32_t size;     /* sizeof(LumaEvent) in the game that runs */
    uint32_t kind;     /* the service's own event kinds (its contract) */
    int32_t status;    /* LUMA_OK or a LUMA_E_* */
    uint32_t flags;    /* the kind's */
    LumaHandle handle; /* what it is about: the handle or the request id the call gave */
    const void* data;  /* the kind's bytes, or null */
    uint64_t data_size;
    uint64_t value; /* a number the kind defines */
} LumaEvent;

/* The mod's function for events, given with the call that asks; `user` comes back as it was given. */
typedef void (*LumaEventFn)(void* user, const LumaEvent* event);

/* The most event deliveries (nodes) one mod is given in one frame; the rest wait for the next frames, in order. */
#define LUMA_EVENTS_PER_FRAME 256u

#ifdef __cplusplus
}
#endif

#endif
