/* SPDX-License-Identifier: GPL-3.0-only */
/* The validator of a .luma, as the game calls it (validator contract, section 3). One library with LumaLink's: the
   same rules, the same findings, the same text in every language.

   At import: luma_validate with `kept` null runs every rule; keep luma_report_kept's bytes beside the mod.
   At each start, and each change of the mod list or options: luma_validate with the mod's kept bytes. When they are for
   the same .luma and the same game (release, region, platform, languages, LumaCore version, names, services,
   store_key), the rules that read the whole .luma are taken from them and every other rule runs; otherwise every rule
   runs. Keep the new kept bytes either way.

   Every pointer in the input is read only during the call. A report is used from one thread at a time. */
#ifndef LUMA_VALIDATE_H
#define LUMA_VALIDATE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Given to a store's `folder`: called once for each name directly in the folder, the name in the game's case,
   `is_folder` non-zero when it is a folder. The name is read only during the call. */
typedef void (*LumaFolderSink)(void* sink_context, const char* name, uint32_t is_folder);

/* The player's game files (validator contract, revision 3): all three functions are given (which files may be new is
   the library's own list, as LumaLink's; F INBOX-G 46). Rust reads each table from
   its cooked bytes, so the game's check and LumaLink's build one view with one code (table-keys 11). */
typedef struct LumaGameStore {
    uint32_t size; /* sizeof(LumaGameStore) */
    void* context;
    /* The game's own case of a lowercase path, NUL-terminated in `out` when it fits; returns its length in bytes,
       0 when the game has no such path. Called again with a larger `out` when the length did not fit. */
    uint32_t (*path)(void* context, const char* lower, char* out, uint32_t out_size);
    /* The file at `path` (a path `path` gave) when it is a table: its cooked bytes as the store holds them (NTBL) in
       *bytes and *bytes_size, and its path in the store, in the game's case, in *store_path (null: `path` is it).
       Returns 0 when the file is no table. The bytes and the path are valid until the next call on this store. */
    uint32_t (*table)(void* context, const char* path, const uint8_t** bytes, uint64_t* bytes_size, const char** store_path);
    /* The names directly in the folder at `lower` (a lowercase path; "stagedata" lists the galaxies' folders, as the
       game's ScenarioDataParser does), each through `sink`, in any order; returns how many, or -1 when the path is no
       folder. */
    int32_t (*folder)(void* context, const char* lower, LumaFolderSink sink, void* sink_context);
} LumaGameStore;

/* Another installed mod: its file name and its kept report. */
typedef struct LumaInstalledMod {
    const char* file;
    uint32_t on;
    const uint8_t* kept;
    uint64_t kept_size;
} LumaInstalledMod;

typedef struct LumaOption {
    const char* key;
    const char* value;
} LumaOption;

typedef struct LumaValidateInput {
    uint32_t size;          /* sizeof(LumaValidateInput) */
    const char* path;       /* the .luma, UTF-8 */
    const char* platform;   /* "windows-x64", "android-arm64"... (luma-format 6) */
    const char* nebula;     /* this release, "2.0.0" */
    const char* region;     /* "RMGE01", "RMGP01", "RMGJ01" */
    const char* const* languages; /* languages beyond the 14 that installed language mods add */
    uint32_t language_count;
    const char* names;      /* the game's names as `.rows` lines (name, signature, hook|tell, line); null: not checked */
    uint64_t names_size;
    uint32_t luma_api;      /* LUMA_API */
    const char* services;   /* the game's services catalog: JSON with the self-description's "services" key (services-registry 9);
                               null: kind 5 imports are not checked */
    uint64_t services_size;
    const LumaGameStore* store;     /* null: the rules on game paths and rows are not checked */
    const char* store_key;          /* what identifies the store's content (its version and the game it came from) */
    const LumaInstalledMod* installed; /* every other installed mod, in load order (an update leaves out the mod it replaces) */
    uint32_t installed_count;
    uint32_t position;      /* how many of `installed` load before this mod; UINT32_MAX: after all of them */
    const LumaOption* options; /* the player's values; null: the defaults */
    uint32_t option_count;
    const uint8_t* kept;    /* this mod's kept report; null at import */
    uint64_t kept_size;
    const char* types;      /* the game's luma_types.json (types contract); null: kinds 1, 2 and 7 are not checked */
    uint64_t types_size;
} LumaValidateInput;

typedef struct LumaFinding {
    uint32_t code;          /* 203 is L203 */
    uint32_t level;         /* 0 an error (the mod is refused, or off), 1 a note */
    const char* mod_id;     /* null only when luma.json cannot be read */
    const char* file;       /* the entry inside the mod, or the .luma */
    uint32_t line;          /* 0: none */
    uint32_t column;        /* 0: none */
    const char* pointer;    /* a JSON pointer, or null */
    const char* text;       /* the message, with the place, in the language asked for */
} LumaFinding;

typedef struct LumaReport LumaReport;

/* Never null; give it back to luma_report_free. */
LumaReport* luma_validate(const LumaValidateInput* input);
/* 0: every rule ran; 1: a start's check; -1: the input was not valid (luma_report_invalid says why, in English). */
int32_t luma_report_state(const LumaReport* report);
const char* luma_report_invalid(const LumaReport* report);
uint32_t luma_report_count(const LumaReport* report);
/* The findings that refuse the mod or leave it off; 0: it may be on. */
uint32_t luma_report_errors(const LumaReport* report);
/* Finding `index`, its text in `language` ("UsEnglish", "EuGerman"...; null or unknown: UsEnglish). Null past the
   last. Valid until the next luma_report_at on this report, or its free. */
const LumaFinding* luma_report_at(const LumaReport* report, uint32_t index, const char* language);
/* The rules not checked for want of an input: the code in *code, what was missing as text. */
uint32_t luma_report_not_checked_count(const LumaReport* report);
const char* luma_report_not_checked_at(const LumaReport* report, uint32_t index, const char* language, uint32_t* code);
/* The bytes to keep beside the mod. Valid until luma_report_free. */
const uint8_t* luma_report_kept(const LumaReport* report, uint64_t* size);
/* The mod's id and version from its luma.json; null when not given well formed. */
const char* luma_report_id(const LumaReport* report);
const char* luma_report_version(const LumaReport* report);
/* The service number `index` (0 first) that the mod's entry library for `platform` imports without LUMA_OPTIONAL, in
   import order, each once: its name; null past the last. The same after a start's check that took the kept report. A
   permissioned service's permission is its name (services-registry 9). */
const char* luma_report_required_service(const LumaReport* report, const char* platform, uint32_t index);
void luma_report_free(LumaReport* report);

/* A refusal said as the validator says its findings (loader contract 3; the cook's `failed` facts): rule `code`'s
   message with its parameters, names[i] = values[i]. A value "T:<text>" (or one with neither prefix) is a literal,
   "P:<phrase id>" a phrase, itself translated; the place goes in as "mod", "file", "line", "column". A parameter the
   text needs and the call lacks shows as {name}. `language` as luma_report_at. Never null; valid until the next
   luma_validate_message or luma_validate_reason on the same thread. */
const char* luma_validate_message(uint16_t code, const char* const* names, const char* const* values, uint32_t count,
                                  const char* language);
/* Why, without where: the `why` or `found` phrase filled (the Mods screen puts it after the mod and file it names),
   else the whole message. As luma_validate_message otherwise. */
const char* luma_validate_reason(uint16_t code, const char* const* names, const char* const* values, uint32_t count,
                                 const char* language);

#ifdef __cplusplus
}
#endif

#endif
