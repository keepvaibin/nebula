/* SPDX-License-Identifier: GPL-3.0-only */
/* The on-screen mod log (contract `mod-log`): one store of everything said about a mod this session, kept by the game
   module, shown as a notice while playing and as the LOG page in MODS. Between the host and the game module; mods
   never call it (their reports reach it through LumaCore.report). */
#ifndef LUMA_MODLOG_H
#define LUMA_MODLOG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LUMA_MODLOG_ARGS 8u

typedef enum LumaModLogLevel {
    LUMA_MODLOG_ERROR = 0, /* something the mod meant does not happen: refused, off, a placeholder stands in, a call did
                              nothing */
    LUMA_MODLOG_NOTE = 1   /* shown, nothing lost: an overlap the order settled, the player's own choice */
} LumaModLogLevel;

typedef enum LumaModLogSource {
    LUMA_MODLOG_VALIDATOR = 0, /* a finding of luma_validate at import or start (validator.md) */
    LUMA_MODLOG_LOADER = 1,    /* a refusal of luma_load_mod (loader.md section 3) or the host's start order */
    LUMA_MODLOG_COOK = 2,      /* a fact of the device cooker: a failed patch or file, an overlap */
    LUMA_MODLOG_CODE = 3,      /* LumaCore.report: the kernel's codes 1-99, a service's 100-899, lumart's 900-9999 */
    LUMA_MODLOG_CONTENT = 4,   /* the game's content at run time: a reference not found, a placeholder, an effect that
                                  draws nothing (contracts references, effects) */
    LUMA_MODLOG_GUARD = 5,     /* a fault caught in the mod's code (plan 1.6) */
    LUMA_MODLOG_SOURCE_COUNT = 6
} LumaModLogSource;

typedef struct LumaModLogEntry {
    uint32_t size;   /* sizeof(LumaModLogEntry) as the caller knows it */
    uint32_t source; /* LumaModLogSource */
    uint32_t level;  /* LumaModLogLevel */
    uint32_t code;   /* the source's own code: 203 for L203, 1 for Hook.OutOfMemory, 101 for Service.Argument */
    const char* mod; /* the mod's id; for a .luma whose manifest cannot be read, its file name. Never null */
    const char* file; /* the entry inside the mod ("luma.json", "layers/base/assets/aura.json"), a script file, a function
                         ("Mario::update") for the kernel's run-time lines, "<service>.<function>" for a service's report
                         of a mistake (line 0), or "<service> event <kind>" for the guard in a service's event; "" or
                         null if none */
    uint32_t line, column; /* 0 when the place has none */
    const char* names[LUMA_MODLOG_ARGS];  /* the message's parameter names, in the row's order */
    const char* values[LUMA_MODLOG_ARGS]; /* VALIDATOR, LOADER, COOK: "T:<text>" a literal, "P:<phrase id>" a phrase
                                             (validator.md rev 2 item 1). CODE, CONTENT, GUARD: the values as given,
                                             plain UTF-8; nothing is stripped from them */
    uint32_t count;      /* how many of names/values; at most LUMA_MODLOG_ARGS */
    const char* text;    /* the line in the played language, already rendered by the source (UTF-8); null: the log
                            renders it (contract section 3) */
    const char* english; /* the same line in English for the game's log (UTF-8); null: rendered as above */
    const char* host;    /* a host sentence (English capitals, as the Mods page's: "ITS CODE DID NOT START"), used when
                            text is null: the words are its translation as the settings menu translates host text, and
                            english is the sentence as given. Read only when size covers it; null: none */
} LumaModLogEntry;

/* The smallest `size` the log reads: every field up to and including `english`. */
#define LUMA_MODLOG_ENTRY_SIZE ((uint32_t)(offsetof(LumaModLogEntry, english) + sizeof(const char*)))

/* A harness fact: `key` and its value as JSON text (luma_harness.h's `fact`). */
typedef void (*LumaModLogFact)(const char* key, const char* json);

/* The game module's exports, found by name ("luma_modlog_session", "luma_modlog_add").

   luma_modlog_session: once per session, from the host's start, before the first scene: how many mods are on this
   session (`mods_on`; 0: none, and then the notice is never made), and the harness's fact writer (null outside a
   harness run). Entries added before it are kept; their facts are written when it gives the writer.

   luma_modlog_add: adds an entry, or counts a repeat of one already kept (key: mod, source, code, file, line,
   column). Any thread. Copies everything it is given; the caller's strings may go when it returns. */
typedef void (*LumaModLogSession)(uint32_t mods_on, LumaModLogFact fact);
typedef void (*LumaModLogAdd)(const LumaModLogEntry* entry);

#ifdef __cplusplus
}
#endif

#endif
