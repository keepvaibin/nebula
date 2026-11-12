/* SPDX-License-Identifier: GPL-3.0-only */
/* What the host tells the game module's own services (services-time 11): given once by the host's provide_all, after
   luma_services_open and before the first mod. Not for mods. */
#ifndef LUMA_SERVICES_FACTS_H
#define LUMA_SERVICES_FACTS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One user file as save.game's offer screen shows it (services-save 8): the module's reading of it. */
typedef struct LumaOfferFile {
    uint32_t exists;   /* 0: an empty file */
    uint32_t in_play;  /* 1: the file loaded now (shown, not pickable) */
    uint32_t luigi;    /* 1: its last character Luigi */
    uint32_t stars;    /* that character's power stars */
} LumaOfferFile;

typedef struct LumaOfferFacts {
    LumaOfferFile offered;   /* the mod's file */
    LumaOfferFile files[6];  /* user files 1-6, as in memory */
} LumaOfferFacts;

/* The offer screen's answer, on the game thread at a frame's start: LUMA_OK and the user file (1-6) to replace, or
   LUMA_E_DENIED and 0. */
typedef void (*LumaOfferDone)(void* context, int32_t status, uint32_t file);

typedef struct LumaServicesFacts {
    uint32_t size;            /* sizeof(LumaServicesFacts) as the host was built */
    int64_t (*utc_ns)(void);  /* any thread: nanoseconds since 1970-01-01 00:00 UTC (the system's wall clock) */
    /* Game thread. The file at `path` (a data path, services-fs 5) in mod `id`'s data folder: LUMA_OK, its length in
       *length and min(size, length) of its bytes in buf (buf may be null when size is 0); LUMA_E_NOT_FOUND none (or a
       folder), LUMA_E_DENIED through a link, LUMA_E_IO not read. save.game's offer reads the offered file with it. */
    int32_t (*data_file)(const char* id, const char* path, void* buf, uint32_t size, uint32_t* length);
    /* Game thread. save.game's offer screen (galaxy/mod_screens.h ask_offer) for `asker`: LUMA_PENDING and `done`
       called once later (never inside this call), LUMA_E_BUSY a screen is up, LUMA_E_ABSENT no menu in this host. */
    int32_t (*ask_offer)(const LumaOfferFacts* facts, const char* asker, LumaOfferDone done, void* context);
    /* Game thread. `asker`'s open screen closes with no answer (done is not called). */
    void (*close_offer)(const char* asker);
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
