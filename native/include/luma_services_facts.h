/* SPDX-License-Identifier: GPL-3.0-only */
/* What the host tells the game module's own services (services-time 11): given once by the host's provide_all, after
   luma_services_open and before the first mod. Not for mods. */
#ifndef LUMA_SERVICES_FACTS_H
#define LUMA_SERVICES_FACTS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct LumaServicesFacts {
    uint32_t size;            /* sizeof(LumaServicesFacts) as the host was built */
    int64_t (*utc_ns)(void);  /* any thread: nanoseconds since 1970-01-01 00:00 UTC (the system's wall clock) */
} LumaServicesFacts;

/* The module's export "luma_services_facts": keeps what `facts` gives (the struct is the host's for the process's
   life). Game thread. */
typedef void (*LumaServicesGiveFacts)(const LumaServicesFacts* facts);

/* The module's export "luma_services_monotonic_ns", for the host's services (services-system 6: a link's spacing): the
   game's steady clock as time.monotonic_ns reads it (frames in deterministic mode). Any thread. */
typedef uint64_t (*LumaServicesMonotonic)(void);

#ifdef __cplusplus
}
#endif

#endif
