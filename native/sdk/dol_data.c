/* Data the game keeps in its executable rather than in disc files (Petari extracts it from main.dol at build time):
   natively it is read from the user's own main.dol at boot, so the module ships none of it. Each table is a block of
   the DOL's .rodata, at the address the RMGxxx symbol maps give, copied in the DOL's byte order. */
#include <string.h>

#include <revolution/types.h>

#include <nebula_endian.h>
#include <nebula_game_api.h>

#include "nebula_sdk.h"

const NebulaHostServices* nebula_host(void);

#if VERSION == RMGK01
#define ERROR_MESSAGE_ARCHIVE_SIZE 0x1CAC0
#define STORY_PROGRESS_ADDRESS 0x8053DC20
#define GALAXY_INFO_ADDRESS 0x8053DE00
#define ERROR_MESSAGE_ADDRESS 0x8053F5E0
#else
#define ERROR_MESSAGE_ARCHIVE_SIZE 0x9AC0
#define STORY_PROGRESS_ADDRESS 0x8053C620
#define GALAXY_INFO_ADDRESS 0x8053C800
#define ERROR_MESSAGE_ADDRESS 0x8053DFE0
#endif

u8 cStoryProgressTable[0x1E0] __attribute__((aligned(32)));
u8 cGalaxyInfoTable[0xD20] __attribute__((aligned(32)));
u8 cErrorMessageArchive[ERROR_MESSAGE_ARCHIVE_SIZE] __attribute__((aligned(32)));

static const struct {
    const char* name;
    u8* dst;
    u32 size;
    u32 address;
} kTables[] = {
    {"cStoryProgressTable", cStoryProgressTable, sizeof(cStoryProgressTable), STORY_PROGRESS_ADDRESS},
    {"cGalaxyInfoTable", cGalaxyInfoTable, sizeof(cGalaxyInfoTable), GALAXY_INFO_ADDRESS},
    {"cErrorMessageArchive", cErrorMessageArchive, sizeof(cErrorMessageArchive), ERROR_MESSAGE_ADDRESS},
};

/* The DOL header: 7 text then 11 data sections' file offsets (0x00), load addresses (0x48) and sizes (0x90). */
enum { kDolSections = 18, kDolHeaderSize = 0x100 };

/* Tables of string pointers the game compiles in, as main.dol holds them (USA release): the comet schedules of the six
   Grand galaxies (GalaxyCometTimePaper: name, category; 8 bytes) and the event flags (GameEventFlag: 20 bytes). */
typedef struct {
    const char* name;
    s32 category;
} NativeCometPaper;
typedef struct {
    const char* name;
    u8 type;
    u8 save;
    u8 condition1;
    u8 condition2;
    u32 unused;
    const char* condition3;
    const char* condition4;
} NativeEventFlag;

static const u8* sDol;
static size_t sDolSize;

/* A string in main.dol by its address; NULL for 0 or one outside its sections (or unterminated there). */
static const char* dol_string(u32 address) {
    if (address == 0 || sDol == NULL) {
        return NULL;
    }
    for (int s = 0; s < kDolSections; s++) {
        const u32 offset = nebula_load_be32(sDol + 0x00 + s * 4);
        const u32 base = nebula_load_be32(sDol + 0x48 + s * 4);
        const u32 length = nebula_load_be32(sDol + 0x90 + s * 4);
        if (length == 0 || address < base || address - base >= length || offset > sDolSize) {
            continue;
        }
        const u8* text = sDol + offset + (address - base);
        const size_t room = (size_t)(base + length - address);
        if (memchr(text, 0, room) == NULL) {
            return NULL;
        }
        return (const char*)text;
    }
    return NULL;
}

/* main.dol's bytes at an address, `size` of them; NULL when not in one section. */
static const u8* dol_bytes(u32 address, u32 size) {
    for (int s = 0; s < kDolSections && sDol != NULL; s++) {
        const u32 offset = nebula_load_be32(sDol + 0x00 + s * 4);
        const u32 base = nebula_load_be32(sDol + 0x48 + s * 4);
        const u32 length = nebula_load_be32(sDol + 0x90 + s * 4);
        if (length == 0 || address < base || address - base > length || length - (address - base) < size) {
            continue;
        }
        if (offset + (address - base) + size > sDolSize) {
            return NULL;
        }
        return sDol + offset + (address - base);
    }
    return NULL;
}

static int same_string(const char* a, const char* b) {
    return (a == NULL || b == NULL) ? a == b : strcmp(a, b) == 0;
}

