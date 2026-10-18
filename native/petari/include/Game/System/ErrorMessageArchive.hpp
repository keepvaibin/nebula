#pragma once

#include <revolution/types.h>

#ifdef TARGET_PC
// Natively read from the user's main.dol at boot (Nebula's native/sdk/dol_data.c), never part of the build.
extern "C" u8 cErrorMessageArchive[];
#else
extern const u8 cErrorMessageArchive[];
#endif
