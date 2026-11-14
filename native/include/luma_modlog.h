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
    LUMA_MODLOG_GUARD = 5,     /* a fault caught in the mod's code (the guard) */
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
    const char* row;     /* a row of the module's text table ("Start.PartMissing") for the words, used when text
                            and host are null: {mod} and the row's {names} filled from names/values, both languages
                            from the row; a value written "R:<row>" is that row's words in the same language, its own
                            {names} filled from the same entry. Read only when size covers it; null: none */
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

/* One kept entry as the host's Mods > Log page reads it (contract 4.2). Every pointer lives for the session. */
typedef struct LumaModLogView {
    uint32_t size; /* sizeof(LumaModLogView) as the caller knows it: the module writes whole fields, none past it */
    uint32_t source;
    uint32_t level;
    uint32_t code;
    uint32_t line, column;
    uint32_t count;        /* repeats included, as it is now */
    uint32_t reload;       /* the mod's reload number when it was added; 0 before any */
    const char* mod;
    const char* file;      /* "" when none */
    const char* text;      /* the played language, UTF-8 */
    const char* english;
    const char* code_name; /* the code as the page shows it: "L203", "Hook.NoResult", "S0904"; "" for none */
} LumaModLogView;

/* The game module's exports for the page, found by name ("luma_modlog_next", "luma_modlog_next_mod",
   "luma_modlog_words"). Any thread: kept entries never change or go in a session, only their counts grow.

   luma_modlog_next: the kept entry after `after` (null: the first), in added order, into *view; returns it, for the
   next call; null past the last (view untouched).
   luma_modlog_next_mod: the mod after `after` (null: the first) that has entries, in the order its first came: its id
   and how many of its entries were not kept (ModLog.TooMany's count); null past the last.
   luma_modlog_words: the module's text row `row` (ModLog.*) in the played language as UTF-8 into `out` (`size` bytes,
   cut at a character), {mod} and {count} filled; the bytes written, 0 when there is no such row.
   luma_modlog_row_words ("luma_modlog_row_words"): any text row in the played language, {mod} from values->mod and
   its other {names} from values' names and values (a value "R:<row>" is that row's words, as `row` above); only mod,
   names, values and count of `values` are read. The host's screens' rows with values (consent's Permission.*.Text
   with {hosts}). */
typedef const void* (*LumaModLogNext)(const void* after, LumaModLogView* view);
typedef const void* (*LumaModLogNextMod)(const void* after, const char** id, uint32_t* past);
typedef uint32_t (*LumaModLogWords)(const char* row, const char* mod, uint32_t count, char* out, uint32_t size);
typedef uint32_t (*LumaModLogRowWords)(const LumaModLogEntry* values, const char* row, char* out, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif
