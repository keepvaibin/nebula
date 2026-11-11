/* SPDX-License-Identifier: GPL-3.0-only */
/* Patches by key, as the game applies them (contract table-keys): one game table and the patches of every mod that is
   on, from the bottom of the mod list up, make the table the game reads: the last given (the mod higher in the list)
   wins, as for whole files. The same applier LumaLink's checks are made against.

   The table is the store's cooked table (NTBL) and its path the store path ("StageData/AstroGalaxy/jmp/Placement/
   Common/ObjInfo"; a disc path with its archives is taken too). A mod's patch that cannot apply is left out whole and
   named in a LUMA_PATCH_FAILED fact; the others still apply. Two mods changing one thing, a row a mod names that is not
   there, and how many rows each selector matched are facts too, for the Mods screen: none is silent.

   Every pointer in the input is read only during the call. A result is used from one thread at a time. */
#ifndef LUMA_PATCH_H
#define LUMA_PATCH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One mod's patch of the table: `layers/<layer>/patches/<game path>.json` of a mod that is on. */
typedef struct LumaTablePatch {
    const char* mod_id;  /* the mod's id, to name it */
    const char* file;    /* the patch file in the mod */
    const uint8_t* json; /* the file's bytes (UTF-8 JSON) */
    uint64_t json_size;
} LumaTablePatch;

typedef struct LumaPatchInput {
    uint32_t size;         /* sizeof(LumaPatchInput) */
    const char* path;      /* the table's path in the store, UTF-8 */
    const uint8_t* table;  /* the game's cooked table */
    uint64_t table_size;
    const LumaTablePatch* patches; /* from the bottom of the mod list up: the last given wins */
    uint32_t patch_count;
} LumaPatchInput;

enum {
    LUMA_PATCH_CONFLICT = 0, /* mod_id and other change one row (column null: the whole row); mod_id's is used: it is
                                higher in the list */
    LUMA_PATCH_MISSING = 1,  /* mod_id's file names `row`, which is not there: other removed it, or (null) an
                                lower mod's whole table left it out */
    LUMA_PATCH_SELECTED = 2, /* mod_id's selector number `selector` matched `rows` rows (0: a note, L407) */
    LUMA_PATCH_FAILED = 3    /* mod_id's file could not apply and is left out; `why` in English, for the log */
};

typedef struct LumaPatchFact {
    uint32_t kind;          /* LUMA_PATCH_* */
    const char* mod_id;
    const char* file;       /* mod_id's patch file; null for a conflict */
    const char* other;      /* the other mod, or null */
    const char* row;        /* the row's key values joined by U+001F, "#<n>" by place, "*" the whole table; or null */
    const char* column;     /* "0x" and the column's hash, or null */
    const char* why;        /* LUMA_PATCH_FAILED only */
    uint32_t selector;
    uint32_t rows;
} LumaPatchFact;

typedef struct LumaPatched LumaPatched;

/* Never null; give it back to luma_patched_free. */
LumaPatched* luma_patch_table(const LumaPatchInput* input);
/* 0: the table is patched (each mod that could not apply named in a fact); -1: the input was not valid or the game's
   table cannot be read by its rule (luma_patched_error says why, in English), and the game's table stands. */
int32_t luma_patched_state(const LumaPatched* result);
const char* luma_patched_error(const LumaPatched* result);
/* The patched cooked table. Valid until luma_patched_free; null when the state is -1. */
const uint8_t* luma_patched_table(const LumaPatched* result, uint64_t* size);
uint32_t luma_patched_fact_count(const LumaPatched* result);
/* Null past the last. Valid until luma_patched_free. */
const LumaPatchFact* luma_patched_fact_at(const LumaPatched* result, uint32_t index);
void luma_patched_free(LumaPatched* result);

#ifdef __cplusplus
}
#endif

#endif
