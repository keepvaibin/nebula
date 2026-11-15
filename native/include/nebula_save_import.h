/* SPDX-License-Identifier: GPL-3.0-only */
/* The settings' save import, file by file (runtime save_transfer.h): the game module reads a save the host found and
   checked (a Nebula.sav, or a GameData.bin of any version) as its start reads a save, an old order's files made again
   in host order, and gives it with the save in memory, both in Nebula.sav's form. The host merges the chosen files and
   writes the result before the game starts again. */
#ifndef NEBULA_SAVE_IMPORT_H
#define NEBULA_SAVE_IMPORT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEBULA_SAVE_IMPORT_FILES 6u

enum {
    NEBULA_SAVE_IMPORT_OK = 0,
    NEBULA_SAVE_IMPORT_BUSY = 1,     /* a read is waiting already */
    NEBULA_SAVE_IMPORT_DAMAGED = 2,  /* not a save the game reads: no known form, or its checks fail */
    NEBULA_SAVE_IMPORT_MEMORY = 3,
    NEBULA_SAVE_IMPORT_ARGUMENT = 4,
};

/* One user file of a save, as the game reads it. */
typedef struct NebulaSaveImportFile {
    uint32_t exists;   /* 1: created */
    uint32_t damaged;  /* 1: its config, or its last character's game data, does not load */
    uint32_t luigi;    /* 1: its last character Luigi */
    uint32_t stars;    /* that character's power stars */
    int64_t saved;     /* its last save: local seconds since 2000-01-01 00:00; 0 not known */
} NebulaSaveImportFile;

/* The read's answer, on the game thread, once: NEBULA_SAVE_IMPORT_OK with `imported` (the source's save) and `current`
   (the save in memory, as its next write would put it) as Nebula.sav files and their user files' facts (1 to 6), each
   valid during the call only; else a status and nulls. */
typedef void (*NebulaSaveImportDone)(void* context, int32_t status, const uint8_t* imported, uint32_t imported_length,
                                     const NebulaSaveImportFile* imported_files, const uint8_t* current,
                                     uint32_t current_length, const NebulaSaveImportFile* current_files);

/* The module's export "nebula_save_import_read". Any thread. `bytes` is read at a frame's start, once the game reads
   and writes no save; it stays the caller's and must last until `done` is called. NEBULA_SAVE_IMPORT_OK: `done` is
   called once, never inside this call; NEBULA_SAVE_IMPORT_BUSY or NEBULA_SAVE_IMPORT_ARGUMENT: never. */
typedef int32_t (*NebulaSaveImportRead)(const uint8_t* bytes, uint32_t length, NebulaSaveImportDone done, void* context);

#ifdef __cplusplus
}
#endif

#endif
