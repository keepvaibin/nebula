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

/* Another table of the table's link group (contract table-keys 13): the same zone and kind in another layer
   (StageData/<zone>/jmp/Placement|MapParts|ChildObj|Start|GeneralPos/<layer>/<file>, layer Common or LayerA to
   LayerP), with the patches of the mods that are on for it, as the input's own. */
typedef struct LumaLinkTable {
    const char* path;
    const uint8_t* table;
    uint64_t table_size;
    const LumaTablePatch* patches;
    uint32_t patch_count;
} LumaLinkTable;

typedef struct LumaPatchInput {
    uint32_t size;         /* sizeof(LumaPatchInput) as the caller was built; any other size is refused */
    const char* path;      /* the table's path in the store, UTF-8 */
    const uint8_t* table;  /* the game's cooked table */
    uint64_t table_size;
    /* From the bottom of the mod list up: the last given wins. One mod's patches of this table (one a layer that is
       on, `layers/<layer>/patches/<path>.json`) in its layer order (luma-format 5: the layers as the mod lists them,
       the ones the player turned on), then by file path. luma-format 5.1's "load order, layer order, then file
       order" for one table; the caller builds this list, so this is its contract. */
    const LumaTablePatch* patches;
    uint32_t patch_count;
    /* A table of a link group: the other tables of its group the game meets its key among, each once (none for a
       table of no group; read only when the table has a patch). For Placement, MapParts and ChildObj (l_id) every
       other layer of the zone; for Start and GeneralPos (MarioNo; PosName and Obj_ID) Common and the layers some
       scenario turns on together with this table's, by the galaxies' scenario tables after their own patches
       (contract table-keys T-4). A key a patch adds must be new to those: one the game has there leaves the patch out
       (LUMA_PATCH_FAILED, L402), one a higher mod adds there leaves this row out (LUMA_PATCH_CONFLICT, the higher's
       row stays). */
    const LumaLinkTable* links;
    uint32_t link_count;
    /* Every mod that is on, by id, from the bottom of the list up: which of two mods adding one key in the group is
       higher. When a link table has a patch, every mod with a patch here or in `links` must be named, else the state
       is -1 and luma_patched_error names the mod (F3.11-1): never guessed. */
    const char* const* mods;
    uint32_t mod_count;
} LumaPatchInput;

enum {
    LUMA_PATCH_CONFLICT = 0, /* mod_id and other change one row (column null: the whole row); mod_id's is used: it is
                                higher in the list */
    LUMA_PATCH_MISSING = 1,  /* mod_id's file names `row`, which is not there: other removed it, or (null) a
                                lower mod's whole table left it out */
    LUMA_PATCH_SELECTED = 2, /* mod_id's selector number `selector` matched `rows` rows (0: a note, L407) */
    LUMA_PATCH_FAILED = 3    /* mod_id's file could not apply and is left out: `code` is the validator's rule (L400,
                                L402, L403, L406), luma_patched_fact_reason says why in a language (Mods.Patch.Failed's
                                {2}), `why` is that in English, for the log */
};

typedef struct LumaPatchFact {
    uint32_t kind;          /* LUMA_PATCH_* */
    const char* mod_id;
    const char* file;       /* mod_id's patch file; null for a conflict */
    const char* other;      /* the other mod, or null */
    /* The row: its key values in the key's column order, joined by U+001F; "#<n>" by place (a table with no key);
       "*" the whole table; or null. Each value as the validator writes a key (table-keys 5): a whole number in
       decimal, signed at its column's width (a field unsigned); a float in its shortest form; a word as itself, or
       `{bytes:<hex>}` (lower-case hex) when it is not UTF-8, the form JSON gives it as {"bytes":"<hex>"}. Lower case
       for a table whose keys the game compares without case. The screen may show U+001F as ", ". */
    const char* row;
    /* LUMA_PATCH_CONFLICT on one cell: the column's name as mod_id's patch wrote it (the higher's), never a hash;
       null for the whole row (add, remove, rows) and for the other kinds. */
    const char* column;
    const char* why;        /* LUMA_PATCH_FAILED only: the reason in English */
    uint32_t selector;
    uint32_t rows;
    uint32_t code;          /* LUMA_PATCH_FAILED only: the validator's rule, 403 for L403; 0 for the other kinds */
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
/* A LUMA_PATCH_FAILED fact's reason in one of the game's 14 languages by its name ("EuFrench"; null or unknown:
   "UsEnglish"), as the validator words the same fault, without the mod and file (the Mods screen names them). Null
   for a fact of another kind or past the last. Valid until luma_patched_free. */
const char* luma_patched_fact_reason(const LumaPatched* result, uint32_t index, const char* language);
void luma_patched_free(LumaPatched* result);

#ifdef __cplusplus
}
#endif

#endif