#if VERSION == RMGE01
static const struct {
    u32 address;
    u32 size;
} kCometTables[6] = {{0x8053AEF0, 0x88}, {0x8053AF78, 0x88}, {0x8053B000, 0x88},
                     {0x8053B088, 0xC8}, {0x8053B150, 0xC8}, {0x8053B218, 0xC8}};
enum { kEventFlagsAddress = 0x8053B320, kEventFlagsSize = 0xEB0, kEventFlagCount = kEventFlagsSize / 0x14 };
#endif

const void* __NebulaDolTable(u32 kind, const void* compiled) {
#if VERSION == RMGE01
    if (kind < 6) {
        static NativeCometPaper tables[6][26];
        const u8* dol = dol_bytes(kCometTables[kind].address, kCometTables[kind].size);
        const NativeCometPaper* mine = (const NativeCometPaper*)compiled;
        const u32 entries = kCometTables[kind].size / 8;
        if (dol == NULL || entries > 26) {
            return compiled;
        }
        int same = 1;
        int ended = 0;
        for (u32 i = 0; i < entries; i++) {
            NativeCometPaper* out = &tables[kind][i];
            out->name = dol_string(nebula_load_be32(dol + i * 8));
            out->category = (s32)nebula_load_be32(dol + i * 8 + 4);
            if (!ended) {
                same = same && same_string(out->name, mine[i].name) && out->category == mine[i].category;
                ended = mine[i].name == NULL;
            }
        }
        if (same) {
            return compiled;
        }
        nebula_log("main.dol: Grand Galaxy %u's comet schedule differs from the game's (a mod's): main.dol's used\n", kind + 1);
        return tables[kind];
    }
    if (kind == NEBULA_DOL_EVENT_FLAGS) {
        static NativeEventFlag flags[kEventFlagCount];
        const u8* dol = dol_bytes(kEventFlagsAddress, kEventFlagsSize);
        const NativeEventFlag* mine = (const NativeEventFlag*)compiled;
        if (dol == NULL) {
            return compiled;
        }
        int same = 1;
        for (u32 i = 0; i < kEventFlagCount; i++) {
            const u8* e = dol + i * 0x14;
            NativeEventFlag* out = &flags[i];
            out->name = dol_string(nebula_load_be32(e));
            out->type = e[4];
            out->save = e[5];
            out->condition1 = e[6];
            out->condition2 = e[7];
            out->unused = nebula_load_be32(e + 8);
            out->condition3 = dol_string(nebula_load_be32(e + 12));
            out->condition4 = dol_string(nebula_load_be32(e + 16));
            same = same && same_string(out->name, mine[i].name) && out->type == mine[i].type &&
                   out->save == mine[i].save && out->condition1 == mine[i].condition1 &&
                   out->condition2 == mine[i].condition2 && same_string(out->condition3, mine[i].condition3) &&
                   same_string(out->condition4, mine[i].condition4);
        }
        if (same) {
            return compiled;
        }
        nebula_log("main.dol: the event flags differ from the game's (a mod's): main.dol's used\n");
        return flags;
    }
#else
    (void)kind;
#endif
    return compiled;
}

void nebula_load_dol_data(void) {
    size_t size = 0;
    const u8* dol = nebula_host()->disc_dol(nebula_host()->user, &size);
    if (dol == NULL || size < kDolHeaderSize) {
        nebula_fatal("the disc's main.dol is missing or too short (%u bytes)", (unsigned)size);
    }
    /* Kept (the host holds it for the session): tables read later take their strings from it. */
    sDol = dol;
    sDolSize = size;
    for (size_t t = 0; t < sizeof(kTables) / sizeof(kTables[0]); t++) {
        const u32 want = kTables[t].address;
        int found = 0;
        for (int s = 0; s < kDolSections && !found; s++) {
            const u32 offset = nebula_load_be32(dol + 0x00 + s * 4);
            const u32 address = nebula_load_be32(dol + 0x48 + s * 4);
            const u32 length = nebula_load_be32(dol + 0x90 + s * 4);
            if (length == 0 || want < address || want - address > length || length - (want - address) < kTables[t].size) {
                continue;
            }
            const u32 at = offset + (want - address);
            if (at > size || size - at < kTables[t].size) {
                break;
            }
            memcpy(kTables[t].dst, dol + at, kTables[t].size);
            found = 1;
        }
        if (!found) {
            nebula_fatal("%s (0x%08X, 0x%X bytes) is not in the disc's main.dol: not the game version this build is for",
                         kTables[t].name, want, kTables[t].size);
        }
    }
}
