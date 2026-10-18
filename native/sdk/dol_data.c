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

void nebula_load_dol_data(void) {
    size_t size = 0;
    const u8* dol = nebula_host()->disc_dol(nebula_host()->user, &size);
    if (dol == NULL || size < kDolHeaderSize) {
        nebula_fatal("the disc's main.dol is missing or too short (%u bytes)", (unsigned)size);
    }
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
